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
import type { TestVisualPoint } from '../types';
import { Card, CardContent, CardHeader, CardTitle } from './ui/card';

type MetricSpec = {
  fields: string[];
  mode: 'line' | 'bar';
};

const metricRegistry: Record<string, MetricSpec> = {
  load: { fields: ['loadPass'], mode: 'bar' },
  validate: { fields: ['validationPass'], mode: 'bar' },
  determinism: { fields: ['determinismResidualDbfs'], mode: 'line' },
  stateRoundtrip: { fields: ['stateRoundtripResidualDbfs'], mode: 'line' },
  aliasing: { fields: ['aliasingRatioDb', 'aliasingMeanRatioDb'], mode: 'line' },
  saturationFingerprint: { fields: ['saturationWorstThdDb', 'saturationOddEvenImbalanceDb'], mode: 'line' },
  eqCurve: { fields: ['eqMaxErrorDb', 'eqRmsErrorDb'], mode: 'line' },
  phaseGroupDelay: { fields: ['phaseDeviationDeg'], mode: 'line' },
  thdn: { fields: ['thdnDb'], mode: 'line' },
  imd: { fields: ['imdDb'], mode: 'line' },
  noiseDc: { fields: ['noiseFloorDbfs', 'dcOffset'], mode: 'line' },
  latency: { fields: ['latencyErrorSamples'], mode: 'line' },
  automationZipper: { fields: ['zipperArtifactDb'], mode: 'line' },
  bypassClickPop: { fields: ['bypassClickPeakDbfs'], mode: 'line' },
  perfStress: { fields: ['realtimeFactor', 'memoryDriftMb'], mode: 'line' },
  presetGain: { fields: ['presetGainSpreadDb', 'presetCount'], mode: 'bar' },
  abx: { fields: ['abxLoudnessDeltaDb', 'abxTrialCount'], mode: 'line' },
};

const palette = ['#22d3ee', '#f59e0b', '#f43f5e', '#a78bfa'];

function mapData(points: TestVisualPoint[], fields: string[]) {
  return points.map((p, index) => {
    const row: Record<string, number> = { index };
    for (const f of fields) {
      row[f] = Number(p.metrics[f] ?? 0);
    }
    return row;
  });
}

export function TestVisualizationPanel({ testType, points }: { testType: string; points: TestVisualPoint[] }): JSX.Element {
  const spec = metricRegistry[testType] ?? { fields: ['value'], mode: 'line' as const };
  const fields = spec.fields;
  const data = mapData(points, fields);

  return (
    <Card className="test-panel">
      <CardHeader>
        <CardTitle>{testType}</CardTitle>
      </CardHeader>
      <CardContent className="test-panel-content">
        <ResponsiveContainer width="100%" height="100%">
          {spec.mode === 'bar' ? (
            <BarChart data={data}>
              <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" />
              <XAxis dataKey="index" stroke="#94a3b8" />
              <YAxis stroke="#94a3b8" />
              <Tooltip />
              {fields.map((field, i) => (
                <Bar key={field} dataKey={field} fill={palette[i % palette.length]} />
              ))}
            </BarChart>
          ) : (
            <LineChart data={data}>
              <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" />
              <XAxis dataKey="index" stroke="#94a3b8" />
              <YAxis stroke="#94a3b8" />
              <Tooltip />
              {fields.map((field, i) => (
                <Line key={field} dataKey={field} stroke={palette[i % palette.length]} dot={false} />
              ))}
            </LineChart>
          )}
        </ResponsiveContainer>
      </CardContent>
    </Card>
  );
}
