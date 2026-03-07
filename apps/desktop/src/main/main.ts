import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import path from 'node:path';
import { spawn, ChildProcessWithoutNullStreams } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { SidecarClient, SidecarEvent } from './sidecarClient.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const repoRoot = path.resolve(__dirname, '../../../../');
const defaultVstTestBin = path.join(repoRoot, 'build/Plugalyzer_artefacts/Release/vst-test');
const sidecarPort = Number(process.env.VST_TEST_UI_PORT ?? 47555);

const suitePaths = {
  quickAlias: path.join(repoRoot, 'suites/examples/chorus80.alias-quick.example.json'),
  devQuick: path.join(repoRoot, 'suites/examples/chorus80.dev-quick.example.json'),
  releaseGate: path.join(repoRoot, 'suites/examples/chorus80.release-gate.example.json'),
  nonlinearScan: path.join(repoRoot, 'suites/examples/chorus80.nonlinear-scan.example.json'),
  presetLoudness: path.join(repoRoot, 'suites/examples/chorus80.preset-loudness.example.json'),
};

let mainWindow: BrowserWindow | null = null;
let sidecarProcess: ChildProcessWithoutNullStreams | null = null;
const sidecar = new SidecarClient();

function broadcastSidecarEvent(event: SidecarEvent): void {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  mainWindow.webContents.send('sidecar:event', event);
}

async function connectSidecarWithRetry(retries = 120): Promise<void> {
  for (let i = 0; i < retries; i += 1) {
    try {
      await sidecar.connect('127.0.0.1', sidecarPort, 1200);
      return;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
  }
  throw new Error('Unable to connect to ui-server sidecar');
}

async function launchSidecar(): Promise<void> {
  const sidecarBin = process.env.VST_TEST_BIN ?? defaultVstTestBin;

  sidecarProcess = spawn(sidecarBin, ['ui-server', '--port', String(sidecarPort), '--session', 'desktop'], {
    cwd: repoRoot,
    env: process.env,
  });

  sidecarProcess.stdout.on('data', (data) => {
    const text = data.toString();
    if (text.trim().length > 0) {
      console.log(`[ui-server] ${text.trim()}`);
    }
  });

  sidecarProcess.stderr.on('data', (data) => {
    const text = data.toString();
    if (text.trim().length > 0) {
      console.error(`[ui-server:error] ${text.trim()}`);
    }
  });

  sidecarProcess.on('exit', (code) => {
    console.log(`[ui-server] exited with code ${code ?? 'unknown'}`);
    sidecarProcess = null;
  });

  await connectSidecarWithRetry();
  sidecar.on('event', broadcastSidecarEvent);
  sidecar.on('error', (error) => {
    broadcastSidecarEvent({ method: 'engine.error', params: { message: String(error.message ?? error) } });
  });
}

function createWindow(): void {
  mainWindow = new BrowserWindow({
    width: 1500,
    height: 980,
    backgroundColor: '#0f1117',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  const devServerUrl = process.env.VITE_DEV_SERVER_URL;
  if (devServerUrl) {
    void mainWindow.loadURL(devServerUrl);
    mainWindow.webContents.openDevTools({ mode: 'detach' });
  } else {
    void mainWindow.loadFile(path.join(repoRoot, 'apps/desktop/dist/renderer/index.html'));
  }
}

function ensureConnected(): void {
  if (!sidecar) {
    throw new Error('Sidecar client is not initialized');
  }
}

function registerIpcHandlers(): void {
  ipcMain.handle('api.config', async () => ({
    repoRoot,
    suitePaths,
    defaultOutDir: path.join(repoRoot, '.vst-test/runs-ui'),
  }));

  ipcMain.handle('api.openAudioFile', async () => {
    const win = BrowserWindow.getFocusedWindow() ?? mainWindow;
    const result = await dialog.showOpenDialog(win!, {
      properties: ['openFile'],
      filters: [{ name: 'Audio', extensions: ['wav', 'aiff', 'aif', 'flac', 'mp3'] }],
    });
    if (result.canceled || result.filePaths.length === 0) {
      return null;
    }
    return result.filePaths[0];
  });

  ipcMain.handle('api.plugins.scan', async (_event, roots?: string[]) => {
    ensureConnected();
    return sidecar.request('plugins.scan', roots ? { roots } : {});
  });

  ipcMain.handle('api.plugins.list', async () => {
    ensureConnected();
    return sidecar.request('plugins.list', {});
  });

  ipcMain.handle('api.plugins.activate', async (_event, pathValue: string) => {
    ensureConnected();
    return sidecar.request('plugins.activateVersion', { path: pathValue });
  });

  ipcMain.handle('api.transport.loadFile', async (_event, filePath: string) => {
    ensureConnected();
    return sidecar.request('transport.loadFile', { path: filePath });
  });

  ipcMain.handle('api.transport.play', async () => sidecar.request('transport.play', {}));
  ipcMain.handle('api.transport.pause', async () => sidecar.request('transport.pause', {}));
  ipcMain.handle('api.transport.seek', async (_event, seconds: number) =>
    sidecar.request('transport.seek', { seconds }),
  );
  ipcMain.handle('api.transport.setLoop', async (_event, payload: { startSec: number; endSec: number; enabled: boolean }) =>
    sidecar.request('transport.setLoop', payload),
  );

  ipcMain.handle('api.engine.setSampleRate', async (_event, sampleRate: number) =>
    sidecar.request('engine.setSampleRate', { sampleRate }),
  );
  ipcMain.handle('api.engine.setBlockSize', async (_event, blockSize: number) =>
    sidecar.request('engine.setBlockSize', { blockSize }),
  );
  ipcMain.handle('api.engine.setChannels', async (_event, channels: number) =>
    sidecar.request('engine.setChannels', { channels }),
  );
  ipcMain.handle('api.engine.setParam', async (_event, payload: { name: string; value: number }) =>
    sidecar.request('engine.setParam', payload),
  );

  ipcMain.handle('api.tests.runSuite', async (_event, payload: {
    suite: string;
    pluginPath?: string;
    outDir?: string;
    jobs?: number;
    maxSummaryCases?: number;
  }) =>
    sidecar.request('tests.runSuite', payload),
  );

  ipcMain.handle('api.tests.runCase', async (_event, payload: {
    suite: string;
    caseId: string;
    pluginPath?: string;
    outDir?: string;
    jobs?: number;
    maxSummaryCases?: number;
  }) =>
    sidecar.request('tests.runCase', payload),
  );

  ipcMain.handle('api.tests.cancel', async () => sidecar.request('tests.cancel', {}));

  ipcMain.handle('api.tests.quickAlias', async (_event, payload: { pluginPath?: string }) =>
    sidecar.request('tests.runSuite', {
      suite: suitePaths.quickAlias,
      pluginPath: payload.pluginPath,
      outDir: path.join(repoRoot, '.vst-test/runs-ui'),
      jobs: 1,
      maxSummaryCases: 12,
    }),
  );

  ipcMain.handle('api.tests.devQuick', async (_event, payload: { pluginPath?: string }) =>
    sidecar.request('tests.runSuite', {
      suite: suitePaths.devQuick,
      pluginPath: payload.pluginPath,
      outDir: path.join(repoRoot, '.vst-test/runs-ui'),
      jobs: 1,
      maxSummaryCases: 15,
    }),
  );

  ipcMain.handle('api.tests.releaseGate', async (_event, payload: { pluginPath?: string }) =>
    sidecar.request('tests.runSuite', {
      suite: suitePaths.releaseGate,
      pluginPath: payload.pluginPath,
      outDir: path.join(repoRoot, '.vst-test/runs-ui'),
      jobs: 1,
      maxSummaryCases: 20,
    }),
  );

  ipcMain.handle('api.tests.nonlinearScan', async (_event, payload: { pluginPath?: string }) =>
    sidecar.request('tests.runSuite', {
      suite: suitePaths.nonlinearScan,
      pluginPath: payload.pluginPath,
      outDir: path.join(repoRoot, '.vst-test/runs-ui'),
      jobs: 1,
      maxSummaryCases: 20,
    }),
  );

  ipcMain.handle('api.tests.presetLoudness', async (_event, payload: { pluginPath?: string }) =>
    sidecar.request('tests.runSuite', {
      suite: suitePaths.presetLoudness,
      pluginPath: payload.pluginPath,
      outDir: path.join(repoRoot, '.vst-test/runs-ui'),
      jobs: 1,
      maxSummaryCases: 40,
    }),
  );
}

app.whenReady().then(async () => {
  registerIpcHandlers();
  await launchSidecar();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', async () => {
  try {
    await sidecar.request('server.shutdown', {});
  } catch {
    // ignore
  }
  sidecar.disconnect();
  if (sidecarProcess) {
    sidecarProcess.kill();
  }
});
