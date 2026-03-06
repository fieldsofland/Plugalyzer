import { useEffect, useMemo, useState } from 'react';
import {
  Bar,
  BarChart,
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { useAppStore } from './store';
import type { PluginVersionInfo } from './types';
import { Separator } from './components/base/Separator';
import { TestVisualizationPanel } from './components/TestVisualizationPanel';
import { Button } from './components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from './components/ui/card';

function formatDb(value: number): string {
  return `${value.toFixed(1)} dB`;
}

export default function App(): JSX.Element {
  const {
    config,
    plugins,
    activePluginPath,
    engineState,
    meterHistory,
    spectrumBins,
    visualByTestType,
    testRunState,
    testSummary,
    errors,
    setConfig,
    setPlugins,
    setActivePluginPath,
    ingestEvent,
    pushError,
  } = useAppStore();

  const [selectedPluginId, setSelectedPluginId] = useState('');
  const [scanRootsText, setScanRootsText] = useState('/Users/matt/dev/vst');
  const [compatibilityDiff, setCompatibilityDiff] = useState<Record<string, unknown> | null>(null);

  useEffect(() => {
    const unsubscribe = window.plugalyzerApi.onEvent((event) => {
      ingestEvent(event);
    });

    void (async () => {
      try {
        const cfg = await window.plugalyzerApi.getConfig();
        setConfig(cfg);
        if (cfg?.repoRoot) {
          setScanRootsText('/Users/matt/dev/vst');
        }
        const listed = await window.plugalyzerApi.listPlugins();
        setPlugins(listed.plugins ?? []);
      } catch (error) {
        pushError(String((error as Error).message ?? error));
      }
    })();

    return unsubscribe;
  }, [ingestEvent, pushError, setConfig, setPlugins]);

  const grouped = useMemo(() => {
    const map = new Map<string, PluginVersionInfo[]>();
    for (const plugin of plugins) {
      const key = plugin.pluginId;
      const current = map.get(key) ?? [];
      current.push(plugin);
      map.set(key, current);
    }
    return map;
  }, [plugins]);

  const selectedVersions = grouped.get(selectedPluginId) ?? [];

  async function scanPlugins(): Promise<void> {
    try {
      const roots = scanRootsText
        .split(/[\n,]+/)
        .map((value) => value.trim())
        .filter((value) => value.length > 0);
      const result = await window.plugalyzerApi.scanPlugins(roots.length > 0 ? roots : undefined);
      setPlugins(result.plugins ?? []);
    } catch (error) {
      pushError(String((error as Error).message ?? error));
    }
  }

  async function activate(path: string): Promise<void> {
    try {
      const result = await window.plugalyzerApi.activatePlugin(path);
      setActivePluginPath(path);
      setCompatibilityDiff((result?.compatibilityDiff as Record<string, unknown>) ?? null);
    } catch (error) {
      pushError(String((error as Error).message ?? error));
    }
  }

  async function loadAudio(): Promise<void> {
    const file = await window.plugalyzerApi.openAudioFile();
    if (!file) {
      return;
    }
    await window.plugalyzerApi.loadFile(file);
  }

  async function runQuickAlias(): Promise<void> {
    await window.plugalyzerApi.runQuickAlias(activePluginPath || undefined);
  }

  async function runDevQuick(): Promise<void> {
    await window.plugalyzerApi.runDevQuick(activePluginPath || undefined);
  }

  async function runReleaseGate(): Promise<void> {
    await window.plugalyzerApi.runReleaseGate(activePluginPath || undefined);
  }

  async function runNonlinearScan(): Promise<void> {
    await window.plugalyzerApi.runNonlinearScan(activePluginPath || undefined);
  }

  async function runPresetLoudness(): Promise<void> {
    await window.plugalyzerApi.runPresetLoudness(activePluginPath || undefined);
  }

  async function runReleaseCycle(): Promise<void> {
    await runReleaseGate();
    await runNonlinearScan();
    await runPresetLoudness();
  }

  const latestMeters = meterHistory[meterHistory.length - 1];

  return (
    <div className="app-shell">
      <header className="app-header">
        <div>
          <h1>Plugalyzer Desktop</h1>
          <p>Local GUI for plugin version hotswap, realtime transport, and live test dashboards.</p>
        </div>
        <div className="header-actions">
          <Button variant="secondary" onClick={scanPlugins}>Scan Plugins</Button>
          <Button onClick={runQuickAlias}>Quick Alias</Button>
          <Button variant="secondary" onClick={runDevQuick}>Dev Quick</Button>
          <Button variant="secondary" onClick={runReleaseCycle}>Release Cycle</Button>
          <Button variant="secondary" onClick={runReleaseGate}>Release Gate</Button>
          <Button variant="secondary" onClick={runNonlinearScan}>Nonlinear Scan</Button>
          <Button variant="secondary" onClick={runPresetLoudness}>Preset Loudness</Button>
        </div>
      </header>

      <main className="layout-grid">
        <Card className="panel">
          <CardHeader>
            <CardTitle>Plugin Family</CardTitle>
          </CardHeader>
          <CardContent>
            <label className="muted" htmlFor="scan-roots-input">Scan Roots (comma/newline separated)</label>
            <textarea
              id="scan-roots-input"
              className="field scan-roots"
              value={scanRootsText}
              onChange={(e) => setScanRootsText(e.target.value)}
            />
            <div className="row">
              <Button variant="secondary" size="sm" onClick={scanPlugins}>Scan Selected Roots</Button>
            </div>
            <select
              className="field"
              value={selectedPluginId}
              onChange={(e) => setSelectedPluginId(e.target.value)}
            >
              <option value="">Select a plugin</option>
              {Array.from(grouped.keys()).map((id) => (
                <option key={id} value={id}>{id}</option>
              ))}
            </select>
            <Separator className="separator-spaced" />
            <div className="versions-list">
              {selectedVersions.map((plugin) => {
                const isActive = activePluginPath === plugin.path || engineState?.activePlugin?.path === plugin.path;
                return (
                  <button
                    key={plugin.path}
                    className={`version-row ${isActive ? 'active' : ''}`}
                    onClick={() => activate(plugin.path)}
                  >
                    <div className="version-main">{plugin.displayName}</div>
                    <div className="version-meta">{plugin.format.toUpperCase()} / {plugin.buildType} / v{plugin.version}</div>
                  </button>
                );
              })}
            </div>
            {compatibilityDiff ? (
              <>
                <Separator className="separator-spaced" />
                <div className="muted">Hotswap Compatibility Diff</div>
                <pre className="json-block compact">{JSON.stringify(compatibilityDiff, null, 2)}</pre>
              </>
            ) : null}
          </CardContent>
        </Card>

        <Card className="panel">
          <CardHeader>
            <CardTitle>Realtime Transport</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="row">
              <Button variant="secondary" onClick={loadAudio}>Load Audio File</Button>
              <Button onClick={() => window.plugalyzerApi.play()}>Play</Button>
              <Button variant="secondary" onClick={() => window.plugalyzerApi.pause()}>Pause</Button>
              <Button variant="ghost" onClick={() => window.plugalyzerApi.seek(0)}>Seek 0</Button>
            </div>
            <div className="row muted">Input: {engineState?.inputFile || 'None loaded'}</div>
            <div className="row muted">Playhead: {(engineState?.playheadSec ?? 0).toFixed(2)}s</div>
            <Separator className="separator-spaced" />
            <div className="meter-grid">
              <div className="meter-box">
                <span>Output Peak</span>
                <strong>{latestMeters ? formatDb(latestMeters.outputPeakDbfs) : '--'}</strong>
              </div>
              <div className="meter-box">
                <span>Output RMS</span>
                <strong>{latestMeters ? formatDb(latestMeters.outputRmsDbfs) : '--'}</strong>
              </div>
              <div className="meter-box">
                <span>Output LUFS</span>
                <strong>{latestMeters ? latestMeters.outputLufs.toFixed(1) : '--'}</strong>
              </div>
              <div className="meter-box">
                <span>CPU</span>
                <strong>{latestMeters ? `${latestMeters.cpuPercent.toFixed(1)}%` : '--'}</strong>
              </div>
            </div>
          </CardContent>
        </Card>

        <Card className="panel wide">
          <CardHeader>
            <CardTitle>Live Meters + Spectrum</CardTitle>
          </CardHeader>
          <CardContent className="charts-grid">
            <div className="chart-box">
              <ResponsiveContainer width="100%" height="100%">
                <LineChart data={meterHistory.map((m, i) => ({ i, output: m.outputPeakDbfs, rms: m.outputRmsDbfs }))}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" />
                  <XAxis dataKey="i" stroke="#94a3b8" />
                  <YAxis stroke="#94a3b8" />
                  <Tooltip />
                  <Line type="monotone" dataKey="output" stroke="#22d3ee" dot={false} />
                  <Line type="monotone" dataKey="rms" stroke="#f59e0b" dot={false} />
                </LineChart>
              </ResponsiveContainer>
            </div>
            <div className="chart-box">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={spectrumBins.map((v, i) => ({ i, v }))}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" />
                  <XAxis dataKey="i" stroke="#94a3b8" />
                  <YAxis stroke="#94a3b8" />
                  <Tooltip />
                  <Bar dataKey="v" fill="#a78bfa" />
                </BarChart>
              </ResponsiveContainer>
            </div>
          </CardContent>
        </Card>

        <Card className="panel wide">
          <CardHeader>
            <CardTitle>Test Run Status</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="status-row">Running: {testRunState ? JSON.stringify(testRunState) : 'idle'}</div>
            <Button variant="danger" size="sm" onClick={() => window.plugalyzerApi.cancelTests()}>Cancel Tests</Button>
            <pre className="json-block">{JSON.stringify(testSummary, null, 2)}</pre>
          </CardContent>
        </Card>

        <div className="visual-grid">
          {Object.entries(visualByTestType).map(([testType, points]) => (
            <TestVisualizationPanel key={testType} testType={testType} points={points} />
          ))}
        </div>

        <Card className="panel wide">
          <CardHeader>
            <CardTitle>Errors</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="errors">
              {errors.map((err, i) => (
                <div key={`${err}-${i}`}>{err}</div>
              ))}
            </div>
            <div className="muted">Repo Root: {config?.repoRoot ?? 'loading...'}</div>
          </CardContent>
        </Card>
      </main>
    </div>
  );
}
