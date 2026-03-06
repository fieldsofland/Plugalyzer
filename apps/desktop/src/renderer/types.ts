export interface PluginVersionInfo {
  pluginId: string;
  displayName: string;
  format: string;
  buildType: string;
  version: string;
  path: string;
  mtime: number;
}

export interface EngineState {
  sessionId: string;
  sampleRate: number;
  blockSize: number;
  channels: number;
  playing: boolean;
  bypass: boolean;
  playheadSec: number;
  loopEnabled: boolean;
  loopStartSec: number;
  loopEndSec: number;
  outputGainDb: number;
  inputFile: string;
  activePlugin?: PluginVersionInfo;
}

export interface MeterState {
  inputPeakDbfs: number;
  outputPeakDbfs: number;
  outputRmsDbfs: number;
  outputLufs: number;
  cpuPercent: number;
  xruns: number;
}

export interface SidecarEvent {
  method: string;
  params: Record<string, any>;
}

export interface TestVisualPoint {
  t: number;
  caseId: string;
  metrics: Record<string, number>;
}

export interface DesktopConfig {
  repoRoot: string;
  suitePaths: Record<string, string>;
  defaultOutDir: string;
}

declare global {
  interface Window {
    plugalyzerApi: {
      getConfig: () => Promise<DesktopConfig>;
      openAudioFile: () => Promise<string | null>;

      scanPlugins: (roots?: string[]) => Promise<{ plugins: PluginVersionInfo[] }>;
      listPlugins: () => Promise<{ plugins: PluginVersionInfo[] }>;
      activatePlugin: (path: string) => Promise<any>;

      loadFile: (path: string) => Promise<any>;
      play: () => Promise<any>;
      pause: () => Promise<any>;
      seek: (seconds: number) => Promise<any>;
      setLoop: (payload: { startSec: number; endSec: number; enabled: boolean }) => Promise<any>;

      setSampleRate: (sampleRate: number) => Promise<any>;
      setBlockSize: (blockSize: number) => Promise<any>;
      setChannels: (channels: number) => Promise<any>;
      setParam: (payload: { name: string; value: number }) => Promise<any>;

      runSuite: (payload: {
        suite: string;
        pluginPath?: string;
        outDir?: string;
        jobs?: number;
        maxSummaryCases?: number;
      }) => Promise<any>;
      runCase: (payload: {
        suite: string;
        caseId: string;
        pluginPath?: string;
        outDir?: string;
        jobs?: number;
        maxSummaryCases?: number;
      }) => Promise<any>;
      cancelTests: () => Promise<any>;

      runQuickAlias: (pluginPath?: string) => Promise<any>;
      runDevQuick: (pluginPath?: string) => Promise<any>;
      runReleaseGate: (pluginPath?: string) => Promise<any>;
      runNonlinearScan: (pluginPath?: string) => Promise<any>;
      runPresetLoudness: (pluginPath?: string) => Promise<any>;

      onEvent: (callback: (event: SidecarEvent) => void) => () => void;
    };
  }
}
