const { contextBridge, ipcRenderer } = require('electron');

const api = {
  getConfig: () => ipcRenderer.invoke('api.config'),
  openAudioFile: () => ipcRenderer.invoke('api.openAudioFile'),

  scanPlugins: (roots) => ipcRenderer.invoke('api.plugins.scan', roots),
  listPlugins: () => ipcRenderer.invoke('api.plugins.list'),
  activatePlugin: (path) => ipcRenderer.invoke('api.plugins.activate', path),

  loadFile: (filePath) => ipcRenderer.invoke('api.transport.loadFile', filePath),
  play: () => ipcRenderer.invoke('api.transport.play'),
  pause: () => ipcRenderer.invoke('api.transport.pause'),
  seek: (seconds) => ipcRenderer.invoke('api.transport.seek', seconds),
  setLoop: (payload) => ipcRenderer.invoke('api.transport.setLoop', payload),

  setSampleRate: (sampleRate) => ipcRenderer.invoke('api.engine.setSampleRate', sampleRate),
  setBlockSize: (blockSize) => ipcRenderer.invoke('api.engine.setBlockSize', blockSize),
  setChannels: (channels) => ipcRenderer.invoke('api.engine.setChannels', channels),
  setParam: (payload) => ipcRenderer.invoke('api.engine.setParam', payload),

  runSuite: (payload) => ipcRenderer.invoke('api.tests.runSuite', payload),
  runCase: (payload) => ipcRenderer.invoke('api.tests.runCase', payload),
  cancelTests: () => ipcRenderer.invoke('api.tests.cancel'),

  runQuickAlias: (pluginPath) => ipcRenderer.invoke('api.tests.quickAlias', { pluginPath }),
  runDevQuick: (pluginPath) => ipcRenderer.invoke('api.tests.devQuick', { pluginPath }),
  runReleaseGate: (pluginPath) => ipcRenderer.invoke('api.tests.releaseGate', { pluginPath }),
  runNonlinearScan: (pluginPath) => ipcRenderer.invoke('api.tests.nonlinearScan', { pluginPath }),
  runPresetLoudness: (pluginPath) => ipcRenderer.invoke('api.tests.presetLoudness', { pluginPath }),

  onEvent: (callback) => {
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on('sidecar:event', listener);
    return () => ipcRenderer.removeListener('sidecar:event', listener);
  },
};

contextBridge.exposeInMainWorld('plugalyzerApi', api);
