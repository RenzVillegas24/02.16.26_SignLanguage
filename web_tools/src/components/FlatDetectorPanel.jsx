import { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { runFlatDetector, FLAT_DISCRIMINANT_CHANNELS } from '../utils/flatDetector';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';
import { lastAlgoStore } from '../utils/lastAlgo';

// ─── Activity score graph ─────────────────────────────────────────────────
function ActivityGraph({ scores, flatRegions, threshold, N, interval_ms, height = 80 }) {
  const ref = useRef();
  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);
    if (!scores.length) return;
    const maxScore = Math.max(...scores.map(s => s.score), threshold * 2, 3);

    // Flat region shading
    flatRegions.forEach(r => {
      const x1 = (r.start / N) * W, x2 = (r.end / N) * W;
      ctx.fillStyle = '#450a0a55'; ctx.fillRect(x1, 0, x2 - x1, H);
    });

    // Threshold line
    const ty = H - (threshold / maxScore) * H;
    ctx.strokeStyle = '#f59e0b88'; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(0, ty); ctx.lineTo(W, ty); ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = '#f59e0b'; ctx.font = '8px monospace'; ctx.textAlign = 'left';
    ctx.fillText(`threshold ${threshold.toFixed(1)}`, 2, ty - 2);

    // Score line
    ctx.strokeStyle = '#34d399'; ctx.lineWidth = 1.5;
    ctx.beginPath();
    scores.forEach((w, i) => {
      const x = (w.center / N) * W;
      const y = H - (w.score / maxScore) * H;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();

    // X time labels
    const totalMs = N * interval_ms;
    ctx.fillStyle = '#334155'; ctx.font = '8px monospace'; ctx.textAlign = 'center';
    for (let t = 0; t <= 5; t++) {
      ctx.fillText(`${((t / 5) * totalMs / 1000).toFixed(1)}s`, (t / 5) * W, H - 1);
    }
  }, [scores, flatRegions, threshold, N, interval_ms]);

  return <canvas ref={ref} width={860} height={height} style={{ width: '100%', height, display: 'block', borderRadius: 6 }} />;
}

// ─── Channel selector ─────────────────────────────────────────────────────
function ChannelSelector({ sensors, selected, onChange }) {
  const available = FLAT_DISCRIMINANT_CHANNELS.filter(d => sensors.includes(d.key));
  const byPower = { high: [], medium: [], low: [] };
  available.forEach(d => byPower[d.power].push(d));

  const toggleAll = power => {
    const keys = byPower[power].map(d => d.key);
    const allOn = keys.every(k => selected.has(k));
    const next = new Set(selected);
    keys.forEach(k => allOn ? next.delete(k) : next.add(k));
    onChange(next);
  };

  return (
    <div>
      {[['high', '🔥 High', '#34d399'], ['medium', '⚡ Medium', '#fbbf24'], ['low', '📉 Low', '#94a3b8']].map(([power, label, color]) => (
        byPower[power].length === 0 ? null :
        <div key={power} style={{ marginBottom: 6 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 3 }}>
            <span style={{ fontSize: 9, color, fontWeight: 700 }}>{label} discriminant</span>
            <button onClick={() => toggleAll(power)} style={{
              background: 'none', border: `1px solid ${color}44`, color,
              borderRadius: 3, padding: '0px 5px', fontSize: 8, cursor: 'pointer', fontFamily: 'monospace'
            }}>all</button>
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3 }}>
            {byPower[power].map(d => {
              const ci = sensors.indexOf(d.key);
              return (
                <button key={d.key} onClick={() => {
                  const n = new Set(selected); n.has(d.key) ? n.delete(d.key) : n.add(d.key); onChange(n);
                }} style={{
                  background: selected.has(d.key) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] + '33' : '#050c1a',
                  border: `1px solid ${selected.has(d.key) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] : '#1e293b'}`,
                  color: selected.has(d.key) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] : '#475569',
                  borderRadius: 4, padding: '2px 7px', fontSize: 9, cursor: 'pointer',
                  fontFamily: 'monospace', fontWeight: selected.has(d.key) ? 700 : 400,
                }}>{d.key}</button>
              );
            })}
          </div>
        </div>
      ))}
    </div>
  );
}

// ─── Main panel ───────────────────────────────────────────────────────────
export default function FlatDetectorPanel({ targetSample, allSamples, sensors, onCutsFound }) {
  const { values, interval_ms, label: targetLabel } = targetSample;
  const N = values.length;

  // Default channels: high + medium discriminant, from store if available
  const defaultCh = useMemo(() => {
    if (lastAlgoStore.flatSelectedCh && lastAlgoStore.flatSelectedCh.length > 0) {
      // restore from store, but only keep channels that exist in this dataset
      const restored = lastAlgoStore.flatSelectedCh.filter(ch => sensors.includes(ch));
      if (restored.length > 0) return new Set(restored);
    }
    const high = FLAT_DISCRIMINANT_CHANNELS.filter(d => d.power === 'high'   && sensors.includes(d.key)).map(d => d.key);
    const med  = FLAT_DISCRIMINANT_CHANNELS.filter(d => d.power === 'medium' && sensors.includes(d.key)).map(d => d.key);
    return new Set([...high, ...med]);
  }, [sensors]);

  // Default selected flat IDs from store
  const defaultIds = useMemo(() => {
    const stored = lastAlgoStore.flatSelectedIds || [];
    const validIds = new Set(allSamples.map(s => s.id));
    return new Set(stored.filter(id => validIds.has(id)));
  }, [allSamples]);

  const [selectedFlatIds, setSelectedFlatIds] = useState(defaultIds);
  const [selectedCh, setSelectedCh]           = useState(defaultCh);
  const [search, setSearch]                   = useState('');

  // Params — restore from store
  const [windowSize,  setWindowSize]  = useState(lastAlgoStore.flatWindowSize);
  const [threshold,   setThreshold]   = useState(lastAlgoStore.flatThreshold);
  const [minFlatPts,  setMinFlatPts]  = useState(lastAlgoStore.flatMinFlatPts);
  const [result,      setResult]      = useState(null);

  // Persist to store whenever state changes
  useEffect(() => {
    lastAlgoStore.flatWindowSize  = windowSize;
    lastAlgoStore.flatThreshold   = threshold;
    lastAlgoStore.flatMinFlatPts  = minFlatPts;
    lastAlgoStore.flatSelectedIds = [...selectedFlatIds];
    lastAlgoStore.flatSelectedCh  = [...selectedCh];
  }, [windowSize, threshold, minFlatPts, selectedFlatIds, selectedCh]);

  // Run detector
  const runDetector = useCallback(() => {
    const flatSamples = allSamples
      .filter(s => selectedFlatIds.has(s.id) && s.values.length > 0)
      .map(s => s.values);
    if (!flatSamples.length) return;
    const useChannels = [...selectedCh].filter(ch => sensors.includes(ch));
    if (!useChannels.length) return;
    const res = runFlatDetector(values, sensors, flatSamples, useChannels, { windowSize, threshold, minFlatPts });
    setResult(res);
    onCutsFound(res.cuts);
  }, [allSamples, selectedFlatIds, selectedCh, sensors, values, windowSize, threshold, minFlatPts, onCutsFound]);

  // Auto-run when params or selection changes
  useEffect(() => {
    if (selectedFlatIds.size > 0 && selectedCh.size > 0) runDetector();
  }, [runDetector]);

  // Eligible samples (exclude target itself)
  const eligibleSamples = useMemo(() =>
    allSamples.filter(s => s.id !== targetSample.id && s.values.length > 0),
    [allSamples, targetSample]
  );

  // Filtered by search
  const filteredSamples = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return eligibleSamples;
    return eligibleSamples.filter(s =>
      s.label.toLowerCase().includes(q) ||
      (s.sampleName || '').toLowerCase().includes(q) ||
      s.filename.toLowerCase().includes(q)
    );
  }, [eligibleSamples, search]);

  const Sldr = ({ label: l, value, min, max, step = 1, fmt, onChange }) => (
    <div style={{ flex: 1, minWidth: 110 }}>
      <span style={{ color: '#64748b', fontSize: 10, display: 'block', marginBottom: 2 }}>
        {l}: <b style={{ color: '#94a3b8' }}>{fmt ? fmt(value) : value}</b>
      </span>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(Number(e.target.value))}
        style={{ width: '100%', accentColor: '#3b82f6' }} />
    </div>
  );

  return (
    <div>
      {/* ── Reference sample picker ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 4 }}>
          📚 Flat / Rest Reference Samples
          <span style={{ fontWeight: 400, color: '#64748b', fontSize: 10, marginLeft: 8 }}>
            ({selectedFlatIds.size} selected)
          </span>
        </div>
        <div style={{ fontSize: 10, color: '#64748b', marginBottom: 8 }}>
          Select samples that represent the <b style={{ color: '#f1f5f9' }}>flat / rest / idle</b> state.
          The detector learns their signal profile and finds matching regions in <b style={{ color: '#60a5fa' }}>{targetLabel}</b>.
        </div>

        {/* Search box */}
        <div style={{ position: 'relative', marginBottom: 8 }}>
          <span style={{ position: 'absolute', left: 8, top: '50%', transform: 'translateY(-50%)', fontSize: 11, color: '#334155', pointerEvents: 'none' }}>🔍</span>
          <input
            value={search}
            onChange={e => setSearch(e.target.value)}
            placeholder="Search samples by label, name, filename…"
            style={{
              width: '100%', background: '#060d1a', color: '#f1f5f9',
              border: '1px solid #1e293b', borderRadius: 6,
              padding: '5px 10px 5px 26px', fontSize: 10,
              fontFamily: 'inherit', boxSizing: 'border-box',
            }}
          />
          {search && (
            <button onClick={() => setSearch('')} style={{ position: 'absolute', right: 8, top: '50%', transform: 'translateY(-50%)', background: 'none', border: 'none', color: '#475569', cursor: 'pointer', fontSize: 12, lineHeight: 1 }}>✕</button>
          )}
        </div>

        {eligibleSamples.length === 0 ? (
          <div style={{ color: '#f87171', fontSize: 10 }}>No other samples loaded. Load a flat/rest sample first.</div>
        ) : filteredSamples.length === 0 ? (
          <div style={{ color: '#475569', fontSize: 10 }}>No samples match "{search}"</div>
        ) : (
          <>
            <div style={{ display: 'flex', gap: 6, marginBottom: 6 }}>
              <button onClick={() => setSelectedFlatIds(new Set(filteredSamples.map(s => s.id)))}
                style={{ background: 'none', border: '1px solid #1e293b', color: '#64748b', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
                Select all {filteredSamples.length > eligibleSamples.length ? `(${filteredSamples.length} filtered)` : `(${eligibleSamples.length})`}
              </button>
              <button onClick={() => setSelectedFlatIds(new Set())}
                style={{ background: 'none', border: '1px solid #1e293b', color: '#64748b', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
                Clear
              </button>
              {search && (
                <span style={{ fontSize: 9, color: '#475569', alignSelf: 'center' }}>
                  {filteredSamples.length} of {eligibleSamples.length} shown
                </span>
              )}
            </div>
            <div style={{ maxHeight: 180, overflowY: 'auto', display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))', gap: 4, paddingRight: 2 }}>
              {filteredSamples.map(s => {
                const sel = selectedFlatIds.has(s.id);
                return (
                  <div key={s.id}
                    onClick={() => setSelectedFlatIds(p => { const n = new Set(p); n.has(s.id) ? n.delete(s.id) : n.add(s.id); return n; })}
                    style={{
                      display: 'flex', alignItems: 'flex-start', gap: 7, padding: '6px 9px',
                      background: sel ? '#0d2040' : '#060d1a',
                      border: `1px solid ${sel ? '#3b82f6' : '#1e293b'}`,
                      borderRadius: 6, cursor: 'pointer', transition: 'all 0.1s',
                    }}>
                    <input type="checkbox" checked={sel} readOnly style={{ accentColor: '#3b82f6', flexShrink: 0, marginTop: 2 }} />
                    <div style={{ minWidth: 0 }}>
                      <div style={{ fontSize: 10, fontWeight: 700, color: sel ? '#60a5fa' : '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                        {s.label}
                      </div>
                      {s.sampleName && (
                        <div style={{ fontSize: 8, color: '#334155', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{s.sampleName}</div>
                      )}
                      <div style={{ fontSize: 8, color: '#334155', display: 'flex', gap: 4 }}>
                        <span>{s.values.length}pts</span>
                        <span>·</span>
                        <span>{formatMs(s.duration_ms)}</span>
                        <span style={{ color: s.category === 'testing' ? '#f59e0b' : '#3b82f6' }}>{s.category === 'testing' ? 'TEST' : 'TRAIN'}</span>
                      </div>
                    </div>
                  </div>
                );
              })}
            </div>
          </>
        )}
      </div>

      {/* ── Channel selector ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 4 }}>📡 Discriminant Channels</div>
        <div style={{ fontSize: 10, color: '#64748b', marginBottom: 8 }}>
          Channels used to distinguish flat vs active. High-discriminant IMU channels (gyro/accel) are pre-selected based on your data.
        </div>
        <ChannelSelector sensors={sensors} selected={selectedCh} onChange={setSelectedCh} />
      </div>

      {/* ── Detection params ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 10 }}>⚙️ Detection Parameters</div>
        <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', marginBottom: 10 }}>
          <Sldr label="Window Size" value={windowSize} min={5} max={60} onChange={setWindowSize}
            fmt={v => `${v}pts (${formatMs(v * interval_ms)})`} />
          <Sldr label="Threshold" value={Math.round(threshold * 10)} min={1} max={50}
            onChange={v => setThreshold(v / 10)} fmt={() => threshold.toFixed(1)} />
          <Sldr label="Min Flat Length" value={minFlatPts} min={5} max={100} onChange={setMinFlatPts}
            fmt={v => `${v}pts (${formatMs(v * interval_ms)})`} />
        </div>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={runDetector}
            disabled={selectedFlatIds.size === 0 || selectedCh.size === 0}
            style={{
              background: selectedFlatIds.size > 0 && selectedCh.size > 0 ? '#065f46' : '#1e293b',
              border: 'none',
              color: selectedFlatIds.size > 0 && selectedCh.size > 0 ? '#34d399' : '#334155',
              borderRadius: 6, padding: '7px 18px', cursor: 'pointer',
              fontSize: 11, fontWeight: 700, fontFamily: 'inherit',
            }}>
            🔍 Detect Flat Regions
          </button>
          {result && (
            <span style={{ fontSize: 10, color: '#64748b' }}>
              Found <span style={{ color: '#34d399' }}>{result.flatRegions.length}</span> flat region{result.flatRegions.length !== 1 ? 's' : ''}
              {' → '}<span style={{ color: '#60a5fa' }}>{result.cuts.length + 1}</span> segment{result.cuts.length !== 0 ? 's' : ''}
            </span>
          )}
          {selectedFlatIds.size === 0 && (
            <span style={{ fontSize: 10, color: '#f87171' }}>Select at least one reference sample above</span>
          )}
        </div>
      </div>

      {/* ── Activity score graph ── */}
      {result && result.scores.length > 0 && (
        <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 12, marginBottom: 12 }}>
          <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', marginBottom: 5 }}>
            Activity Score over Time
            <span style={{ fontWeight: 400, marginLeft: 8 }}>
              <span style={{ color: '#34d399' }}>— score</span>
              {'  ·  '}
              <span style={{ color: '#f59e0b' }}>— threshold</span>
              {'  ·  '}
              <span style={{ color: '#f87171' }}>█ detected flat</span>
            </span>
          </div>
          <ActivityGraph scores={result.scores} flatRegions={result.flatRegions}
            threshold={threshold} N={N} interval_ms={interval_ms} height={88} />
          {result.flatRegions.length > 0 && (
            <div style={{ marginTop: 6, display: 'flex', flexWrap: 'wrap', gap: 4 }}>
              {result.flatRegions.map((r, i) => (
                <span key={i} style={{ background: '#450a0a44', border: '1px solid #7f1d1d', color: '#f87171', fontSize: 9, borderRadius: 3, padding: '1px 7px', fontFamily: 'monospace' }}>
                  flat {i + 1}: {r.start}–{r.end} · {formatMs((r.end - r.start) * interval_ms)}
                </span>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
