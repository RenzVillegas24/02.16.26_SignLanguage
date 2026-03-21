import { useState, useEffect, useCallback, useMemo } from 'react';
import { runAutoDetect, pickBestChannels, batchAutoSplit } from '../utils/algorithms';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';
import WaveformCutter from './WaveformCutter';
import SplitPreviewCanvas from './SplitPreviewCanvas';
import FlatDetectorPanel from './FlatDetectorPanel';
import { lastAlgoStore } from '../utils/lastAlgo';
import { runFlatDetector } from '../utils/flatDetector';

// ─── Batch preview ─────────────────────────────────────────────────────────
function BatchGraphs({ samples, batchStates, sensors, visSensors, enablePad, padMin, padMax, padUnit, padRandom, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment }) {
  const padInfo = enablePad ? { min: padMin, max: padMax, unit: padUnit, random: padRandom } : null;
  return (
    <div style={{ maxHeight: '50vh', overflowY: 'auto', marginTop: 10, display: 'flex', flexDirection: 'column', gap: 16 }}>
      {samples.map((smpl) => {
        const state = batchStates[smpl.id] || { cuts: [], removedSegs: new Set() };
        return (
          <div key={smpl.id} style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 8, padding: 10 }}>
            <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 6 }}>
              {smpl.label} {smpl.sampleName ? <span style={{ color: '#64748b' }}>· {smpl.sampleName}</span> : ''}
              <span style={{ color: '#475569', fontWeight: 400, marginLeft: 8 }}>
                {state.cuts.length + 1 - state.removedSegs.size} / {state.cuts.length + 1} kept
              </span>
            </div>
            <SplitPreviewCanvas
              values={smpl.values} sensors={sensors} interval_ms={smpl.interval_ms}
              cutPoints={state.cuts}
              removedSegments={state.removedSegs}
              onToggleRemove={(idx) => onToggleRemove(smpl.id, idx)}
              onCombineLeft={(idx) => onCombineLeft(smpl.id, idx)}
              onCombineRight={(idx) => onCombineRight(smpl.id, idx)}
              onSplitSegment={(idx) => onSplitSegment(smpl.id, idx)}
              activeSensors={visSensors}
              padding={padInfo}
              height={140}
            />
          </div>
        );
      })}
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
  const [padMin, setPadMin]         = useState(Number(lastAlgoStore.padMin ?? 0));
  const [padMax, setPadMax]         = useState(Number(lastAlgoStore.padMax ?? 0));
  const [padUnit, setPadUnit]       = useState(lastAlgoStore.padUnit);
  const [padRandom, setPadRandom]   = useState(lastAlgoStore.padRandom);

  // Predicted-flat behavior
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
      mode, algo, windowSize, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts,
      shiftEnabled: enableShift, shiftMin, shiftMax, shiftUnit,
      padEnabled: enablePad, padMin, padMax, padUnit, padRandom,
      flatAutoDisable: autoDisableFlat,
    });
  }, [mode, algo, windowSize, threshold, stdMult, sensitivity, minGap, numParts,
      equalParts, enableShift, shiftMin, shiftMax, shiftUnit,
      enablePad, padMin, padMax, padUnit, padRandom, autoDisableFlat]);

  // ── Derived ────────────────────────────────────────────────────────────
  const allCuts    = useMemo(() => [0, ...cuts, N].sort((a, b) => a - b), [cuts, N]);
  const segments   = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);
  const keptSegs   = useMemo(() => segments.filter(s => !removedSegs.has(s.idx)), [segments, removedSegs]);

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
    if (isBatch) {
      if (mode === 'flat') {
        const selectedFlatIds = new Set(lastAlgoStore.flatSelectedIds || []);
        const selectedCh = (lastAlgoStore.flatSelectedCh || []).filter(ch => sensors.includes(ch));
        const flatRefs = allSamples.filter(s => selectedFlatIds.has(s.id) && s.values?.length > 0);
        if (!selectedCh.length || !flatRefs.length) {
          const emptyStates = {};
          samples.forEach(smpl => { emptyStates[smpl.id] = { cuts: [], removedSegs: new Set() }; });
          setBatchStates(emptyStates);
          return;
        }

        const cfg = {
          windowSize: lastAlgoStore.flatWindowSize,
          threshold: lastAlgoStore.flatThreshold,
          minFlatPts: lastAlgoStore.flatMinFlatPts,
        };

        const states = {};
        samples.forEach(smpl => {
          let refsForSample = flatRefs.filter(r => r.id !== smpl.id).map(r => r.values);
          if (!refsForSample.length) refsForSample = flatRefs.map(r => r.values);
          const res = runFlatDetector(smpl.values, sensors, refsForSample, selectedCh, cfg);
          const cuts = res.cuts || [];
          const removed = autoDisableFlat ? getFlatRemovedSegs(cuts, res.flatRegions || [], smpl.values.length) : new Set();
          states[smpl.id] = { cuts, removedSegs: removed };
        });
        setBatchStates(states);
      } else {
        const bRes = batchAutoSplit(samples, sensors, getCfg());
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
  }, [mode, getCfg, values, sensors, N, equalParts, isBatch, samples, allSamples, autoDisableFlat, getFlatRemovedSegs]);

  useEffect(() => { if (mode !== 'manual') recalc(); }, [mode, recalc]);

  // ── Execute split ──────────────────────────────────────────────────────
  const doSplit = () => {
    if (isBatch) {
      if (!Object.keys(batchStates).length) return;
      const finalBatch = [];
      samples.forEach(smpl => {
        const state = batchStates[smpl.id];
        if (!state) return;
        const currentCuts = state.cuts || [];
        const sr = state.removedSegs || new Set();
        
        // shift
        let finalCuts = [...currentCuts];
        if (enableShift) {
          const shiftLoRaw = shiftUnit === 'ms' ? Math.round(shiftMin / smpl.interval_ms) : Math.round(shiftMin);
          const shiftHiRaw = shiftUnit === 'ms' ? Math.round(shiftMax / smpl.interval_ms) : Math.round(shiftMax);
          const lo = Math.min(shiftLoRaw, shiftHiRaw);
          const hi = Math.max(shiftLoRaw, shiftHiRaw);
          finalCuts = currentCuts.map(c => {
            const shift = Math.round(lo + Math.random() * (hi - lo));
            return Math.max(1, Math.min(smpl.values.length - 1, c + shift));
          }).sort((a, b) => a - b);
        }

        const fc = [0, ...finalCuts, smpl.values.length].sort((a, b) => a - b);
        const allPartRecords = fc.slice(0, -1).map((s, i) => {
          let start = s, end = fc[i + 1];
          if (enablePad && !sr.has(i)) {
            const padLoRaw = padUnit === 'ms' ? padMin / smpl.interval_ms : padMin;
            const padHiRaw = padUnit === 'ms' ? padMax / smpl.interval_ms : padMax;
            const lo = Math.min(padLoRaw, padHiRaw);
            const hi = Math.max(padLoRaw, padHiRaw);
            const actualPad = padRandom ? Math.round(lo + Math.random() * (hi - lo)) : Math.round(lo);
            start = Math.max(0, start - actualPad);
            end   = Math.min(smpl.values.length, end + actualPad);
          }
          return { idx: i, start, end, values: smpl.values.slice(start, end) };
        });
        const keptRecords = allPartRecords.filter(r => !sr.has(r.idx));
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
      const allPartRecords = fc.slice(0, -1).map((s, i) => {
        let start = s, end = fc[i + 1];
        if (enablePad && !removedSegs.has(i)) {
          const padLoRaw = padUnit === 'ms' ? padMin / interval_ms : padMin;
          const padHiRaw = padUnit === 'ms' ? padMax / interval_ms : padMax;
          const lo = Math.min(padLoRaw, padHiRaw);
          const hi = Math.max(padLoRaw, padHiRaw);
          const actualPad = padRandom ? Math.round(lo + Math.random() * (hi - lo)) : Math.round(lo);
          start = Math.max(0, start - actualPad);
          end   = Math.min(N, end + actualPad);
        }
        return { idx: i, start, end, values: values.slice(start, end) };
      });
      const keptRecords = allPartRecords.filter(r => !removedSegs.has(r.idx));
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
    ? Object.values(batchStates).reduce((a, s) => a + (s.cuts.length + 1 - (s.removedSegs?.size || 0)), 0)
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
      const smpl = samples.find(s => s.id === smplId);
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

  // Cuts to show in preview — if shift enabled, show the shifted preview
  const previewCuts = enableShift && shiftPreview.length ? shiftPreview : cuts;

  const padInfo = enablePad ? { min: padMin, max: padMax, unit: padUnit, random: padRandom } : null;

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
          {
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
          }

          {/* ── Padding ── */}
          {
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
                    <input type="number" value={padMin} onChange={e => setPadMin(Number(e.target.value))}
                      style={{ ...SI, width: 70 }} />
                  </div>
                  {padRandom && (
                    <div>
                      <span style={{ fontSize: 10, color: '#64748b', display: 'block', marginBottom: 3 }}>Max pad</span>
                      <input type="number" value={padMax} onChange={e => setPadMax(Number(e.target.value))}
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
          }

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
                onCombineLeft={handleCombineLeft}
                onCombineRight={handleCombineRight}
                onSplitSegment={handleSplitSegment}
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
          {isBatch && Object.keys(batchStates).length > 0 && (
            <BatchGraphs 
              samples={samples} batchStates={batchStates} 
              sensors={sensors} visSensors={visSensors}
              enablePad={enablePad} padMin={padMin} padMax={padMax} padUnit={padUnit} padRandom={padRandom}
              onToggleRemove={handleBatchToggleRemove}
              onCombineLeft={handleBatchCombineLeft}
              onCombineRight={handleBatchCombineRight}
              onSplitSegment={handleBatchSplitSegment}
            />
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
