import { useState, useMemo } from 'react';
import { SENSOR_COLORS, CATEGORY_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';

export default function WindowedCountTab({ samples, theme }) {
  const [windowSize, setWindowSize] = useState(50);
  const [stride, setStride]         = useState(25);
  const [countMode, setCountMode]   = useState('samples'); // 'samples' | 'windows'

  const interval_ms = samples[0]?.interval_ms || 33.33;

  const stats = useMemo(() => {
    const byLabel = {};
    samples.filter(s => s.values?.length > 0).forEach(s => {
      if (!byLabel[s.label]) byLabel[s.label] = { training: { samples: 0, windows: 0 }, testing: { samples: 0, windows: 0 } };
      const cat = s.category === 'testing' ? 'testing' : 'training';
      byLabel[s.label][cat].samples++;
      const wins = s.values.length >= windowSize
        ? Math.floor((s.values.length - windowSize) / stride) + 1
        : 0;
      byLabel[s.label][cat].windows += wins;
    });
    return byLabel;
  }, [samples, windowSize, stride]);

  const labels = Object.keys(stats).sort();
  const maxVal = Math.max(1, ...labels.flatMap(l => {
    const r = stats[l];
    return countMode === 'windows'
      ? [r.training.windows + r.testing.windows]
      : [r.training.samples + r.testing.samples];
  }));

  const totals = labels.reduce((acc, l) => {
    const r = stats[l];
    acc.trainSamples += r.training.samples;
    acc.testSamples  += r.testing.samples;
    acc.trainWindows += r.training.windows;
    acc.testWindows  += r.testing.windows;
    return acc;
  }, { trainSamples: 0, testSamples: 0, trainWindows: 0, testWindows: 0 });

  const INP = { background: theme.bgInput, color: theme.textPrimary, border: `1px solid ${theme.border}`, borderRadius: 5, padding: '4px 8px', fontSize: 11, fontFamily: 'inherit', width: 80 };

  return (
    <div>
      {/* Controls */}
      <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14, marginBottom: 14 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10 }}>Windowing Parameters</div>
        <div style={{ display: 'flex', gap: 20, flexWrap: 'wrap', alignItems: 'flex-end', marginBottom: 10 }}>
          <div>
            <div style={{ fontSize: 10, color: theme.textMuted, marginBottom: 4 }}>Window Size (pts)</div>
            <input type="number" min={1} value={windowSize} onChange={e => setWindowSize(Math.max(1, Number(e.target.value)))} style={INP} />
            <div style={{ fontSize: 9, color: theme.textDim, marginTop: 3 }}>{formatMs(windowSize * interval_ms)}</div>
          </div>
          <div>
            <div style={{ fontSize: 10, color: theme.textMuted, marginBottom: 4 }}>Stride (pts)</div>
            <input type="number" min={1} value={stride} onChange={e => setStride(Math.max(1, Number(e.target.value)))} style={INP} />
            <div style={{ fontSize: 9, color: theme.textDim, marginTop: 3 }}>{formatMs(stride * interval_ms)}</div>
          </div>
          <div>
            <div style={{ fontSize: 10, color: theme.textMuted, marginBottom: 4 }}>Show</div>
            <div style={{ display: 'flex', gap: 3 }}>
              {[['samples', 'Samples'], ['windows', 'Windows']].map(([v, l]) => (
                <button key={v} onClick={() => setCountMode(v)} style={{
                  background: countMode === v ? '#1d4ed8' : theme.bgInput,
                  border: `1px solid ${countMode === v ? '#3b82f6' : theme.border}`,
                  color: countMode === v ? '#fff' : theme.textMuted,
                  borderRadius: 4, padding: '4px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                }}>{l}</button>
              ))}
            </div>
          </div>
        </div>
        <div style={{ fontSize: 9, color: theme.textDim, lineHeight: 1.7 }}>
          Windows per sample = ⌊(N − W) / S⌋ + 1 where N = sample length, W = {windowSize}, S = {stride}
          <br />Total — Train: {totals.trainSamples} samples / {totals.trainWindows} windows &nbsp;·&nbsp; Test: {totals.testSamples} samples / {totals.testWindows} windows
        </div>
      </div>

      {/* Per-label bars */}
      <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10 }}>
          Per-Label {countMode === 'windows' ? 'Window' : 'Sample'} Count
        </div>
        {/* Header */}
        <div style={{ display: 'flex', gap: 8, marginBottom: 6, fontSize: 9, color: theme.textDim, paddingRight: 8 }}>
          <span style={{ minWidth: 130 }}>Label</span>
          <span style={{ flex: 1 }}>Distribution</span>
          <span style={{ minWidth: 50, textAlign: 'right', color: CATEGORY_COLORS.training }}>Train</span>
          <span style={{ minWidth: 50, textAlign: 'right', color: CATEGORY_COLORS.testing }}>Test</span>
          <span style={{ minWidth: 50, textAlign: 'right' }}>Total</span>
        </div>
        {labels.map((label, i) => {
          const r = stats[label];
          const trainVal = countMode === 'windows' ? r.training.windows : r.training.samples;
          const testVal  = countMode === 'windows' ? r.testing.windows  : r.testing.samples;
          const total = trainVal + testVal;
          const trainPct = (trainVal / maxVal) * 100;
          const testPct  = (testVal  / maxVal) * 100;
          return (
            <div key={label} style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6 }}>
              <span style={{ fontSize: 10, color: theme.textSecondary, minWidth: 130, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} title={label}>{label}</span>
              <div style={{ flex: 1, height: 10, background: theme.border, borderRadius: 5, display: 'flex', overflow: 'hidden' }}>
                <div style={{ width: `${trainPct}%`, background: CATEGORY_COLORS.training, transition: 'width 0.3s' }} />
                <div style={{ width: `${testPct}%`, background: CATEGORY_COLORS.testing, transition: 'width 0.3s' }} />
              </div>
              <span style={{ fontSize: 10, color: CATEGORY_COLORS.training, minWidth: 50, textAlign: 'right' }}>{trainVal}</span>
              <span style={{ fontSize: 10, color: CATEGORY_COLORS.testing,  minWidth: 50, textAlign: 'right' }}>{testVal}</span>
              <span style={{ fontSize: 10, color: theme.textSecondary, fontWeight: 700, minWidth: 50, textAlign: 'right' }}>{total}</span>
            </div>
          );
        })}
        {labels.length === 0 && (
          <div style={{ color: theme.textDim, textAlign: 'center', padding: 24, fontSize: 11 }}>No samples loaded</div>
        )}
      </div>
    </div>
  );
}
