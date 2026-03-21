import { useState, useEffect, useCallback, useMemo } from 'react';
import { runAutoDetect, pickBestChannels, batchAutoSplit } from '../utils/algorithms';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';
import WaveformCutter from './WaveformCutter';
import SplitPreviewCanvas from './SplitPreviewCanvas';
import FlatDetectorPanel from './FlatDetectorPanel';
import { lastAlgoStore } from '../utils/lastAlgo';

// ─── Batch preview ─────────────────────────────────────────────────────────
function BatchPreview({ results, interval_ms }) {
  return (
    <div style={{ maxHeight: 200, overflowY: 'auto', marginTop: 10 }}>
      <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', marginBottom: 5 }}>
        BATCH — {results.reduce((a, r) => a + r.parts.length, 0)} segments from {results.length} samples
      </div>
      {results.map((r, ri) => (
        <div key={ri} style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 5, padding: '6px 8px', marginBottom: 4 }}>
          <div style={{ fontSize: 10, fontWeight: 700, color: '#38bdf8', marginBottom: 3 }}>
            {r.sample.label}
            {r.sample.sampleName ? <span style={{ color: '#334155', fontWeight: 400 }}> · {r.sample.sampleName}</span> : null}
            <span style={{ color: '#475569', fontWeight: 400 }}> → {r.parts.length} seg{r.parts.length !== 1 ? 's' : ''}</span>
          </div>
          <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }}>
            {r.parts.map((p, pi) => (
              <span key={pi} style={{ background: '#0d2040', color: '#60a5fa', fontSize: 9, borderRadius: 3, padding: '1px 6px', fontFamily: 'monospace' }}>
                {pi + 1}: {p.length}pt · {formatMs(p.length * interval_ms)}
              </span>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}

// ─── Slider helper ─────────────────────────────────────────────────────────
function Sldr({ label: l, value, min, max, step = 1, fmt, onChange }) {
  return (
    <div style={{ flex: 1, minWidth: 120 }}>
      <span style={{ color: '#64748b', fontSize: 10, display: 'block', marginBottom: 2 }}>
        {l}: <b style={{ color: '#94a3b8' }}>{fmt ? fmt(value) : value}</b>
      </span>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(Number(e.target.value))}
        style={{ width: '100%', accentColor: '#3b82f6' }} />
    </div>
  );
}

// ─── Main SplitModal ────────────────────────────────────────────────────────
export default function SplitModal({ sample, samples, allSamples = [], onSplit, onBatchSplit, onClose }) {
  const isBatch       = !sample && Array.isArray(samples) && samples.length > 0;
  const primarySample = sample || samples?.[0];
  const { sensors, values, interval_ms, label } = primarySample;
  const N = values.length;

  // ── State (initialised from lastAlgoStore) ─────────────────────────────
  const [mode, setMode]         = useState(lastAlgoStore.mode || 'auto');
  const [cuts, setCuts]         = useState([]);
  const [removedSegs, setRemovedSegs] = useState(new Set());
  const [partLabels, setPartLabels]   = useState({});
  const [batchResults, setBatchResults] = useState(null);

  // Sensor visibility
  const [visSensors, setVisSensors] = useState(() => new Set(sensors.slice(0, 6)));
  // Ref sensors (multi) — auto-pick by default
  const [refSensors, setRefSensors] = useState(() => new Set(pickBestChannels(values, sensors, 3)));

  // Auto params — restored from lastAlgoStore
  const [algo, setAlgo]             = useState(lastAlgoStore.algo);
  const [windowSize, setWindowSize] = useState(lastAlgoStore.windowSize);
  const [threshold, setThreshold]   = useState(lastAlgoStore.threshold);
  const [stdMult, setStdMult]       = useState(lastAlgoStore.stdMult);
  const [sensitivity, setSensitivity] = useState(lastAlgoStore.sensitivity);
  const [minGap, setMinGap]         = useState(lastAlgoStore.minGap ?? Math.max(20, Math.floor(N * 0.05)));
  const [numParts, setNumParts]     = useState(lastAlgoStore.numParts);
  const [equalParts, setEqualParts] = useState(lastAlgoStore.equalParts || 4);

  // Random shift
  const [enableShift, setEnableShift] = useState(lastAlgoStore.shiftEnabled);
  const [shiftMin, setShiftMin]       = useState(lastAlgoStore.shiftMin);
  const [shiftMax, setShiftMax]       = useState(lastAlgoStore.shiftMax);
  const [shiftUnit, setShiftUnit]     = useState(lastAlgoStore.shiftUnit);
  const [shiftPreview, setShiftPreview] = useState([]); // simulated shifted cuts for preview

  // Padding on kept segments
  const [enablePad, setEnablePad]   = useState(lastAlgoStore.padEnabled);
  const [padMin, setPadMin]         = useState(Math.max(0, Number(lastAlgoStore.padMin ?? 0)));
  const [padMax, setPadMax]         = useState(Math.max(0, Number(lastAlgoStore.padMax ?? 0)));
  const [padUnit, setPadUnit]       = useState(lastAlgoStore.padUnit);
  const [padRandom, setPadRandom]   = useState(lastAlgoStore.padRandom);

  const sameNumArray = useCallback((a = [], b = []) => {
    if (a === b) return true;
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }, []);

  const handleFlatCutsFound = useCallback((newCuts) => {
    const normalized = [...(newCuts || [])]
      .map(v => Math.round(Number(v) || 0))
      .filter(v => v > 0 && v < N)
      .sort((a, b) => a - b);

    setCuts(prev => {
      if (sameNumArray(prev, normalized)) return prev;
      setRemovedSegs(new Set());
      return normalized;
    });
  }, [N, sameNumArray]);

  // Persist to store when algo params change
  useEffect(() => {
    Object.assign(lastAlgoStore, {
      mode, algo, windowSize, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts,
      shiftEnabled: enableShift, shiftMin, shiftMax, shiftUnit,
      padEnabled: enablePad, padMin, padMax, padUnit, padRandom,
    });
  }, [mode, algo, windowSize, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts, enableShift, shiftMin, shiftMax, shiftUnit,
      enablePad, padMin, padMax, padUnit, padRandom]);

  // ── Derived ────────────────────────────────────────────────────────────
  const allCuts    = useMemo(() => [0, ...cuts, N].sort((a, b) => a - b), [cuts, N]);
  const segments   = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);
  const keptSegs   = useMemo(() => segments.filter(s => !removedSegs.has(s.idx)), [segments, removedSegs]);

  const toggleRemove = idx => setRemovedSegs(p => { const n = new Set(p); n.has(idx) ? n.delete(idx) : n.add(idx); return n; });

  // Generate a shift preview (simulate random shift visually)
  const generateShiftPreview = useCallback(() => {
    if (!enableShift || !cuts.length) { setShiftPreview([]); return; }
    const shiftLoRaw = shiftUnit === 'ms' ? Math.round(shiftMin / interval_ms) : Math.round(shiftMin);
    const shiftHiRaw = shiftUnit === 'ms' ? Math.round(shiftMax / interval_ms) : Math.round(shiftMax);
    const lo = Math.min(shiftLoRaw, shiftHiRaw);
    const hi = Math.max(shiftLoRaw, shiftHiRaw);
    const preview = cuts.map(c => {
      const shift = Math.round(lo + Math.random() * (hi - lo));
      return Math.max(1, Math.min(N - 1, c + shift));
    }).sort((a, b) => a - b);
    setShiftPreview(preview);
  }, [enableShift, cuts, shiftMin, shiftMax, shiftUnit, interval_ms, N]);

  useEffect(() => { generateShiftPreview(); }, [generateShiftPreview]);

  // ── Recalculate ────────────────────────────────────────────────────────
  const getCfg = useCallback(() => ({
    algorithm: algo, refSensors: [...refSensors],
    windowSize, threshold, stdMult, sensitivity, minGap, numParts,
  }), [algo, refSensors, windowSize, threshold, stdMult, sensitivity, minGap, numParts]);

  const recalc = useCallback(() => {
    if (mode === 'auto') {
      const newCuts = runAutoDetect(values, sensors, getCfg());
      setCuts(newCuts); setRemovedSegs(new Set());
    } else if (mode === 'equal') {
      const sz = Math.floor(N / equalParts);
      setCuts(Array.from({ length: equalParts - 1 }, (_, i) => (i + 1) * sz));
      setRemovedSegs(new Set());
    }
    if (isBatch) setBatchResults(batchAutoSplit(samples, sensors, getCfg()));
  }, [mode, getCfg, values, sensors, N, equalParts, isBatch, samples]);

  useEffect(() => { if (mode !== 'manual') recalc(); }, [mode, recalc]);

  // ── Execute split ──────────────────────────────────────────────────────
  const doSplit = () => {
    if (isBatch) { if (batchResults) onBatchSplit(batchResults, {}); }
    else {
      // Apply random shift if enabled
      let finalCuts = [...cuts];
      if (enableShift) {
        const shiftLoRaw = shiftUnit === 'ms' ? Math.round(shiftMin / interval_ms) : Math.round(shiftMin);
        const shiftHiRaw = shiftUnit === 'ms' ? Math.round(shiftMax / interval_ms) : Math.round(shiftMax);
        const lo = Math.min(shiftLoRaw, shiftHiRaw);
        const hi = Math.max(shiftLoRaw, shiftHiRaw);
        finalCuts = cuts.map(c => {
          const shift = Math.round(lo + Math.random() * (hi - lo));
          return Math.max(1, Math.min(N - 1, c + shift));
        }).sort((a, b) => a - b);
      }

      // Build parts from finalCuts, excluding removed
      const fc = [0, ...finalCuts, N].sort((a, b) => a - b);
      const allParts = fc.slice(0, -1).map((s, i) => {
        let start = s, end = fc[i + 1];
        if (enablePad && !removedSegs.has(i)) {
          const padLoRaw = padUnit === 'ms' ? Math.round(padMin / interval_ms) : Math.round(padMin);
          const padHiRaw = padUnit === 'ms' ? Math.round(padMax / interval_ms) : Math.round(padMax);
          const lo = Math.max(0, Math.min(padLoRaw, padHiRaw));
          const hi = Math.max(lo, Math.max(padLoRaw, padHiRaw));
          const actualPad = padRandom ? Math.round(lo + Math.random() * (hi - lo)) : lo;
          start = Math.max(0, start - actualPad);
          end   = Math.min(N, end + actualPad);
        }
        return values.slice(start, end);
      });
      const finalParts = allParts.filter((_, i) => !removedSegs.has(i));
      onSplit(sample, finalParts, partLabels);
    }
    onClose();
  };

  const SI = { background: '#060d1a', color: '#f1f5f9', border: '1px solid #1e293b', borderRadius: 5, padding: '5px 9px', fontSize: 11, boxSizing: 'border-box', fontFamily: 'inherit' };
  const totalKept = isBatch
    ? (batchResults ? batchResults.reduce((a, r) => a + r.parts.length, 0) : 0)
    : keptSegs.length;

  // Cuts to show in preview — if shift enabled, show the shifted preview
  const previewCuts = enableShift && shiftPreview.length ? shiftPreview : cuts;

  const padInfo = enablePad ? { min: padMin, max: padMax, unit: padUnit, random: padRandom } : null;

  return (
    <div style={{ position: 'fixed', inset: 0, background: '#000d', zIndex: 200, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 12, backdropFilter: 'blur(4px)' }}>
      <div style={{ background: '#0a1628', border: '1px solid #1e3a5f', borderRadius: 14, width: 'min(1080px, 100%)', maxHeight: '96vh', overflowY: 'auto', boxShadow: '0 32px 80px #0008' }}>

        {/* Header */}
        <div style={{ padding: '13px 18px', borderBottom: '1px solid #1e293b', display: 'flex', alignItems: 'center', gap: 10, position: 'sticky', top: 0, background: '#0a1628', zIndex: 1 }}>
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 14, fontWeight: 700, color: '#f1f5f9' }}>
              {isBatch ? `✂️ Batch Split — ${samples.length} samples` : '✂️ Smart Split'}
            </div>
            <div style={{ fontSize: 10, color: '#475569' }}>
              {isBatch ? `Preview: ${label}` : label} · {N} pts · {formatMs(N * interval_ms)}
            </div>
          </div>
          <button onClick={onClose} style={{ background: 'none', border: 'none', color: '#475569', fontSize: 20, cursor: 'pointer' }}>✕</button>
        </div>

        <div style={{ padding: 16 }}>
          {/* Mode tabs */}
          <div style={{ display: 'flex', gap: 3, background: '#050c1a', borderRadius: 8, padding: 4, marginBottom: 14 }}>
            {[['auto', '🤖 Auto-Detect'], ['equal', '⚖️ Equal Parts'], ['manual', '✏️ Manual'], ['flat', '📉 Predicted Flat']].map(([m, lbl]) => (
              <button key={m} onClick={() => setMode(m)} style={{
                flex: 1, background: mode === m ? (m === 'flat' ? '#0a2a0a' : '#0d2040') : 'none',
                border: `1px solid ${mode === m ? (m === 'flat' ? '#34d399' : '#3b82f6') : 'transparent'}`,
                color: mode === m ? (m === 'flat' ? '#34d399' : '#60a5fa') : '#475569',
                borderRadius: 6, padding: '7px 0', cursor: 'pointer',
                fontSize: 11, fontFamily: 'inherit', fontWeight: mode === m ? 700 : 400,
              }}>{lbl}</button>
            ))}
          </div>

          {/* Auto settings */}
          {mode === 'auto' && (
            <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
              <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 10 }}>DETECTION</div>
              <div style={{ display: 'flex', gap: 10, marginBottom: 10 }}>
                <div style={{ flex: 1 }}>
                  <label style={{ color: '#64748b', fontSize: 10, display: 'block', marginBottom: 3 }}>Algorithm</label>
                  <select value={algo} onChange={e => setAlgo(e.target.value)} style={{ ...SI, width: '100%' }}>
                    <option value="energy">Energy Valley — low-activity gaps</option>
                    <option value="threshold">Threshold Crossing — N·σ surges</option>
                    <option value="derivative">Rate of Change — sharp transitions</option>
                    <option value="zero_crossing">Zero Crossing — oscillation-based</option>
                    <option value="peak2peak">Peak-to-Peak — amplitude change</option>
                    <option value="variance">Variance Shift — statistical change</option>
                    <option value="equal">Fixed Count — N equal parts</option>
                  </select>
                </div>
              </div>
              {/* Multi-channel ref */}
              <div style={{ marginBottom: 10 }}>
                <div style={{ fontSize: 10, color: '#64748b', marginBottom: 4 }}>
                  Reference Channels (multi, RMS-merged) — <span style={{ color: '#38bdf8' }}>auto-picked: highest variance</span>
                </div>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3 }}>
                  {sensors.map((s, i) => (
                    <button key={s} onClick={() => setRefSensors(p => { const n = new Set(p); n.has(s) ? n.delete(s) : n.add(s); return n; })} style={{
                      background: refSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] + '33' : '#0a1628',
                      border: `1px solid ${refSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#1e293b'}`,
                      color: refSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#475569',
                      borderRadius: 4, padding: '2px 7px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace', fontWeight: refSensors.has(s) ? 700 : 400,
                    }}>{s}</button>
                  ))}
                </div>
              </div>
              <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', marginBottom: 10 }}>
                {(algo === 'energy' || algo === 'peak2peak' || algo === 'variance') && <Sldr label="Window" value={windowSize} min={5} max={200} onChange={setWindowSize} fmt={v => `${v}pts`} />}
                {algo === 'energy' && <Sldr label="Threshold" value={Math.round(threshold * 100)} min={5} max={80} onChange={v => setThreshold(v / 100)} fmt={v => `${v}%`} />}
                {algo === 'threshold' && <Sldr label="Std ×" value={Math.round(stdMult * 10)} min={5} max={50} onChange={v => setStdMult(v / 10)} fmt={v => `${(v / 10).toFixed(1)}σ`} />}
                {(algo === 'derivative' || algo === 'variance') && <Sldr label="Sensitivity" value={Math.round(sensitivity * 100)} min={5} max={90} onChange={v => setSensitivity(v / 100)} fmt={v => `${v}%`} />}
                {algo === 'equal' && <Sldr label="Parts" value={numParts} min={2} max={20} onChange={setNumParts} />}
                {algo !== 'equal' && <Sldr label="Min Gap" value={minGap} min={5} max={Math.floor(N / 2)} onChange={setMinGap} fmt={v => `${v}pts`} />}
              </div>
              <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                <button onClick={recalc} style={{ background: '#1d4ed8', border: 'none', color: '#fff', borderRadius: 6, padding: '7px 18px', cursor: 'pointer', fontSize: 11, fontWeight: 700, fontFamily: 'inherit' }}>
                  ⟳ Recalculate
                </button>
                <span style={{ fontSize: 10, color: '#475569' }}>
                  {cuts.length} cuts → {cuts.length + 1} segments
                  {isBatch ? ` × ${samples.length} samples` : ''}
                </span>
              </div>
            </div>
          )}

          {/* Equal settings */}
          {mode === 'equal' && (
            <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
              <Sldr label="Parts" value={equalParts} min={2} max={30} onChange={setEqualParts} />
              <div style={{ fontSize: 10, color: '#475569', marginTop: 4 }}>
                Each: {Math.floor(N / equalParts)} pts · {formatMs(Math.floor(N / equalParts) * interval_ms)}
              </div>
            </div>
          )}

          {/* Manual hint */}
          {mode === 'manual' && !isBatch && (
            <div style={{ background: '#050c1a', border: '1px dashed #1e293b', borderRadius: 8, padding: 10, marginBottom: 12, fontSize: 10, color: '#64748b' }}>
              Click waveform to add cuts · Drag handles to move · Dbl-click to remove
            </div>
          )}

          {/* Predicted Flat mode */}
          {mode === 'flat' && !isBatch && (
            <FlatDetectorPanel
              targetSample={sample}
              allSamples={allSamples}
              sensors={sensors}
              onCutsFound={handleFlatCutsFound}
            />
          )}

          {/* ── Random Shift ── */}
          {!isBatch && (
            <div style={{ background: '#050c1a', border: `1px solid ${enableShift ? '#f59e0b55' : '#1e293b'}`, borderRadius: 10, padding: 12, marginBottom: 10 }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: enableShift ? 10 : 0 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none' }}>
                  <input type="checkbox" checked={enableShift} onChange={e => setEnableShift(e.target.checked)} style={{ accentColor: '#f59e0b', width: 13, height: 13 }} />
                  <span style={{ fontSize: 11, fontWeight: 700, color: enableShift ? '#fbbf24' : '#64748b' }}>🎲 Random Shift</span>
                </label>
                {enableShift && (
                  <span style={{ fontSize: 9, color: '#64748b' }}>
                    Randomly shifts each cut boundary — preview shows a sample result
                  </span>
                )}
                {enableShift && (
                  <button onClick={generateShiftPreview} style={{ background: '#451a03', border: '1px solid #f59e0b55', color: '#fbbf24', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit', marginLeft: 'auto' }}>
                    🔀 Resample preview
                  </button>
                )}
              </div>
              {enableShift && (
                <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', alignItems: 'flex-end' }}>
                  <div>
                    <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Min shift</span>
                    <input type="number" value={shiftMin} onChange={e => setShiftMin(Number(e.target.value))}
                      style={{ ...SI, width: 70 }} />
                  </div>
                  <div>
                    <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Max shift</span>
                    <input type="number" value={shiftMax} onChange={e => setShiftMax(Number(e.target.value))}
                      style={{ ...SI, width: 70 }} />
                  </div>
                  <div>
                    <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Unit</span>
                    <div style={{ display: 'flex', gap: 3 }}>
                      {['pts', 'ms'].map(u => (
                        <button key={u} onClick={() => setShiftUnit(u)} style={{
                          background: shiftUnit === u ? '#451a03' : '#060d1a',
                          border: `1px solid ${shiftUnit === u ? '#f59e0b' : '#1e293b'}`,
                          color: shiftUnit === u ? '#fbbf24' : '#64748b',
                          borderRadius: 4, padding: '4px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                        }}>{u}</button>
                      ))}
                    </div>
                  </div>
                  <span style={{ fontSize: 10, color: '#64748b', alignSelf: 'center' }}>
                    ≈ ±{Math.max(Math.abs(shiftMin), Math.abs(shiftMax))}{shiftUnit}
                    {shiftUnit === 'pts' && ` (${formatMs(Math.max(Math.abs(shiftMin), Math.abs(shiftMax)) * interval_ms)})`}
                  </span>
                </div>
              )}
            </div>
          )}

          {/* ── Padding ── */}
          {!isBatch && (
            <div style={{ background: '#050c1a', border: `1px solid ${enablePad ? '#a78bfa55' : '#1e293b'}`, borderRadius: 10, padding: 12, marginBottom: 12 }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: enablePad ? 10 : 0 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none' }}>
                  <input type="checkbox" checked={enablePad} onChange={e => setEnablePad(e.target.checked)} style={{ accentColor: '#a78bfa', width: 13, height: 13 }} />
                  <span style={{ fontSize: 11, fontWeight: 700, color: enablePad ? '#a78bfa' : '#64748b' }}>📐 Padding</span>
                </label>
                {enablePad && (
                  <span style={{ fontSize: 9, color: '#64748b' }}>
                    Extends each kept segment at both ends · shown as amber zones
                  </span>
                )}
              </div>
              {enablePad && (
                <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', alignItems: 'flex-end' }}>
                  <div>
                    <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>{padRandom ? 'Min pad' : 'Pad size'}</span>
                    <input type="number" min={0} value={padMin} onChange={e => setPadMin(Math.max(0, Number(e.target.value)))}
                      style={{ ...SI, width: 70 }} />
                  </div>
                  {padRandom && (
                    <div>
                      <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Max pad</span>
                      <input type="number" min={0} value={padMax} onChange={e => setPadMax(Math.max(0, Number(e.target.value)))}
                        style={{ ...SI, width: 70 }} />
                    </div>
                  )}
                  <div>
                    <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Unit</span>
                    <div style={{ display: 'flex', gap: 3 }}>
                      {['pts', 'ms'].map(u => (
                        <button key={u} onClick={() => setPadUnit(u)} style={{
                          background: padUnit === u ? '#1a0a33' : '#060d1a',
                          border: `1px solid ${padUnit === u ? '#a78bfa' : '#1e293b'}`,
                          color: padUnit === u ? '#a78bfa' : '#64748b',
                          borderRadius: 4, padding: '4px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                        }}>{u}</button>
                      ))}
                    </div>
                  </div>
                  <label style={{ display: 'flex', alignItems: 'center', gap: 5, cursor: 'pointer', fontSize: 10, color: padRandom ? '#a78bfa' : '#64748b', userSelect: 'none' }}>
                    <input type="checkbox" checked={padRandom} onChange={e => setPadRandom(e.target.checked)} style={{ accentColor: '#a78bfa' }} />
                    Random
                  </label>
                  <span style={{ fontSize: 10, color: '#64748b', alignSelf: 'center' }}>
                    ≈ {padRandom ? `${padMin}–${padMax}` : padMin}{padUnit}
                    {padUnit === 'pts' && ` (${formatMs(padMin * interval_ms)})`}
                    {' · shown as amber in preview'}
                  </span>
                </div>
              )}
            </div>
          )}

          {/* Sensor vis toggles */}
          {!isBatch && (
            <>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, marginBottom: 7 }}>
                <span style={{ fontSize: 9, color: '#334155', alignSelf: 'center', marginRight: 4 }}>PREVIEW CHANNELS:</span>
                {sensors.map((s, i) => (
                  <button key={s} onClick={() => setVisSensors(p => { const n = new Set(p); n.has(s) ? n.delete(s) : n.add(s); return n; })} style={{
                    background: visSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] + '22' : '#050c1a',
                    border: `1px solid ${visSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#1e293b'}`,
                    color: visSensors.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#334155',
                    borderRadius: 4, padding: '2px 7px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
                  }}>{s}</button>
                ))}
              </div>

              {/* EI-style preview with overlay option */}
              <SplitPreviewCanvas
                values={values} sensors={sensors} interval_ms={interval_ms}
                cutPoints={previewCuts}
                removedSegments={removedSegs}
                onToggleRemove={toggleRemove}
                activeSensors={visSensors}
                padding={padInfo}
                height={250}
              />

              {/* Shift indicator under canvas */}
              {enableShift && shiftPreview.length > 0 && (
                <div style={{ marginTop: 5, padding: '5px 10px', background: '#451a0322', border: '1px solid #f59e0b33', borderRadius: 5, fontSize: 9, color: '#fbbf24' }}>
                  🎲 Preview shows a simulated random shift — actual shifts will differ per split. Cuts shifted by [{shiftMin},{shiftMax}]{shiftUnit}.
                </div>
              )}

              {/* Manual cutter (manual mode only) */}
              {mode === 'manual' && (
                <div style={{ marginTop: 10 }}>
                  <div style={{ fontSize: 10, color: '#64748b', marginBottom: 5 }}>Fine-tune cut points:</div>
                  <WaveformCutter values={values} sensors={sensors} interval_ms={interval_ms}
                    cutPoints={cuts} onCutsChange={c => { setCuts(c); setRemovedSegs(new Set()); }}
                    activeSensors={[...visSensors]} height={130} />
                </div>
              )}

              {/* Segment label editors */}
              {keptSegs.length > 0 && (
                <div style={{ marginTop: 12 }}>
                  <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', marginBottom: 6 }}>
                    {keptSegs.length} KEPT SEGMENT{keptSegs.length !== 1 ? 'S' : ''}
                    {removedSegs.size > 0 && <span style={{ color: '#f87171', fontWeight: 400 }}> · {removedSegs.size} removed</span>}
                  </div>
                  <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(140px, 1fr))', gap: 5 }}>
                    {keptSegs.map((seg, ki) => {
                      const segValues = values.slice(seg.start, seg.end);
                      return (
                        <div key={seg.idx} style={{ background: '#050c1a', border: `1px solid ${SENSOR_COLORS[ki % SENSOR_COLORS.length]}44`, borderRadius: 7, padding: 8 }}>
                          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 3 }}>
                            <span style={{ fontSize: 10, fontWeight: 700, color: SENSOR_COLORS[ki % SENSOR_COLORS.length] }}>Seg {seg.idx + 1}</span>
                            <span style={{ fontSize: 9, color: '#475569' }}>{formatMs(segValues.length * interval_ms)}</span>
                          </div>
                          <div style={{ fontSize: 9, color: '#334155', marginBottom: 5 }}>{segValues.length} pts</div>
                          <input value={partLabels[seg.idx] !== undefined ? partLabels[seg.idx] : label}
                            onChange={e => setPartLabels(p => ({ ...p, [seg.idx]: e.target.value }))}
                            placeholder={label}
                            style={{ background: '#0a1628', color: '#94a3b8', border: '1px solid #1e293b', borderRadius: 4, padding: '3px 7px', fontSize: 10, width: '100%', boxSizing: 'border-box', fontFamily: 'inherit' }} />
                        </div>
                      );
                    })}
                  </div>
                </div>
              )}
            </>
          )}

          {/* Batch preview */}
          {isBatch && batchResults && <BatchPreview results={batchResults} interval_ms={interval_ms} />}
          {isBatch && !batchResults && (
            <div style={{ textAlign: 'center', color: '#334155', padding: 20, fontSize: 11 }}>
              Configure above then click Recalculate to preview
            </div>
          )}

          {/* Footer */}
          <div style={{ display: 'flex', gap: 8, justifyContent: 'space-between', alignItems: 'center', marginTop: 16, paddingTop: 14, borderTop: '1px solid #1e293b' }}>
            <div style={{ fontSize: 10, color: '#475569', display: 'flex', gap: 10, flexWrap: 'wrap' }}>
              {!isBatch && <>
                <span style={{ color: '#34d399' }}>{keptSegs.length} segments kept</span>
                {removedSegs.size > 0 && <span style={{ color: '#f87171' }}>· {removedSegs.size} removed</span>}
                {enableShift && <span style={{ color: '#fbbf24' }}>· shift ±{Math.max(Math.abs(shiftMin), Math.abs(shiftMax))}{shiftUnit}</span>}
                {enablePad && <span style={{ color: '#a78bfa' }}>· pad {padRandom ? `${padMin}–${padMax}` : padMin}{padUnit}</span>}
              </>}
              {isBatch && batchResults && <span style={{ color: '#34d399' }}>{totalKept} total segments</span>}
            </div>
            <div style={{ display: 'flex', gap: 8 }}>
              <button onClick={onClose} style={{ background: '#050c1a', border: '1px solid #1e293b', color: '#64748b', borderRadius: 6, padding: '8px 18px', cursor: 'pointer', fontFamily: 'inherit' }}>Cancel</button>
              <button onClick={doSplit}
                disabled={isBatch ? !batchResults : keptSegs.length < 1}
                style={{
                  background: (isBatch ? batchResults : keptSegs.length >= 1) ? '#1d4ed8' : '#1e293b',
                  border: 'none',
                  color: (isBatch ? batchResults : keptSegs.length >= 1) ? '#fff' : '#334155',
                  borderRadius: 6, padding: '8px 22px', cursor: 'pointer', fontWeight: 700, fontFamily: 'inherit',
                }}>
                {isBatch
                  ? `✂️ Split ${samples.length} → ${totalKept} total`
                  : `✂️ Split → ${keptSegs.length} sample${keptSegs.length !== 1 ? 's' : ''}`}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
