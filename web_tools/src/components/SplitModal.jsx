import { useState, useEffect, useCallback, useMemo, useRef } from 'react';
import { Scissors, RefreshCw, X, Shuffle, Trash2 } from 'lucide-react';
import { runAutoDetect, pickBestChannels, batchAutoSplit } from '../utils/algorithms';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';
import WaveformCutter from './WaveformCutter';
import SplitPreviewCanvas from './SplitPreviewCanvas';
import FlatDetectorPanel from './FlatDetectorPanel';
import { lastAlgoStore } from '../utils/lastAlgo';
import { runFlatDetector } from '../utils/flatDetector';
import { groupSensorsByDiscriminant } from '../utils/flatDetector';
import { getSensorGroup } from './WaveformViewer';

// ─── Batch preview ─────────────────────────────────────────────────────────
function BatchGraphs({ samples, batchStates, sensors, visSensors: initialVisSensors, padInfo,
  onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, onRemoveSample, onBatchCutsChange }) {

  // Per-panel local sensor visibility (starts from global visSensors)
  const [visSensors, setVisSensors] = useState(() => new Set(initialVisSensors || sensors.slice(0, 6)));

  const toggleSensor = s => setVisSensors(p => { const n = new Set(p); n.has(s) ? n.delete(s) : n.add(s); return n; });
  const allOn  = sensors.every(s => visSensors.has(s));
  const allOff = sensors.every(s => !visSensors.has(s));

  return (
    <div style={{ marginTop: 10 }}>
      {/* Global sensor selector for all batch previews */}
      <div style={{ background: '#060d1a', border: '1px solid #1e293b', borderRadius: 8, padding: '7px 10px', marginBottom: 10, display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
        <span style={{ fontSize: 9, color: '#64748b', fontWeight: 700, whiteSpace: 'nowrap' }}>Preview channels:</span>
        <button onClick={() => setVisSensors(new Set(sensors))}
          style={{ background: allOn ? '#0d2040' : 'none', border: `1px solid ${allOn ? '#3b82f6' : '#1e293b'}`, color: allOn ? '#60a5fa' : '#475569', borderRadius: 4, padding: '1px 7px', fontSize: 8, cursor: 'pointer', fontFamily: 'inherit' }}>
          All
        </button>
        <button onClick={() => setVisSensors(new Set())}
          style={{ background: allOff ? '#1a0808' : 'none', border: `1px solid ${allOff ? '#7f1d1d' : '#1e293b'}`, color: allOff ? '#f87171' : '#475569', borderRadius: 4, padding: '1px 7px', fontSize: 8, cursor: 'pointer', fontFamily: 'inherit' }}>
          None
        </button>
        <div style={{ width: 1, height: 12, background: '#1e293b' }} />
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3 }}>
          {sensors.map((s, i) => {
            const on = visSensors.has(s);
            return (
              <button key={s} onClick={() => toggleSensor(s)} style={{
                background: on ? SENSOR_COLORS[i % SENSOR_COLORS.length] + '22' : '#050c1a',
                border: `1px solid ${on ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#1e293b'}`,
                color: on ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#334155',
                borderRadius: 3, padding: '1px 5px', fontSize: 8, cursor: 'pointer',
                fontFamily: 'monospace', fontWeight: on ? 700 : 400,
              }}>{s}</button>
            );
          })}
        </div>
      </div>

      {/* Per-sample cards */}
      <div style={{ maxHeight: '50vh', overflowY: 'auto', display: 'flex', flexDirection: 'column', gap: 14 }}>
        {samples.map((smpl) => {
          const state = batchStates[smpl.id] || { cuts: [], removedSegs: new Set() };
          const totalSegs = (state.cuts?.length || 0) + 1;
          return (
            <div key={smpl.id} style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 8, padding: 10 }}>
              <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 6, display: 'flex', alignItems: 'center', gap: 8 }}>
                <span style={{ flex: 1, minWidth: 0 }}>
                  {smpl.label}{smpl.sampleName ? <span style={{ color: '#64748b' }}> · {smpl.sampleName}</span> : ''}
                  <span style={{ color: '#475569', fontWeight: 400, marginLeft: 8 }}>
                    {totalSegs - state.removedSegs.size}/{totalSegs} kept
                  </span>
                </span>
                <button onClick={() => onRemoveSample?.(smpl.id)}
                  style={{ background: '#450a0a', border: '1px solid #7f1d1d', color: '#f87171', borderRadius: 4, padding: '2px 7px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
                  Remove
                </button>
              </div>
              <SplitPreviewCanvas
                values={smpl.values} sensors={sensors} interval_ms={smpl.interval_ms}
                cutPoints={state.cuts}
                removedSegments={state.removedSegs}
                onToggleRemove={idx => onToggleRemove(smpl.id, idx)}
                onCombineLeft={idx => onCombineLeft(smpl.id, idx)}
                onCombineRight={idx => onCombineRight(smpl.id, idx)}
                onSplitSegment={idx => onSplitSegment(smpl.id, idx)}
                onCutsChange={newCuts => onBatchCutsChange?.(smpl.id, newCuts)}
                activeSensors={visSensors}
                padding={padInfo}
                height={140}
              />
              <div style={{ marginTop: 6, display: 'flex', flexWrap: 'wrap', gap: 3 }}>
                {Array.from({ length: totalSegs }, (_, i) => {
                  const kept = !state.removedSegs.has(i);
                  return (
                    <button key={i} onClick={() => onToggleRemove(smpl.id, i)} style={{
                      background: kept ? '#052e16' : '#450a0a44',
                      border: `1px solid ${kept ? '#166534' : '#7f1d1d'}`,
                      color: kept ? '#34d399' : '#f87171',
                      borderRadius: 4, padding: '2px 7px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
                    }}>S{i + 1} {kept ? '✓' : '✕'}</button>
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

// ─── Range pair input (lo–hi for a single boundary) ──────────────────────
// ─── Range pair input — defines a [lo, hi] interval ──────────────────────
// For shift: each cut is shifted by a value sampled uniformly from [lo, hi]
// For padding: each segment gets a pad sampled uniformly from [lo, hi] (or fixed lo when not random)
function RangePair({ label, hint, lo, hi, onLoChange, onHiChange, unit, interval_ms, color = '#94a3b8', allowNegative = false, SI }) {
  const toMs = v => {
    if (unit !== 'pts' || !interval_ms) return '';
    const ms = Math.abs(v) * interval_ms;
    return ms < 1000 ? ` (${ms.toFixed(0)}ms)` : ` (${(ms / 1000).toFixed(2)}s)`;
  };

  // Number line: spans from min(lo,hi,0) to max(lo,hi,0) with padding
  const a = Math.min(lo, hi), b = Math.max(lo, hi);
  const lineMin = Math.min(a, allowNegative ? a : 0, 0);
  const lineMax = Math.max(b, 0);
  const lineSpan = lineMax - lineMin || 1;

  // Positions as % of the line
  const loPos   = ((a - lineMin) / lineSpan) * 100;
  const hiPos   = ((b - lineMin) / lineSpan) * 100;
  const zeroPos = ((0 - lineMin) / lineSpan) * 100;
  const fillW   = hiPos - loPos;

  return (
    <div>
      {(label || hint) && (
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 6 }}>
          {label && <span style={{ fontSize: 10, fontWeight: 600, color }}>{label}</span>}
          {hint && <span style={{ fontSize: 9, color: '#334155' }}>{hint}</span>}
        </div>
      )}
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        {/* From input */}
        <div style={{ textAlign: 'center' }}>
          <div style={{ fontSize: 9, color: '#475569', marginBottom: 3 }}>From</div>
          <input type="number" value={lo}
            onChange={e => onLoChange(allowNegative ? Number(e.target.value) : Math.max(0, Number(e.target.value)))}
            style={{ ...SI, width: 72, textAlign: 'center' }} />
          <div style={{ fontSize: 8, color: '#334155', marginTop: 2, fontFamily: 'monospace' }}>{lo}{unit}{toMs(lo)}</div>
        </div>

        {/* Number line */}
        <div style={{ flex: 1, minWidth: 90 }}>
          <div style={{ position: 'relative', height: 24 }}>
            {/* Track */}
            <div style={{ position: 'absolute', top: '50%', left: 0, right: 0, height: 4, background: '#1a2540', borderRadius: 2, transform: 'translateY(-50%)' }} />
            {/* Filled range */}
            <div style={{
              position: 'absolute', top: '50%', height: 4,
              left: `${Math.max(0, loPos)}%`,
              width: `${Math.min(100 - Math.max(0, loPos), Math.max(0, fillW))}%`,
              background: color + 'cc', borderRadius: 2, transform: 'translateY(-50%)',
            }} />
            {/* Zero marker */}
            {zeroPos >= 0 && zeroPos <= 100 && (
              <div style={{ position: 'absolute', top: '15%', height: '70%', width: 2, background: '#475569', left: `${zeroPos}%`, transform: 'translateX(-50%)' }} />
            )}
            {/* Lo dot */}
            <div style={{ position: 'absolute', top: '50%', left: `${loPos}%`, width: 9, height: 9, background: color, borderRadius: '50%', transform: 'translate(-50%, -50%)', border: '2px solid #0a1628', zIndex: 1 }} />
            {/* Hi dot */}
            <div style={{ position: 'absolute', top: '50%', left: `${hiPos}%`, width: 9, height: 9, background: color, borderRadius: '50%', transform: 'translate(-50%, -50%)', border: '2px solid #0a1628', zIndex: 1 }} />
          </div>
          {/* Axis labels */}
          <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 8, color: '#334155', marginTop: 1 }}>
            <span>{lineMin}{unit}</span>
            <span style={{ color, fontWeight: 700 }}>{lo} → {hi}</span>
            <span>{lineMax}{unit}</span>
          </div>
        </div>

        {/* To input */}
        <div style={{ textAlign: 'center' }}>
          <div style={{ fontSize: 9, color: '#475569', marginBottom: 3 }}>To</div>
          <input type="number" value={hi}
            onChange={e => onHiChange(allowNegative ? Number(e.target.value) : Math.max(0, Number(e.target.value)))}
            style={{ ...SI, width: 72, textAlign: 'center' }} />
          <div style={{ fontSize: 8, color: '#334155', marginTop: 2, fontFamily: 'monospace' }}>{hi}{unit}{toMs(hi)}</div>
        </div>
      </div>
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
  const [removedBatchIds, setRemovedBatchIds] = useState(new Set());
  const batchInitialized = useRef(false);
  const batchSamples = useMemo(() => (
    isBatch ? samples.filter(s => !removedBatchIds.has(s.id)) : []
  ), [isBatch, samples, removedBatchIds]);

  useEffect(() => {
    const onKey = e => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  // ── State (initialised from lastAlgoStore) ─────────────────────────────
  const [mode, setMode]         = useState(lastAlgoStore.mode || 'auto');
  const [cuts, setCuts]         = useState([]);
  const [removedSegs, setRemovedSegs] = useState(new Set());
  const [partLabels, setPartLabels]   = useState({});
  const [batchStates, setBatchStates] = useState({}); // { [id]: { cuts, removedSegs } }

  // Sensor visibility
  const [visSensors, setVisSensors] = useState(() => new Set(sensors.slice(0, 6)));
  // Ref sensors (multi) — auto-pick by default
  const [refSensors, setRefSensors] = useState(() => new Set(pickBestChannels(values, sensors, 3)));

  // Auto params — restored from lastAlgoStore
  const [algo, setAlgo]             = useState(lastAlgoStore.algo);
  const [windowSize, setWindowSize] = useState(lastAlgoStore.windowSize);
  const [windowIncreaseStride, setWindowIncreaseStride] = useState(lastAlgoStore.windowIncreaseStride ?? 10);
  const [threshold, setThreshold]   = useState(lastAlgoStore.threshold);
  const [stdMult, setStdMult]       = useState(lastAlgoStore.stdMult);
  const [sensitivity, setSensitivity] = useState(lastAlgoStore.sensitivity);
  const [minGap, setMinGap]         = useState(lastAlgoStore.minGap ?? Math.max(20, Math.floor(N * 0.05)));
  const [numParts, setNumParts]     = useState(lastAlgoStore.numParts);
  const [equalParts, setEqualParts] = useState(lastAlgoStore.equalParts || 4);

  // Random shift — min and max are each a [lo, hi] range
  const [enableShift, setEnableShift] = useState(lastAlgoStore.shiftEnabled);
  const [shiftLo, setShiftLo] = useState(lastAlgoStore.shiftLo ?? -10);
  const [shiftHi, setShiftHi] = useState(lastAlgoStore.shiftHi ?? 10);
  const [shiftUnit, setShiftUnit]     = useState(lastAlgoStore.shiftUnit);
  const [shiftPreview, setShiftPreview] = useState([]);

  // Padding — min and max are each a [lo, hi] range
  const [enablePad, setEnablePad]   = useState(lastAlgoStore.padEnabled);
  const [padLo, setPadLo] = useState(lastAlgoStore.padLo ?? 3);
  const [padHi, setPadHi] = useState(lastAlgoStore.padHi ?? 10);
  const [padUnit, setPadUnit]       = useState(lastAlgoStore.padUnit);
  const [padRandom, setPadRandom]   = useState(lastAlgoStore.padRandom);

  // Target duration — crops each kept segment to a random duration from [durLo, durHi]
  // Alignment controls where the crop window anchors within the original segment.
  const [enableDur, setEnableDur]   = useState(lastAlgoStore.durEnabled ?? false);
  const [durLo, setDurLo]           = useState(lastAlgoStore.durLo ?? 50);
  const [durHi, setDurHi]           = useState(lastAlgoStore.durHi ?? 100);
  const [durUnit, setDurUnit]       = useState(lastAlgoStore.durUnit ?? 'pts');
  const [durAlign, setDurAlign]     = useState(lastAlgoStore.durAlign ?? 'center'); // 'start'|'center'|'end'|'random'
  const [autoDisableFlat, setAutoDisableFlat] = useState(Boolean(lastAlgoStore.flatAutoDisable));

  const getFlatRemovedSegs = useCallback((cutList, flatRegions, totalLen) => {
    if (!Array.isArray(flatRegions) || !flatRegions.length) return new Set();
    const bounds = [0, ...(cutList || []), totalLen].sort((a, b) => a - b);
    const removed = new Set();
    for (let i = 0; i < bounds.length - 1; i++) {
      const s1 = bounds[i];
      const e1 = bounds[i + 1];
      const segLen = Math.max(1, e1 - s1);
      let maxOverlap = 0;
      flatRegions.forEach(r => {
        const rs = Math.max(0, Number(r.start) || 0);
        const re = Math.min(totalLen, Number(r.end) || 0);
        const ov = Math.max(0, Math.min(e1, re) - Math.max(s1, rs));
        if (ov > maxOverlap) maxOverlap = ov;
      });
      if (maxOverlap / segLen >= 0.6) removed.add(i);
    }
    return removed;
  }, []);

  const sameNumArray = useCallback((a = [], b = []) => {
    if (a === b) return true;
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }, []);

  const handleFlatCutsFound = useCallback((newCuts, flatResult = null) => {
    const normalized = [...(newCuts || [])]
      .map(v => Math.round(Number(v) || 0))
      .filter(v => v > 0 && v < N)
      .sort((a, b) => a - b);

    const autoRemoved = (autoDisableFlat && flatResult?.flatRegions?.length)
      ? getFlatRemovedSegs(normalized, flatResult.flatRegions, N)
      : new Set();

    setCuts(prev => {
      if (sameNumArray(prev, normalized)) return prev;
      setRemovedSegs(autoRemoved);
      return normalized;
    });
  }, [N, sameNumArray, autoDisableFlat, getFlatRemovedSegs]);

  // Persist to store when algo params change
  useEffect(() => {
    Object.assign(lastAlgoStore, {
      mode, algo, windowSize, windowIncreaseStride, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts,
      shiftEnabled: enableShift, shiftLo, shiftHi, shiftUnit,
      padEnabled: enablePad, padLo, padHi, padUnit, padRandom,
      durEnabled: enableDur, durLo, durHi, durUnit, durAlign,
      flatAutoDisable: autoDisableFlat,
    });
  }, [mode, algo, windowSize, windowIncreaseStride, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts, enableShift, shiftLo, shiftHi, shiftUnit,
      enablePad, padLo, padHi, padUnit, padRandom,
      enableDur, durLo, durHi, durUnit, durAlign, autoDisableFlat]);

  // ── Derived ────────────────────────────────────────────────────────────
  const allCuts    = useMemo(() => [0, ...cuts, N].sort((a, b) => a - b), [cuts, N]);
  const segments   = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);
  const keptSegs   = useMemo(() => segments.filter(s => !removedSegs.has(s.idx)), [segments, removedSegs]);
  const estimatedWindowsPerSample = useMemo(() => {
    const w = Math.max(1, Math.round(Number(windowSize) || 1));
    const s = Math.max(1, Math.round(Number(windowIncreaseStride) || 1));
    if (!N || N < w) return 0;
    return Math.floor((N - w) / s) + 1;
  }, [N, windowSize, windowIncreaseStride]);
  const estimatedWindowsBatchTotal = useMemo(() => {
    if (!isBatch || !batchSamples.length) return estimatedWindowsPerSample;
    const w = Math.max(1, Math.round(Number(windowSize) || 1));
    const s = Math.max(1, Math.round(Number(windowIncreaseStride) || 1));
    return batchSamples.reduce((acc, smpl) => {
      const n = smpl?.values?.length || 0;
      if (n < w) return acc;
      return acc + (Math.floor((n - w) / s) + 1);
    }, 0);
  }, [isBatch, batchSamples, estimatedWindowsPerSample, windowSize, windowIncreaseStride]);

  const toggleRemove = idx => setRemovedSegs(p => { const n = new Set(p); n.has(idx) ? n.delete(idx) : n.add(idx); return n; });

  const handleCombineLeft = idx => {
    if (idx > 0 && idx <= cuts.length) setCuts(p => p.filter((_, i) => i !== idx - 1));
  };
  const handleCombineRight = idx => {
    if (idx >= 0 && idx < cuts.length) setCuts(p => p.filter((_, i) => i !== idx));
  };
  const handleSplitSegment = idx => {
    setCuts(prev => {
      const bounds = [0, ...prev, N].sort((a, b) => a - b);
      if (idx < 0 || idx >= bounds.length - 1) return prev;
      const start = bounds[idx];
      const end = bounds[idx + 1];
      const mid = Math.round((start + end) / 2);
      if (mid <= start || mid >= end || prev.includes(mid)) return prev;
      return [...prev, mid].sort((a, b) => a - b);
    });
  };

  // Generate shift preview — each cut is shifted by a value sampled uniformly from [shiftLo, shiftHi]
  const generateShiftPreview = useCallback(() => {
    if (!enableShift || !cuts.length) { setShiftPreview([]); return; }
    const topts = v => shiftUnit === 'ms' ? Math.round(v / interval_ms) : Math.round(v);
    const lo = topts(Math.min(shiftLo, shiftHi));
    const hi = topts(Math.max(shiftLo, shiftHi));
    const preview = cuts.map(c => {
      const shift = Math.round(lo + Math.random() * (hi - lo));
      return Math.max(1, Math.min(N - 1, c + shift));
    }).sort((a, b) => a - b);
    setShiftPreview(preview);
  }, [enableShift, cuts, shiftLo, shiftHi, shiftUnit, interval_ms, N]);

  useEffect(() => { generateShiftPreview(); }, [generateShiftPreview]);

  // ── Recalculate ────────────────────────────────────────────────────────
  const getCfg = useCallback(() => ({
    algorithm: algo, refSensors: [...refSensors],
    windowSize, threshold, stdMult, sensitivity, minGap, numParts,
  }), [algo, refSensors, windowSize, threshold, stdMult, sensitivity, minGap, numParts]);

  const recalc = useCallback(() => {
    if (isBatch) {
      if (mode === 'flat') {
        const selectedFlatIds = new Set(lastAlgoStore.flatSelectedIds || []);
        const selectedCh = (lastAlgoStore.flatSelectedCh || []).filter(ch => sensors.includes(ch));
        const flatRefs = allSamples.filter(s => selectedFlatIds.has(s.id) && s.values?.length > 0);
        if (!selectedCh.length || !flatRefs.length) {
          const emptyStates = {};
          batchSamples.forEach(smpl => { emptyStates[smpl.id] = { cuts: [], removedSegs: new Set() }; });
          setBatchStates(emptyStates);
          return;
        }

        const cfg = {
          windowSize: lastAlgoStore.flatWindowSize,
          threshold: lastAlgoStore.flatThreshold,
          minFlatPts: lastAlgoStore.flatMinFlatPts,
        };

        const states = {};
        batchSamples.forEach(smpl => {
          let refsForSample = flatRefs.filter(r => r.id !== smpl.id).map(r => r.values);
          if (!refsForSample.length) refsForSample = flatRefs.map(r => r.values);
          const res = runFlatDetector(smpl.values, sensors, refsForSample, selectedCh, cfg);
          const cuts = res.cuts || [];
          const removed = autoDisableFlat ? getFlatRemovedSegs(cuts, res.flatRegions || [], smpl.values.length) : new Set();
          states[smpl.id] = { cuts, removedSegs: removed };
        });
        setBatchStates(states);
      } else {
        const bRes = batchAutoSplit(batchSamples, sensors, getCfg());
        const states = {};
        bRes.forEach(r => {
          let sampleCuts = r.cuts;
          if (mode === 'equal') {
              const sz = Math.floor(r.sample.values.length / equalParts);
              sampleCuts = Array.from({ length: equalParts - 1 }, (_, i) => (i + 1) * sz);
          }
          states[r.sample.id] = { cuts: sampleCuts, removedSegs: new Set() };
        });
        setBatchStates(states);
      }
    } else {
      if (mode === 'auto') {
        const newCuts = runAutoDetect(values, sensors, getCfg());
        setCuts(newCuts); setRemovedSegs(new Set());
      } else if (mode === 'equal') {
        const sz = Math.floor(N / equalParts);
        setCuts(Array.from({ length: equalParts - 1 }, (_, i) => (i + 1) * sz));
        setRemovedSegs(new Set());
      }
    }
  }, [mode, getCfg, values, sensors, N, equalParts, isBatch, batchSamples, allSamples, autoDisableFlat, getFlatRemovedSegs]);

  useEffect(() => { if (mode !== 'manual') recalc(); }, [mode, recalc]);

  // Ensure batchStates is initialized when batch samples change
  useEffect(() => {
    if (isBatch && batchSamples.length > 0 && !batchInitialized.current) {
      batchInitialized.current = true;
      recalc();
    } else if (!isBatch) {
      batchInitialized.current = false;
    }
  }, [isBatch, batchSamples.length, recalc]);

  // ── Execute split ──────────────────────────────────────────────────────
  // Helper: crop a [start,end] segment to a random target duration from [durLo,durHi].
  const applyDuration = (start, end, totalLen, imsRate) => {
    if (!enableDur) return { start, end };
    const toPts = v => durUnit === 'ms' ? Math.round(v / imsRate) : Math.round(v);
    const lo = toPts(Math.min(durLo, durHi));
    const hi = toPts(Math.max(durLo, durHi));
    const target = lo + Math.round(Math.random() * (hi - lo));
    const segLen = end - start;
    if (target >= segLen) return { start, end }; // segment shorter than target → keep as-is
    // Anchor the crop window
    let cropStart;
    if (durAlign === 'start')  cropStart = start;
    else if (durAlign === 'end') cropStart = end - target;
    else if (durAlign === 'random') cropStart = start + Math.round(Math.random() * (segLen - target));
    else /* center */ cropStart = start + Math.round((segLen - target) / 2);
    cropStart = Math.max(0, Math.min(totalLen - target, cropStart));
    return { start: cropStart, end: Math.min(totalLen, cropStart + target) };
  };

  const doSplit = () => {
    if (isBatch) {
      if (!Object.keys(batchStates).length) return;
      const finalBatch = [];
      batchSamples.forEach(smpl => {
        const state = batchStates[smpl.id];
        if (!state) return;
        const currentCuts = state.cuts || [];
        const sr = state.removedSegs || new Set();
        
        // shift — sample each cut's shift uniformly from [shiftLo, shiftHi]
        let finalCuts = [...currentCuts];
        if (enableShift) {
          const topts = v => shiftUnit === 'ms' ? Math.round(v / smpl.interval_ms) : Math.round(v);
          const lo = topts(Math.min(shiftLo, shiftHi));
          const hi = topts(Math.max(shiftLo, shiftHi));
          finalCuts = currentCuts.map(c => {
            const shift = Math.round(lo + Math.random() * (hi - lo));
            return Math.max(1, Math.min(smpl.values.length - 1, c + shift));
          }).sort((a, b) => a - b);
        }

        const fc = [0, ...finalCuts, smpl.values.length].sort((a, b) => a - b);
        const allPartRecords = fc.slice(0, -1).map((s, i) => {
          let start = s, end = fc[i + 1];
          if (enablePad && !sr.has(i)) {
            const top = v => padUnit === 'ms' ? v / smpl.interval_ms : v;
            const padLoP = top(Math.min(padLo, padHi));
            const padHiP = top(Math.max(padLo, padHi));
            const actualPad = padRandom ? Math.round(padLoP + Math.random() * (padHiP - padLoP)) : Math.round(padLoP);
            start = Math.max(0, start - actualPad);
            end   = Math.min(smpl.values.length, end + actualPad);
          }
          return { idx: i, start, end, values: smpl.values.slice(start, end) };
        });
        const keptRecords = allPartRecords.filter(r => !sr.has(r.idx)).map(r => {
          const { start: cs, end: ce } = applyDuration(r.start, r.end, smpl.values.length, smpl.interval_ms);
          return { ...r, start: cs, end: ce, values: smpl.values.slice(cs, ce) };
        });
        const finalParts = keptRecords.map(r => r.values);
        finalBatch.push({
          sample: smpl,
          parts: finalParts,
          meta: {
            cuts: finalCuts,
            removed: [...sr],
            segments: keptRecords.map((r, i) => ({
              partIndex: i,
              sourceSegIndex: r.idx,
              start: r.start,
              end: r.end,
              length: r.end - r.start,
            })),
          },
        });
      });
      onBatchSplit(finalBatch, {});
    }
    else {
      // Apply random shift — sample each cut's shift uniformly from [shiftLo, shiftHi]
      let finalCuts = [...cuts];
      if (enableShift) {
        const topts = v => shiftUnit === 'ms' ? Math.round(v / interval_ms) : Math.round(v);
        const lo = topts(Math.min(shiftLo, shiftHi));
        const hi = topts(Math.max(shiftLo, shiftHi));
        finalCuts = cuts.map(c => {
          const shift = Math.round(lo + Math.random() * (hi - lo));
          return Math.max(1, Math.min(N - 1, c + shift));
        }).sort((a, b) => a - b);
      }

      // Build parts from finalCuts, excluding removed
      const fc = [0, ...finalCuts, N].sort((a, b) => a - b);
      const allPartRecords = fc.slice(0, -1).map((s, i) => {
        let start = s, end = fc[i + 1];
        if (enablePad && !removedSegs.has(i)) {
          const top = v => padUnit === 'ms' ? v / interval_ms : v;
          const padLoP = top(Math.min(padLo, padHi));
          const padHiP = top(Math.max(padLo, padHi));
          const actualPad = padRandom ? Math.round(padLoP + Math.random() * (padHiP - padLoP)) : Math.round(padLoP);
          start = Math.max(0, start - actualPad);
          end   = Math.min(N, end + actualPad);
        }
        return { idx: i, start, end, values: values.slice(start, end) };
      });
      const keptRecords = allPartRecords.filter(r => !removedSegs.has(r.idx)).map(r => {
        const { start: cs, end: ce } = applyDuration(r.start, r.end, N, interval_ms);
        return { ...r, start: cs, end: ce, values: values.slice(cs, ce) };
      });
      const finalParts = keptRecords.map(r => r.values);
      onSplit(sample, finalParts, partLabels, {
        cuts: finalCuts,
        removed: [...removedSegs],
        segments: keptRecords.map((r, i) => ({
          partIndex: i,
          sourceSegIndex: r.idx,
          start: r.start,
          end: r.end,
          length: r.end - r.start,
        })),
      });
    }
    onClose();
  };

  const SI = { background: '#060d1a', color: '#f1f5f9', border: '1px solid #1e293b', borderRadius: 5, padding: '5px 9px', fontSize: 11, boxSizing: 'border-box', fontFamily: 'inherit' };
  const totalKept = isBatch
    ? batchSamples.reduce((a, smpl) => {
        const st = batchStates[smpl.id] || { cuts: [], removedSegs: new Set() };
        return a + (st.cuts.length + 1 - (st.removedSegs?.size || 0));
      }, 0)
    : keptSegs.length;

  const handleBatchToggleRemove = (smplId, idx) => {
    setBatchStates(p => {
      const state = p[smplId];
      if (!state) return p;
      const n = new Set(state.removedSegs);
      n.has(idx) ? n.delete(idx) : n.add(idx);
      return { ...p, [smplId]: { ...state, removedSegs: n } };
    });
  };

  const handleBatchCombineLeft = (smplId, idx) => {
    setBatchStates(p => {
      const state = p[smplId];
      if (!state || idx <= 0 || idx > state.cuts.length) return p;
      return { ...p, [smplId]: { ...state, cuts: state.cuts.filter((_, i) => i !== idx - 1) } };
    });
  };

  const handleBatchCombineRight = (smplId, idx) => {
    setBatchStates(p => {
      const state = p[smplId];
      if (!state || idx < 0 || idx >= state.cuts.length) return p;
      return { ...p, [smplId]: { ...state, cuts: state.cuts.filter((_, i) => i !== idx) } };
    });
  };

  const handleBatchSplitSegment = (smplId, idx) => {
    setBatchStates(p => {
      const state = p[smplId];
      const smpl = batchSamples.find(s => s.id === smplId);
      if (!state || !smpl) return p;
      const bounds = [0, ...(state.cuts || []), smpl.values.length].sort((a, b) => a - b);
      if (idx < 0 || idx >= bounds.length - 1) return p;
      const start = bounds[idx];
      const end = bounds[idx + 1];
      const mid = Math.round((start + end) / 2);
      if (mid <= start || mid >= end || state.cuts.includes(mid)) return p;
      return {
        ...p,
        [smplId]: { ...state, cuts: [...state.cuts, mid].sort((a, b) => a - b) },
      };
    });
  };

  const handleBatchRemoveSample = (smplId) => {
    setRemovedBatchIds(p => {
      const n = new Set(p);
      n.add(smplId);
      return n;
    });
    setBatchStates(p => {
      const n = { ...p };
      delete n[smplId];
      return n;
    });
  };

  // Direct drag edit of cuts in batch preview
  const handleBatchCutsChange = (smplId, newCuts) => {
    setBatchStates(p => {
      const state = p[smplId];
      if (!state) return p;
      return { ...p, [smplId]: { ...state, cuts: newCuts } };
    });
  };

  // Cuts to show in single-sample preview — shifted if enabled
  const previewCuts = enableShift && shiftPreview.length ? shiftPreview : cuts;

  // For batch preview: apply simulated shift using sampled min/max ranges
  const batchPreviewStates = useMemo(() => {
    if (!isBatch || !enableShift) return batchStates;
    const itvl = primarySample.interval_ms || 33.33;
    const topts = v => shiftUnit === 'ms' ? Math.round(v / itvl) : Math.round(v);
    const shifted = {};
    Object.entries(batchStates).forEach(([id, state]) => {
      const smpl = batchSamples.find(s => s.id === id);
      if (!smpl || !state.cuts.length) { shifted[id] = state; return; }
      const lo = topts(Math.min(shiftLo, shiftHi));
      const hi = topts(Math.max(shiftLo, shiftHi));
      const shiftedCuts = state.cuts.map(c => {
        const s = Math.round(lo + Math.random() * (hi - lo));
        return Math.max(1, Math.min(smpl.values.length - 1, c + s));
      }).sort((a, b) => a - b);
      shifted[id] = { ...state, cuts: shiftedCuts };
    });
    return shifted;
  }, [isBatch, enableShift, batchStates, batchSamples,
      shiftLo, shiftHi, shiftUnit, primarySample.interval_ms]);

  const padInfo = enablePad ? {
    min: padLo,   // lo = guaranteed minimum pad
    max: padHi,   // hi = maximum possible pad (random upper bound)
    unit: padUnit,
    random: padRandom,
  } : null;

  return (
    <div 
      style={{ position: 'fixed', inset: 0, background: '#000d', zIndex: 200, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 12, backdropFilter: 'blur(4px)' }}
      onClick={onClose}
    >
      <div 
        style={{ background: '#0a1628', border: '1px solid #1e3a5f', borderRadius: 14, width: 'min(1080px, 100%)', maxHeight: '96vh', overflowY: 'auto', boxShadow: '0 32px 80px #0008' }}
        onClick={e => e.stopPropagation()}
      >

        {/* Header */}
        <div style={{ padding: '13px 18px', borderBottom: '1px solid #1e293b', display: 'flex', alignItems: 'center', gap: 10, position: 'sticky', top: 0, background: '#0a1628', zIndex: 1 }}>
          <Scissors size={16} color="#60a5fa" strokeWidth={2} />
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 14, fontWeight: 700, color: '#f1f5f9' }}>
              {isBatch ? `Batch Split — ${batchSamples.length} samples` : 'Smart Split'}
            </div>
            <div style={{ fontSize: 10, color: '#475569' }}>
              {isBatch ? `Preview: ${label}` : label} · {N} pts · {formatMs(N * interval_ms)}
            </div>
          </div>
          <button onClick={onClose} style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'none', border: 'none', color: '#475569', width: 28, height: 28, cursor: 'pointer', borderRadius: 4 }}
            onMouseEnter={e => e.currentTarget.style.color = '#f1f5f9'}
            onMouseLeave={e => e.currentTarget.style.color = '#475569'}>
            <X size={16} strokeWidth={2} />
          </button>
        </div>

        <div style={{ padding: 16 }}>
          {/* Mode tabs */}
          <div style={{ display: 'flex', gap: 3, background: '#050c1a', borderRadius: 8, padding: 4, marginBottom: 14 }}>
            {[['auto', 'Auto-Detect'], ['equal', 'Equal Parts'], ['manual', 'Manual'], ['flat', 'Predicted Flat']].map(([m, lbl]) => (
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
                <Sldr label="Window Stride" value={windowIncreaseStride} min={1} max={Math.max(1, windowSize)} onChange={setWindowIncreaseStride} fmt={v => `${v}pts`} />
                {algo === 'energy' && <Sldr label="Threshold" value={Math.round(threshold * 100)} min={5} max={80} onChange={v => setThreshold(v / 100)} fmt={v => `${v}%`} />}
                {algo === 'threshold' && <Sldr label="Std ×" value={Math.round(stdMult * 10)} min={5} max={50} onChange={v => setStdMult(v / 10)} fmt={v => `${(v / 10).toFixed(1)}σ`} />}
                {(algo === 'derivative' || algo === 'variance') && <Sldr label="Sensitivity" value={Math.round(sensitivity * 100)} min={5} max={90} onChange={v => setSensitivity(v / 100)} fmt={v => `${v}%`} />}
                {algo === 'equal' && <Sldr label="Parts" value={numParts} min={2} max={20} onChange={setNumParts} />}
                {algo !== 'equal' && <Sldr label="Min Gap" value={minGap} min={5} max={Math.floor(N / 2)} onChange={setMinGap} fmt={v => `${v}pts`} />}
              </div>
              <div style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 7, padding: '8px 10px', marginBottom: 10, fontSize: 10, color: '#94a3b8', lineHeight: 1.5 }}>
                Estimated windows: <b style={{ color: '#34d399' }}>{isBatch ? estimatedWindowsBatchTotal : estimatedWindowsPerSample}</b>
                {isBatch
                  ? <span style={{ color: '#64748b' }}> total windows across {batchSamples.length} selected sample{batchSamples.length !== 1 ? 's' : ''}</span>
                  : <span style={{ color: '#64748b' }}> windows for this sample</span>}
                <div style={{ color: '#475569', marginTop: 2 }}>
                  Using $\text{'{samples}'} = \left\lfloor\frac{'{N - W}'}{'{S}'}\right\rfloor + 1$ where window size is W and stride is S.
                </div>
              </div>
              <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                <button onClick={recalc} style={{ display: 'flex', alignItems: 'center', gap: 6, background: '#1d4ed8', border: 'none', color: '#fff', borderRadius: 6, padding: '7px 18px', cursor: 'pointer', fontSize: 11, fontWeight: 700, fontFamily: 'inherit' }}>
                  <RefreshCw size={12} /> Recalculate
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
          {mode === 'flat' && (
            <>
              <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 10, marginBottom: 10 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none', fontSize: 10, color: autoDisableFlat ? '#34d399' : '#64748b' }}>
                  <input
                    type="checkbox"
                    checked={autoDisableFlat}
                    onChange={e => {
                      const checked = e.target.checked;
                      setAutoDisableFlat(checked);
                      if (!checked) {
                        if (isBatch) {
                          setBatchStates(p => {
                            const next = {};
                            Object.entries(p).forEach(([id, st]) => {
                              next[id] = { ...st, removedSegs: new Set() };
                            });
                            return next;
                          });
                        } else {
                          setRemovedSegs(new Set());
                        }
                      }
                    }}
                    style={{ accentColor: '#34d399' }}
                  />
                  Auto-disable detected flat segments
                </label>
                <div style={{ fontSize: 9, color: '#475569', marginTop: 4, paddingLeft: 20 }}>
                  When enabled, segments overlapping detected flat regions are auto-excluded (you can still re-enable manually).
                </div>
              </div>
              <FlatDetectorPanel
                targetSample={primarySample}
                allSamples={allSamples}
                sensors={sensors}
                onCutsFound={(newCuts, flatResult) => {
                  handleFlatCutsFound(newCuts, flatResult);
                  if (isBatch) recalc();
                }}
              />
            </>
          )}

          {/* ── Random Shift ── */}
          <div style={{ background: '#050c1a', border: `1px solid ${enableShift ? '#f59e0b55' : '#1e293b'}`, borderRadius: 10, padding: 12, marginBottom: 10 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: enableShift ? 14 : 0 }}>
              <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none' }}>
                <input type="checkbox" checked={enableShift} onChange={e => setEnableShift(e.target.checked)} style={{ accentColor: '#f59e0b', width: 13, height: 13 }} />
                <span style={{ fontSize: 11, fontWeight: 700, color: enableShift ? '#fbbf24' : '#64748b' }}>Random Shift</span>
              </label>
              {enableShift && <span style={{ fontSize: 9, color: '#64748b' }}>Each cut boundary shifts by a random amount from this range</span>}
              {enableShift && (
                <button onClick={generateShiftPreview} style={{ display: 'flex', alignItems: 'center', gap: 5, background: '#451a03', border: '1px solid #f59e0b55', color: '#fbbf24', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit', marginLeft: 'auto' }}>
                  <Shuffle size={10} /> Resample preview
                </button>
              )}
            </div>
            {enableShift && (
              <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 10, color: '#64748b', minWidth: 32 }}>Unit</span>
                  <div style={{ display: 'flex', gap: 3 }}>
                    {['pts', 'ms'].map(u => (
                      <button key={u} onClick={() => setShiftUnit(u)} style={{
                        background: shiftUnit === u ? '#451a03' : '#060d1a',
                        border: `1px solid ${shiftUnit === u ? '#f59e0b' : '#1e293b'}`,
                        color: shiftUnit === u ? '#fbbf24' : '#64748b',
                        borderRadius: 4, padding: '3px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                      }}>{u}</button>
                    ))}
                  </div>
                </div>
                <RangePair
                  lo={shiftLo} hi={shiftHi}
                  onLoChange={setShiftLo} onHiChange={setShiftHi}
                  unit={shiftUnit} interval_ms={interval_ms}
                  color="#f59e0b" allowNegative
                  SI={SI}
                />
                <div style={{ fontSize: 9, color: '#64748b', background: '#451a0318', border: '1px solid #f59e0b22', borderRadius: 5, padding: '5px 10px' }}>
                  Per cut: shift &isin; [{shiftLo}, {shiftHi}] {shiftUnit}
                  {shiftUnit === 'pts' && <> &nbsp;&middot;&nbsp; {formatMs(Math.abs(shiftLo) * interval_ms)} → {formatMs(Math.abs(shiftHi) * interval_ms)}</>}
                </div>
              </div>
            )}
          </div>

          {/* ── Padding ── */}
          <div style={{ background: '#050c1a', border: `1px solid ${enablePad ? '#a78bfa55' : '#1e293b'}`, borderRadius: 10, padding: 12, marginBottom: 12 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: enablePad ? 14 : 0 }}>
              <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none' }}>
                <input type="checkbox" checked={enablePad} onChange={e => setEnablePad(e.target.checked)} style={{ accentColor: '#a78bfa', width: 13, height: 13 }} />
                <span style={{ fontSize: 11, fontWeight: 700, color: enablePad ? '#a78bfa' : '#64748b' }}>Padding</span>
              </label>
              {enablePad && <span style={{ fontSize: 9, color: '#64748b' }}>Extends each kept segment at both ends &middot; amber zones in preview</span>}
              {enablePad && (
                <label style={{ display: 'flex', alignItems: 'center', gap: 5, cursor: 'pointer', fontSize: 10, color: padRandom ? '#a78bfa' : '#64748b', userSelect: 'none', marginLeft: 'auto' }}>
                  <input type="checkbox" checked={padRandom} onChange={e => setPadRandom(e.target.checked)} style={{ accentColor: '#a78bfa' }} />
                  Random in range
                </label>
              )}
            </div>
            {enablePad && (
              <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 10, color: '#64748b', minWidth: 32 }}>Unit</span>
                  <div style={{ display: 'flex', gap: 3 }}>
                    {['pts', 'ms'].map(u => (
                      <button key={u} onClick={() => setPadUnit(u)} style={{
                        background: padUnit === u ? '#1a0a33' : '#060d1a',
                        border: `1px solid ${padUnit === u ? '#a78bfa' : '#1e293b'}`,
                        color: padUnit === u ? '#a78bfa' : '#64748b',
                        borderRadius: 4, padding: '3px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                      }}>{u}</button>
                    ))}
                  </div>
                </div>
                <RangePair
                  lo={padLo} hi={padHi}
                  onLoChange={setPadLo} onHiChange={setPadHi}
                  unit={padUnit} interval_ms={interval_ms}
                  color="#a78bfa"
                  SI={SI}
                />
                <div style={{ fontSize: 9, color: '#64748b', background: '#1a0a3318', border: '1px solid #a78bfa22', borderRadius: 5, padding: '5px 10px' }}>
                  {padRandom
                    ? <>Per segment: pad &isin; [{padLo}, {padHi}] {padUnit} (random){padUnit === 'pts' ? <> &nbsp;&middot;&nbsp; {formatMs(padLo * interval_ms)} → {formatMs(padHi * interval_ms)}</> : null}</>
                    : <>Fixed pad: {padLo} {padUnit}{padUnit === 'pts' ? ` · ${formatMs(padLo * interval_ms)}` : ''} added to each end</>}
                </div>
              </div>
            )}
          </div>

          {/* ── Target Duration ── */}
          <div style={{ background: '#050c1a', border: `1px solid ${enableDur ? '#38bdf855' : '#1e293b'}`, borderRadius: 10, padding: 12, marginBottom: 12 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: enableDur ? 14 : 0 }}>
              <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', userSelect: 'none' }}>
                <input type="checkbox" checked={enableDur} onChange={e => setEnableDur(e.target.checked)} style={{ accentColor: '#38bdf8', width: 13, height: 13 }} />
                <span style={{ fontSize: 11, fontWeight: 700, color: enableDur ? '#38bdf8' : '#64748b' }}>Target Duration</span>
              </label>
              {enableDur && <span style={{ fontSize: 9, color: '#64748b' }}>Crops each kept segment to a random length from this range</span>}
            </div>
            {enableDur && (
              <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                {/* Unit toggle */}
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 10, color: '#64748b', minWidth: 32 }}>Unit</span>
                  <div style={{ display: 'flex', gap: 3 }}>
                    {['pts', 'ms'].map(u => (
                      <button key={u} onClick={() => setDurUnit(u)} style={{
                        background: durUnit === u ? '#0d2040' : '#060d1a',
                        border: `1px solid ${durUnit === u ? '#38bdf8' : '#1e293b'}`,
                        color: durUnit === u ? '#38bdf8' : '#64748b',
                        borderRadius: 4, padding: '3px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                      }}>{u}</button>
                    ))}
                  </div>
                </div>
                {/* Duration range */}
                <RangePair
                  lo={durLo} hi={durHi}
                  onLoChange={setDurLo} onHiChange={setDurHi}
                  unit={durUnit} interval_ms={interval_ms}
                  color="#38bdf8"
                  SI={SI}
                />
                {/* Alignment */}
                <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                  <span style={{ fontSize: 10, color: '#64748b', minWidth: 52 }}>Anchor at</span>
                  {[['start', 'Start'], ['center', 'Center'], ['end', 'End'], ['random', 'Random']].map(([v, l]) => (
                    <button key={v} onClick={() => setDurAlign(v)} style={{
                      background: durAlign === v ? '#0d2040' : '#060d1a',
                      border: `1px solid ${durAlign === v ? '#38bdf8' : '#1e293b'}`,
                      color: durAlign === v ? '#38bdf8' : '#64748b',
                      borderRadius: 4, padding: '3px 9px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                      fontWeight: durAlign === v ? 700 : 400,
                    }}>{l}</button>
                  ))}
                  <span style={{ fontSize: 9, color: '#334155' }}>
                    {durAlign === 'start'  ? '— crop from beginning'   :
                     durAlign === 'end'    ? '— crop from end'          :
                     durAlign === 'center' ? '— centered on segment'   :
                     '— random offset within segment'}
                  </span>
                </div>
                <div style={{ fontSize: 9, color: '#64748b', background: '#0d204018', border: '1px solid #38bdf822', borderRadius: 5, padding: '5px 10px' }}>
                  Per segment: duration &isin; [{durLo}, {durHi}] {durUnit}
                  {durUnit === 'pts' && <> &nbsp;&middot;&nbsp; {formatMs(durLo * interval_ms)} → {formatMs(durHi * interval_ms)}</>}
                  &nbsp;&middot;&nbsp; segments shorter than target are kept as-is
                </div>
              </div>
            )}
          </div>

          {/* Sensor vis toggles */}
          {!isBatch && (
            <>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 7 }}>
                <span style={{ fontSize: 9, color: '#334155' }}>PREVIEW CHANNELS (by discriminant):</span>
                {(() => {
                  const grouped = groupSensorsByDiscriminant(sensors);
                  const featureGroups = [
                    { key: 'flex', label: 'Flex', color: '#38bdf8' },
                    { key: 'hall', label: 'Hall', color: '#34d399' },
                    { key: 'accel', label: 'Accel', color: '#f87171' },
                    { key: 'gyro', label: 'Gyro', color: '#fbbf24' },
                    { key: 'orient', label: 'Orient', color: '#a78bfa' },
                  ].map(group => ({
                    ...group,
                    sensors: sensors.filter(s => getSensorGroup(s) === group.key),
                  }));
                  const groups = [
                    { key: 'high', label: '🔴 HIGH', color: '#ef4444', sensors: grouped.high },
                    { key: 'medium', label: '🟡 MEDIUM', color: '#f59e0b', sensors: grouped.medium },
                    { key: 'low', label: '🟢 LOW', color: '#10b981', sensors: grouped.low },
                  ].filter(g => g.sensors.length > 0);

                  const toggleSensorGroup = (groupSensors) => {
                    const valid = (groupSensors || []).filter(ch => sensors.includes(ch));
                    if (!valid.length) return;
                    setVisSensors(prev => {
                      const next = new Set(prev);
                      const allSelected = valid.every(ch => next.has(ch));
                      valid.forEach(ch => (allSelected ? next.delete(ch) : next.add(ch)));
                      return next;
                    });
                  };

                  const selectDiscriminantGroup = (groupSensors) => {
                    const valid = (groupSensors || []).filter(ch => sensors.includes(ch));
                    if (!valid.length) return;
                    setVisSensors(new Set(valid));
                  };

                  return (
                    <>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
                        <span style={{ fontSize: 8, color: '#64748b' }}>Features:</span>
                        {featureGroups.map(group => (
                          <button
                            key={`feat-all-${group.key}`}
                            onClick={() => toggleSensorGroup(group.sensors)}
                            style={{
                              background: '#050c1a',
                              border: `1px solid ${group.color}`,
                              color: group.color,
                              borderRadius: 4,
                              padding: '2px 8px',
                              fontSize: 8,
                              cursor: 'pointer',
                              fontFamily: 'inherit',
                              fontWeight: 700,
                            }}
                            title={`Toggle all ${group.label} channels`}
                          >
                            {group.key}
                          </button>
                        ))}
                      </div>
                      <div style={{ height: 1, background: '#1e293b', margin: '2px 0' }} />
                      {groups.map(group => (
                        <div key={group.key} style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
                          <span style={{ fontSize: 8, fontWeight: 700, color: group.color, minWidth: 64 }}>{group.label}</span>
                          <button
                            onClick={() => selectDiscriminantGroup(group.sensors)}
                            style={{
                              background: '#050c1a',
                              border: `1px solid ${group.color}`,
                              color: group.color,
                              borderRadius: 4,
                              padding: '2px 7px',
                              fontSize: 8,
                              cursor: 'pointer',
                              fontFamily: 'inherit',
                              fontWeight: 700,
                            }}
                            title={`Select only ${group.label.replace(/^[^A-Z]+/, '')} channels`}
                          >
                            All
                          </button>
                          <span style={{ fontSize: 9, color: '#334155' }}>|</span>
                          <div style={{ display: 'flex', gap: 2, flexWrap: 'wrap' }}>
                            {group.sensors.map((s) => {
                              const idx = sensors.indexOf(s);
                              const selected = visSensors.has(s);
                              return (
                                <button
                                  key={s}
                                  onClick={() => setVisSensors(p => {
                                    const n = new Set(p);
                                    n.has(s) ? n.delete(s) : n.add(s);
                                    return n;
                                  })}
                                  style={{
                                    background: selected ? SENSOR_COLORS[idx % SENSOR_COLORS.length] + '22' : '#050c1a',
                                    border: `1px solid ${selected ? SENSOR_COLORS[idx % SENSOR_COLORS.length] : '#1e293b'}`,
                                    color: selected ? SENSOR_COLORS[idx % SENSOR_COLORS.length] : '#334155',
                                    borderRadius: 4,
                                    padding: '2px 6px',
                                    fontSize: 8,
                                    cursor: 'pointer',
                                    fontFamily: 'monospace',
                                    fontWeight: selected ? 700 : 400,
                                  }}
                                  title={selected ? `Hide ${s}` : `Show ${s}`}
                                >
                                  {s}
                                </button>
                              );
                            })}
                          </div>
                        </div>
                      ))}
                    </>
                  );
                })()}
              </div>

              {/* EI-style preview with overlay option */}
              <SplitPreviewCanvas
                values={values} sensors={sensors} interval_ms={interval_ms}
                cutPoints={previewCuts}
                removedSegments={removedSegs}
                onToggleRemove={toggleRemove}
                onCombineLeft={handleCombineLeft}
                onCombineRight={handleCombineRight}
                onSplitSegment={handleSplitSegment}
                onCutsChange={newCuts => { setCuts(newCuts); setMode('manual'); }}
                activeSensors={visSensors}
                padding={padInfo}
                height={250}
              />

              {/* Shift indicator under canvas */}
              {enableShift && shiftPreview.length > 0 && (
                <div style={{ marginTop: 5, padding: '5px 10px', background: '#451a0322', border: '1px solid #f59e0b33', borderRadius: 5, fontSize: 9, color: '#fbbf24' }}>
                  Preview shows a simulated random shift — actual shifts will differ per split. [{shiftLo},{shiftHi}]{shiftUnit}.
                </div>
              )}

              {/* Manual cutter (always available for fine-tuning) */}
              <div style={{ marginTop: 10 }}>
                <div style={{ fontSize: 10, color: '#64748b', marginBottom: 5 }}>Fine-tune cut points:</div>
                <WaveformCutter values={values} sensors={sensors} interval_ms={interval_ms}
                  cutPoints={cuts} onCutsChange={c => { setCuts(c); setRemovedSegs(new Set()); setMode('manual'); }}
                  activeSensors={[...visSensors]} height={130} />
              </div>

              {/* Segment label editors */}
              {keptSegs.length > 0 && (
                <div style={{ marginTop: 12 }}>
                  <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', marginBottom: 6 }}>
                    {keptSegs.length} KEPT SEGMENT{keptSegs.length !== 1 ? 'S' : ''}
                    {removedSegs.size > 0 && <span style={{ color: '#f87171', fontWeight: 400 }}> · {removedSegs.size} removed</span>}
                  </div>
                  <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(150px, 1fr))', gap: 5 }}>
                    {keptSegs.map((seg, ki) => {
                      const segValues = values.slice(seg.start, seg.end);
                      return (
                        <div key={seg.idx} style={{ background: '#050c1a', border: `1px solid ${SENSOR_COLORS[ki % SENSOR_COLORS.length]}44`, borderRadius: 7, padding: 8 }}>
                          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 3 }}>
                            <span style={{ fontSize: 10, fontWeight: 700, color: SENSOR_COLORS[ki % SENSOR_COLORS.length] }}>Seg {seg.idx + 1}</span>
                            <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                              <span style={{ fontSize: 9, color: '#475569' }}>{formatMs(segValues.length * interval_ms)}</span>
                              <button
                                onClick={() => toggleRemove(seg.idx)}
                                title="Remove this segment"
                                style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'none', border: '1px solid #3f1515', color: '#7f1d1d', borderRadius: 3, width: 18, height: 18, cursor: 'pointer', padding: 0, transition: 'all 0.1s' }}
                                onMouseEnter={e => { e.currentTarget.style.background = '#450a0a'; e.currentTarget.style.color = '#f87171'; }}
                                onMouseLeave={e => { e.currentTarget.style.background = 'none'; e.currentTarget.style.color = '#7f1d1d'; }}
                              >
                                <Trash2 size={9} strokeWidth={2} />
                              </button>
                            </div>
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
          {isBatch && batchSamples.length === 0 && (
            <div style={{ background: '#050c1a', border: '1px solid #7f1d1d55', borderRadius: 10, padding: 12, fontSize: 10, color: '#f87171' }}>
              No samples left in batch selection.
            </div>
          )}
          {isBatch && batchSamples.length > 0 && Object.keys(batchStates).length > 0 && (
            <BatchGraphs 
              samples={batchSamples} batchStates={batchPreviewStates} 
              sensors={sensors} visSensors={visSensors}
              padInfo={padInfo}
              onToggleRemove={handleBatchToggleRemove}
              onCombineLeft={handleBatchCombineLeft}
              onCombineRight={handleBatchCombineRight}
              onSplitSegment={handleBatchSplitSegment}
              onRemoveSample={handleBatchRemoveSample}
              onBatchCutsChange={handleBatchCutsChange}
            />
          )}

          {/* Footer */}
          <div style={{ display: 'flex', gap: 8, justifyContent: 'space-between', alignItems: 'center', marginTop: 16, paddingTop: 14, borderTop: '1px solid #1e293b' }}>
            <div style={{ fontSize: 10, color: '#475569', display: 'flex', gap: 10, flexWrap: 'wrap' }}>
              {!isBatch && <>
                <span style={{ color: '#34d399' }}>{keptSegs.length} segments kept</span>
                {removedSegs.size > 0 && <span style={{ color: '#f87171' }}>· {removedSegs.size} removed</span>}
                {enableShift && <span style={{ color: '#fbbf24' }}>· shift [{shiftLo},{shiftHi}]{shiftUnit}</span>}
                {enablePad && <span style={{ color: '#a78bfa' }}>· pad {padRandom ? `[${padLo},${padHi}] random` : `${padLo}–${padHi}`}{padUnit}</span>}
                {enableDur && <span style={{ color: '#38bdf8' }}>· dur [{durLo},{durHi}]{durUnit} @{durAlign}</span>}
              </>}
              {isBatch && <span style={{ color: '#34d399' }}>{totalKept} total segments</span>}
            </div>
            <div style={{ display: 'flex', gap: 8 }}>
              <button onClick={onClose} style={{ background: '#050c1a', border: '1px solid #1e293b', color: '#64748b', borderRadius: 6, padding: '8px 18px', cursor: 'pointer', fontFamily: 'inherit' }}>Cancel</button>
              <button onClick={doSplit}
                disabled={isBatch ? totalKept < 1 : keptSegs.length < 1}
                style={{
                  background: (isBatch ? totalKept >= 1 : keptSegs.length >= 1) ? '#1d4ed8' : '#1e293b',
                  border: 'none',
                  color: (isBatch ? totalKept >= 1 : keptSegs.length >= 1) ? '#fff' : '#334155',
                  borderRadius: 6, padding: '8px 22px', cursor: 'pointer', fontWeight: 700, fontFamily: 'inherit',
                }}>
                {isBatch
                  ? `Split ${batchSamples.length} → ${totalKept} total`
                  : `Split → ${keptSegs.length} sample${keptSegs.length !== 1 ? 's' : ''}`}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
