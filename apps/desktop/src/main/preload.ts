import { contextBridge, ipcRenderer } from 'electron';

type SidecarEvent = {
  method: string;
  params: Record<string, unknown>;
};

const api = {
  getConfig: () => ipcRenderer.invoke('api.config'),
  openAudioFile: () => ipcRenderer.invoke('api.openAudioFile'),

  scanPlugins: (roots?: string[]) => ipcRenderer.invoke('api.plugins.scan', roots),
  listPlugins: () => ipcRenderer.invoke('api.plugins.list'),
  activatePlugin: (path: string) => ipcRenderer.invoke('api.plugins.activate', path),

  loadFile: (filePath: string) => ipcRenderer.invoke('api.transport.loadFile', filePath),
  play: () => ipcRenderer.invoke('api.transport.play'),
  pause: () => ipcRenderer.invoke('api.transport.pause'),
  seek: (seconds: number) => ipcRenderer.invoke('api.transport.seek', seconds),
  setLoop: (payload: { startSec: number; endSec: number; enabled: boolean }) =>
    ipcRenderer.invoke('api.transport.setLoop', payload),

  setSampleRate: (sampleRate: number) => ipcRenderer.invoke('api.engine.setSampleRate', sampleRate),
  setBlockSize: (blockSize: number) => ipcRenderer.invoke('api.engine.setBlockSize', blockSize),
  setChannels: (channels: number) => ipcRenderer.invoke('api.engine.setChannels', channels),
  setParam: (payload: { name: string; value: number }) => ipcRenderer.invoke('api.engine.setParam', payload),

  runSuite: (payload: {
    suite: string;
    pluginPath?: string;
    outDir?: string;
    jobs?: number;
    maxSummaryCases?: number;
  }) =>
    ipcRenderer.invoke('api.tests.runSuite', payload),
  runCase: (payload: {
    suite: string;
    caseId: string;
    pluginPath?: string;
    outDir?: string;
    jobs?: number;
    maxSummaryCases?: number;
  }) =>
    ipcRenderer.invoke('api.tests.runCase', payload),
  cancelTests: () => ipcRenderer.invoke('api.tests.cancel'),

  runQuickAlias: (pluginPath?: string) => ipcRenderer.invoke('api.tests.quickAlias', { pluginPath }),
  runDevQuick: (pluginPath?: string) => ipcRenderer.invoke('api.tests.devQuick', { pluginPath }),
  runReleaseGate: (pluginPath?: string) => ipcRenderer.invoke('api.tests.releaseGate', { pluginPath }),
  runNonlinearScan: (pluginPath?: string) => ipcRenderer.invoke('api.tests.nonlinearScan', { pluginPath }),
  runPresetLoudness: (pluginPath?: string) => ipcRenderer.invoke('api.tests.presetLoudness', { pluginPath }),

  onEvent: (callback: (event: SidecarEvent) => void) => {
    const listener = (_event: unknown, payload: SidecarEvent) => callback(payload);
    ipcRenderer.on('sidecar:event', listener);
    return () => ipcRenderer.removeListener('sidecar:event', listener);
  },
};

contextBridge.exposeInMainWorld('plugalyzerApi', api);

export type DesktopApi = typeof api;
