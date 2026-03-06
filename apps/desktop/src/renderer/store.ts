import { create } from 'zustand';
import type {
  DesktopConfig,
  EngineState,
  MeterState,
  PluginVersionInfo,
  SidecarEvent,
  TestVisualPoint,
} from './types';

const MAX_HISTORY = 240;

interface AppState {
  config: DesktopConfig | null;
  plugins: PluginVersionInfo[];
  activePluginPath: string;
  engineState: EngineState | null;
  meterHistory: Array<MeterState & { t: number }>;
  spectrumBins: number[];
  testRunState: Record<string, unknown> | null;
  testSummary: Record<string, unknown> | null;
  visualByTestType: Record<string, TestVisualPoint[]>;
  errors: string[];

  setConfig: (config: DesktopConfig) => void;
  setPlugins: (plugins: PluginVersionInfo[]) => void;
  setActivePluginPath: (path: string) => void;
  ingestEvent: (event: SidecarEvent) => void;
  pushError: (message: string) => void;
}

function pushBounded<T>(arr: T[], item: T, max = MAX_HISTORY): T[] {
  const next = [...arr, item];
  if (next.length <= max) {
    return next;
  }
  return next.slice(next.length - max);
}

export const useAppStore = create<AppState>((set) => ({
  config: null,
  plugins: [],
  activePluginPath: '',
  engineState: null,
  meterHistory: [],
  spectrumBins: [],
  testRunState: null,
  testSummary: null,
  visualByTestType: {},
  errors: [],

  setConfig: (config) => set({ config }),
  setPlugins: (plugins) => set({ plugins }),
  setActivePluginPath: (path) => set({ activePluginPath: path }),

  pushError: (message) =>
    set((state) => ({
      errors: pushBounded(state.errors, message, 50),
    })),

  ingestEvent: (event) => {
    set((state) => {
      if (event.method === 'plugins.updated') {
        return {
          plugins: (event.params.plugins as PluginVersionInfo[]) ?? state.plugins,
        };
      }

      if (event.method === 'transport.state') {
        return {
          engineState: event.params as EngineState,
        };
      }

      if (event.method === 'transport.meters') {
        const t = Date.now();
        return {
          meterHistory: pushBounded(state.meterHistory, {
            t,
            inputPeakDbfs: Number(event.params.inputPeakDbfs ?? -120),
            outputPeakDbfs: Number(event.params.outputPeakDbfs ?? -120),
            outputRmsDbfs: Number(event.params.outputRmsDbfs ?? -120),
            outputLufs: Number(event.params.outputLufs ?? -120),
            cpuPercent: Number(event.params.cpuPercent ?? 0),
            xruns: Number(event.params.xruns ?? 0),
          }),
        };
      }

      if (event.method === 'transport.spectrum') {
        return {
          spectrumBins: Array.isArray(event.params.binsDb)
            ? (event.params.binsDb as number[])
            : state.spectrumBins,
        };
      }

      if (event.method === 'tests.started' || event.method === 'tests.progress') {
        return {
          testRunState: event.params,
        };
      }

      if (event.method === 'tests.metric' || event.method === 'tests.completed') {
        return {
          testSummary: event.params,
          testRunState: event.method === 'tests.completed' ? null : state.testRunState,
        };
      }

      if (event.method === 'tests.visual') {
        const testType = String(event.params.testType ?? 'unknown');
        const caseId = String(event.params.caseId ?? '');
        const metrics = (event.params.metrics ?? {}) as Record<string, number>;
        const current = state.visualByTestType[testType] ?? [];
        return {
          visualByTestType: {
            ...state.visualByTestType,
            [testType]: pushBounded(current, {
              t: Date.now(),
              caseId,
              metrics,
            }),
          },
        };
      }

      if (event.method === 'engine.error') {
        const msg = String(event.params.message ?? 'Unknown engine error');
        return {
          errors: pushBounded(state.errors, msg, 50),
        };
      }

      return state;
    });
  },
}));
