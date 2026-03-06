import net from 'node:net';
import { EventEmitter } from 'node:events';

export interface SidecarEvent {
  method: string;
  params: Record<string, unknown>;
}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
}

export class SidecarClient extends EventEmitter {
  private socket: net.Socket | null = null;
  private buffer = '';
  private nextId = 1;
  private pending = new Map<number, PendingRequest>();

  async connect(host: string, port: number, timeoutMs = 5000): Promise<void> {
    if (this.socket && !this.socket.destroyed) {
      return;
    }

    await new Promise<void>((resolve, reject) => {
      const socket = new net.Socket();
      const timeout = setTimeout(() => {
        socket.destroy();
        reject(new Error(`Timed out connecting to sidecar on ${host}:${port}`));
      }, timeoutMs);

      socket.setEncoding('utf8');
      socket.once('error', (error) => {
        clearTimeout(timeout);
        reject(error);
      });

      socket.connect(port, host, () => {
        clearTimeout(timeout);
        this.socket = socket;
        this.bindSocket(socket);
        resolve();
      });
    });
  }

  private bindSocket(socket: net.Socket): void {
    socket.on('data', (chunk: string) => {
      this.buffer += chunk;
      while (true) {
        const newlineIndex = this.buffer.indexOf('\n');
        if (newlineIndex < 0) {
          break;
        }
        const line = this.buffer.slice(0, newlineIndex).trim();
        this.buffer = this.buffer.slice(newlineIndex + 1);
        if (!line) {
          continue;
        }
        this.handleLine(line);
      }
    });

    socket.on('close', () => {
      this.emit('disconnect');
      this.socket = null;
    });

    socket.on('error', (error) => {
      this.emit('error', error);
    });
  }

  private handleLine(line: string): void {
    let message: any;
    try {
      message = JSON.parse(line);
    } catch {
      this.emit('error', new Error(`Invalid sidecar JSON line: ${line}`));
      return;
    }

    if (message.method && !Object.prototype.hasOwnProperty.call(message, 'id')) {
      const event: SidecarEvent = {
        method: String(message.method),
        params: (message.params ?? {}) as Record<string, unknown>,
      };
      this.emit('event', event);
      return;
    }

    if (Object.prototype.hasOwnProperty.call(message, 'id')) {
      const id = Number(message.id);
      const pending = this.pending.get(id);
      if (!pending) {
        return;
      }

      this.pending.delete(id);
      if (message.error) {
        pending.reject(new Error(String(message.error.message ?? 'Unknown JSON-RPC error')));
        return;
      }

      pending.resolve(message.result);
    }
  }

  request<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.socket || this.socket.destroyed) {
      return Promise.reject(new Error('Sidecar is not connected'));
    }

    const id = this.nextId++;
    const payload = JSON.stringify({
      jsonrpc: '2.0',
      id,
      method,
      params,
    }) + '\n';

    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, {
        resolve: (value) => resolve(value as T),
        reject,
      });
      this.socket!.write(payload);
    });
  }

  disconnect(): void {
    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
    }
    this.socket = null;
  }
}
