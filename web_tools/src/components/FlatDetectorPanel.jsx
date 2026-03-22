import { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { runFlatDetector, FLAT_DISCRIMINANT_CHANNELS } from '../utils/flatDetector';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs, simpleHash, uid } from '../utils/parse';
import { lastAlgoStore } from '../utils/lastAlgo';
import { Scissors, X, Plus } from 'lucide-react';

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
    flatRegions.forEach(r => {
      const x1 = (r.start / N) * W, x2 = (r.end / N) * W;
      ctx.fillStyle = '#450a0a55'; ctx.fillRect(x1, 0, x2 - x1, H);
    });
    const ty = H - (threshold / maxScore) * H;
    ctx.strokeStyle = '#f59e0b88'; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(0, ty); ctx.lineTo(W, ty); ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = '#f59e0b'; ctx.font = '8px monospace'; ctx.textAlign = 'left';
    ctx.fillText(`threshold ${threshold.toFixed(1)}`, 2, ty - 2);
    ctx.strokeStyle = '#34d399'; ctx.lineWidth = 1.5;
    ctx.beginPath();
    scores.forEach((w, i) => {
      const x = (w.center / N) * W;
      const y = H - (w.score / maxScore) * H;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();
    const totalMs = N * interval_ms;
    ctx.fillStyle = '#334155'; ctx.font = '8px monospace'; ctx.textAlign = 'center';
    for (let t = 0; t <= 5; t++) ctx.fillText(`${((t / 5) * totalMs / 1000).toFixed(1)}s`, (t / 5) * W, H - 1);
  }, [scores, flatRegions, threshold, N, interval_ms]);
  return <canvas ref={ref} width={860} height={height} style={{ width: '100%', height, display: 'block', borderRadius: 6 }} />;
}

// ─── Region selector canvas — drag to select a flat region on a waveform ──
function RegionSelectorCanvas({ sample, sensors, onRegionAdd, height = 80 }) {
  const ref = useRef();
  const [drag, setDrag] = useState(null);    // { startX, endX } in canvas coords
  const [regions, setRegions] = useState([]); // [{ start, end }] in point indices
  const N = sample?.values?.length || 0;
  const interval_ms = sample?.interval_ms || 33.33;

  const pxToIdx = useCallback((px, W) => Math.max(0, Math.min(N - 1, Math.round((px / W) * (N - 1)))), [N]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);
    if (!N) return;

    // Draw all sensor channels normalized
    sensors.slice(0, 6).forEach((s, si) => {
      const ci = sensors.indexOf(s);
      const col = sample.values.map(v => Array.isArray(v) ? v[ci] : Number(v));
      const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
      ctx.strokeStyle = SENSOR_COLORS[si % SENSOR_COLORS.length] + '99';
      ctx.lineWidth = 1;
      ctx.beginPath();
      col.forEach((val, i) => {
        const x = (i / Math.max(N - 1, 1)) * W;
        const y = H - ((val - mn) / rng) * (H - 4) - 2;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });

    // Draw committed regions
    regions.forEach((r, ri) => {
      const x1 = (r.start / N) * W, x2 = (r.end / N) * W;
      ctx.fillStyle = '#34d39933'; ctx.fillRect(x1, 0, x2 - x1, H);
      ctx.strokeStyle = '#34d399'; ctx.lineWidth = 1.5;
      ctx.strokeRect(x1, 0, x2 - x1, H);
      ctx.fillStyle = '#34d399'; ctx.font = 'bold 9px monospace'; ctx.textAlign = 'center';
      ctx.fillText(`R${ri + 1}`, (x1 + x2) / 2, H - 3);
    });

    // Draw current drag
    if (drag) {
      const x1 = Math.min(drag.startX, drag.endX);
      const x2 = Math.max(drag.startX, drag.endX);
      ctx.fillStyle = '#38bdf822'; ctx.fillRect(x1, 0, x2 - x1, H);
      ctx.strokeStyle = '#38bdf8'; ctx.lineWidth = 1.5; ctx.setLineDash([3, 3]);
      ctx.strokeRect(x1, 0, x2 - x1, H); ctx.setLineDash([]);
      const ms = Math.abs(pxToIdx(x2, W) - pxToIdx(x1, W)) * interval_ms;
      ctx.fillStyle = '#38bdf8'; ctx.font = '8px monospace'; ctx.textAlign = 'center';
      ctx.fillText(formatMs(ms), (x1 + x2) / 2, 10);
    }

    // X axis
    ctx.fillStyle = '#1e293b'; ctx.font = '8px monospace'; ctx.textAlign = 'center';
    for (let t = 0; t <= 4; t++) {
      const ms = (t / 4) * N * interval_ms;
      ctx.fillText(`${(ms / 1000).toFixed(1)}s`, (t / 4) * W, H);
    }
  }, [sample, sensors, N, interval_ms, drag, regions, pxToIdx]);

  const getX = e => {
    const c = ref.current, r = c.getBoundingClientRect();
    return (e.clientX - r.left) * (c.width / r.width);
  };

  const onMouseDown = e => { e.preventDefault(); const x = getX(e); setDrag({ startX: x, endX: x }); };
  const onMouseMove = e => { if (drag) setDrag(d => ({ ...d, endX: getX(e) })); };
  const onMouseUp = e => {
    if (!drag) return;
    const x1 = Math.min(drag.startX, drag.endX);
    const x2 = Math.max(drag.startX, drag.endX);
    const W = ref.current.width;
    const start = pxToIdx(x1, W);
    const end   = pxToIdx(x2, W);
    if (end - start > 5) {
      const region = { start, end };
      setRegions(prev => [...prev, region]);
      onRegionAdd?.(sample.values.slice(start, end), `${sample.label}:R${regions.length + 1}`);
    }
    setDrag(null);
  };

  const removeRegion = (ri) => {
    setRegions(prev => prev.filter((_, i) => i !== ri));
  };

  return (
    <div>
      <div style={{ position: 'relative' }}>
        <canvas ref={ref} width={860} height={height}
          style={{ width: '100%', height, display: 'block', borderRadius: 6, cursor: 'crosshair' }}
          onMouseDown={onMouseDown} onMouseMove={onMouseMove} onMouseUp={onMouseUp}
          onMouseLeave={() => { if (drag) setDrag(null); }}
        />
      </div>
      {regions.length > 0 && (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4, marginTop: 5 }}>
          {regions.map((r, i) => (
            <span key={i} style={{ display: 'flex', alignItems: 'center', gap: 4, background: '#052e1688', border: '1px solid #166534', color: '#34d399', fontSize: 8, borderRadius: 3, padding: '1px 6px', fontFamily: 'monospace' }}>
              R{i + 1}: {r.start}–{r.end} · {formatMs((r.end - r.start) * interval_ms)}
              <button onClick={() => removeRegion(i)}
                style={{ background: 'none', border: 'none', color: '#34d399', cursor: 'pointer', padding: 0, lineHeight: 1 }}>
                <X size={9} />
              </button>
            </span>
          ))}
        </div>
      )}
      <div style={{ fontSize: 8, color: '#334155', marginTop: 3 }}>
        Drag on waveform to select a flat/rest region to use as reference
      </div>
    </div>
  );
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
      {[['high', 'High', '#34d399'], ['medium', 'Medium', '#fbbf24'], ['low', 'Low', '#94a3b8']].map(([power, label, color]) => (
        byPower[power].length === 0 ? null :
        <div key={power} style={{ marginBottom: 6 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 3 }}>
            <span style={{ fontSize: 9, color, fontWeight: 700 }}>{label} discriminant</span>
            <button onClick={() => toggleAll(power)} style={{ background: 'none', border: `1px solid ${color}44`, color, borderRadius: 3, padding: '0px 5px', fontSize: 8, cursor: 'pointer', fontFamily: 'monospace' }}>all</button>
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3 }}>
            {byPower[power].map(d => {
              const ci = sensors.indexOf(d.key);
              return (
                <button key={d.key} onClick={() => { const n = new Set(selected); n.has(d.key) ? n.delete(d.key) : n.add(d.key); onChange(n); }} style={{
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

  const allSamplesRef = useRef(allSamples);
  useEffect(() => { allSamplesRef.current = allSamples; }, [allSamples]);
  const onCutsFoundRef = useRef(onCutsFound);
  useEffect(() => { onCutsFoundRef.current = onCutsFound; }, [onCutsFound]);

  const defaultCh = useMemo(() => {
    if (lastAlgoStore.flatSelectedCh && lastAlgoStore.flatSelectedCh.length > 0) {
      const restored = lastAlgoStore.flatSelectedCh.filter(ch => sensors.includes(ch));
      if (restored.length > 0) return new Set(restored);
    }
    const high = FLAT_DISCRIMINANT_CHANNELS.filter(d => d.power === 'high'   && sensors.includes(d.key)).map(d => d.key);
    const med  = FLAT_DISCRIMINANT_CHANNELS.filter(d => d.power === 'medium' && sensors.includes(d.key)).map(d => d.key);
    return new Set([...high, ...med]);
  }, [sensors]);

  const defaultIds = useMemo(() => {
    const stored = lastAlgoStore.flatSelectedIds || [];
    const validIds = new Set(allSamplesRef.current.map(s => s.id));
    return new Set(stored.filter(id => validIds.has(id)));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const [selectedFlatIds, setSelectedFlatIds] = useState(defaultIds);
  const [selectedCh, setSelectedCh]           = useState(defaultCh);
  const [search, setSearch]                   = useState('');
  const [windowSize,  setWindowSize]  = useState(lastAlgoStore.flatWindowSize  || 20);
  const [threshold,   setThreshold]   = useState(lastAlgoStore.flatThreshold   || 1.5);
  const [minFlatPts,  setMinFlatPts]  = useState(lastAlgoStore.flatMinFlatPts  || 15);
  const [result, setResult] = useState(null);

  // ── "Draw region" references: synthetic samples from user-drawn regions ──
  const [regionRefs, setRegionRefs] = useState([]); // [{ id, label, values }]
  const [expandRegion, setExpandRegion] = useState(false);
  const [regionSample, setRegionSample] = useState(null); // which sample to draw on

  const addRegionRef = useCallback((regionValues, label) => {
    const id = `region_${uid()}`;
    setRegionRefs(prev => [...prev, { id, label, values: regionValues }]);
    setSelectedFlatIds(prev => { const n = new Set(prev); n.add(id); return n; });
  }, []);

  const removeRegionRef = useCallback((id) => {
    setRegionRefs(prev => prev.filter(r => r.id !== id));
    setSelectedFlatIds(prev => { const n = new Set(prev); n.delete(id); return n; });
  }, []);

  useEffect(() => {
    lastAlgoStore.flatWindowSize  = windowSize;
    lastAlgoStore.flatThreshold   = threshold;
    lastAlgoStore.flatMinFlatPts  = minFlatPts;
  }, [windowSize, threshold, minFlatPts]);
  useEffect(() => { lastAlgoStore.flatSelectedIds = [...selectedFlatIds].filter(id => !String(id).startsWith('region_')); }, [selectedFlatIds]);
  useEffect(() => { lastAlgoStore.flatSelectedCh  = [...selectedCh]; }, [selectedCh]);

  const runDetector = useCallback(() => {
    // Combine whole-sample refs with drawn-region refs
    const wholeSampleData = allSamplesRef.current
      .filter(s => selectedFlatIds.has(s.id) && s.values?.length > 0)
      .map(s => s.values);
    const regionData = regionRefs
      .filter(r => selectedFlatIds.has(r.id) && r.values?.length > 0)
      .map(r => r.values);
    const flatData = [...wholeSampleData, ...regionData];

    if (!flatData.length) return;
    const useChannels = [...selectedCh].filter(ch => sensors.includes(ch));
    if (!useChannels.length) return;
    const res = runFlatDetector(values, sensors, flatData, useChannels, { windowSize, threshold, minFlatPts });
    setResult(res);
    onCutsFoundRef.current(res.cuts, res);
  }, [selectedFlatIds, selectedCh, sensors, values, windowSize, threshold, minFlatPts, regionRefs]);

  useEffect(() => {
    if (selectedFlatIds.size > 0 && selectedCh.size > 0) runDetector();
  }, [runDetector]);

  const eligibleSamples = useMemo(() =>
    allSamples.filter(s => s.id !== targetSample.id && s.values.length > 0),
    [allSamples, targetSample.id]
  );
  const filteredSamples = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return eligibleSamples;
    return eligibleSamples.filter(s =>
      s.label.toLowerCase().includes(q) ||
      (s.sampleName || '').toLowerCase().includes(q) ||
      (s.filename || '').toLowerCase().includes(q)
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
          Flat / Rest Reference Samples
          <span style={{ fontWeight: 400, color: '#64748b', fontSize: 10, marginLeft: 8 }}>({selectedFlatIds.size} selected)</span>
        </div>
        <div style={{ fontSize: 10, color: '#64748b', marginBottom: 8 }}>
          Select whole samples <i>or</i> draw regions on a sample to define flat/rest reference data.
        </div>
        {/* Search */}
        <div style={{ position: 'relative', marginBottom: 8 }}>
          <span style={{ position: 'absolute', left: 8, top: '50%', transform: 'translateY(-50%)', fontSize: 11, color: '#334155', pointerEvents: 'none' }}>⌕</span>
          <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Search by label, name, filename…"
            style={{ width: '100%', background: '#060d1a', color: '#f1f5f9', border: '1px solid #1e293b', borderRadius: 6, padding: '5px 10px 5px 26px', fontSize: 10, fontFamily: 'inherit', boxSizing: 'border-box' }} />
          {search && <button onClick={() => setSearch('')} style={{ position: 'absolute', right: 8, top: '50%', transform: 'translateY(-50%)', background: 'none', border: 'none', color: '#475569', cursor: 'pointer', fontSize: 12 }}>✕</button>}
        </div>

        {/* Bulk actions */}
        {eligibleSamples.length > 0 && (
          <div style={{ display: 'flex', gap: 6, marginBottom: 8 }}>
            <button onClick={() => setSelectedFlatIds(prev => { const n = new Set(prev); filteredSamples.forEach(s => n.add(s.id)); return n; })}
              style={{ background: 'none', border: '1px solid #1e293b', color: '#64748b', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
              Select all ({filteredSamples.length})
            </button>
            <button onClick={() => setSelectedFlatIds(prev => { const n = new Set(prev); filteredSamples.forEach(s => n.delete(s.id)); return n; })}
              style={{ background: 'none', border: '1px solid #1e293b', color: '#64748b', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
              Clear samples
            </button>
          </div>
        )}

        {eligibleSamples.length === 0 ? (
          <div style={{ color: '#f87171', fontSize: 10 }}>No other samples loaded.</div>
        ) : filteredSamples.length === 0 ? (
          <div style={{ color: '#475569', fontSize: 10 }}>No samples match "{search}"</div>
        ) : (
          <div style={{ maxHeight: 160, overflowY: 'auto', display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))', gap: 4 }}>
            {filteredSamples.map(s => {
              const sel = selectedFlatIds.has(s.id);
              return (
                <div key={s.id}
                  onClick={() => setSelectedFlatIds(p => { const n = new Set(p); n.has(s.id) ? n.delete(s.id) : n.add(s.id); return n; })}
                  style={{ display: 'flex', alignItems: 'flex-start', gap: 7, padding: '5px 9px', background: sel ? '#0d2040' : '#060d1a', border: `1px solid ${sel ? '#3b82f6' : '#1e293b'}`, borderRadius: 6, cursor: 'pointer' }}>
                  <input type="checkbox" checked={sel} readOnly style={{ accentColor: '#3b82f6', flexShrink: 0, marginTop: 2 }} />
                  <div style={{ minWidth: 0 }}>
                    <div style={{ fontSize: 10, fontWeight: 700, color: sel ? '#60a5fa' : '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{s.label}</div>
                    {s.sampleName && <div style={{ fontSize: 8, color: '#334155', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{s.sampleName}</div>}
                    <div style={{ fontSize: 8, color: '#334155' }}>{s.values.length}pts · {formatMs(s.duration_ms)}</div>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>

      {/* ── Draw region as reference ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: expandRegion ? 10 : 0 }}>
          <Scissors size={13} color="#34d399" />
          <span style={{ fontSize: 11, fontWeight: 700, color: '#34d399' }}>Draw Flat Region as Reference</span>
          <span style={{ fontSize: 9, color: '#475569' }}>— select part of a sample waveform as flat reference</span>
          <button onClick={() => setExpandRegion(v => !v)}
            style={{ marginLeft: 'auto', background: 'none', border: '1px solid #1e293b', color: '#475569', borderRadius: 4, padding: '2px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
            {expandRegion ? 'Collapse' : 'Expand'}
          </button>
        </div>

        {expandRegion && (
          <div>
            {/* Sample picker for region drawing */}
            <div style={{ marginBottom: 8 }}>
              <div style={{ fontSize: 10, color: '#64748b', marginBottom: 4 }}>Choose sample to draw region on:</div>
              <select value={regionSample?.id || ''} onChange={e => {
                const s = allSamples.find(s => String(s.id) === e.target.value);
                setRegionSample(s || null);
              }} style={{ background: '#060d1a', color: '#f1f5f9', border: '1px solid #1e293b', borderRadius: 5, padding: '4px 8px', fontSize: 10, fontFamily: 'inherit', width: '100%' }}>
                <option value="">— pick a sample —</option>
                {allSamples.filter(s => s.values?.length > 0).map(s => (
                  <option key={s.id} value={s.id}>{s.label}{s.sampleName ? ` · ${s.sampleName}` : ''} ({s.values.length}pts)</option>
                ))}
              </select>
            </div>

            {regionSample && (
              <RegionSelectorCanvas
                sample={regionSample}
                sensors={sensors}
                onRegionAdd={addRegionRef}
                height={80}
              />
            )}

            {/* Committed region refs */}
            {regionRefs.length > 0 && (
              <div style={{ marginTop: 10 }}>
                <div style={{ fontSize: 10, color: '#64748b', marginBottom: 5 }}>Drawn region references ({regionRefs.length}):</div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                  {regionRefs.map(r => {
                    const active = selectedFlatIds.has(r.id);
                    return (
                      <div key={r.id} style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '5px 9px', background: active ? '#052e16' : '#060d1a', border: `1px solid ${active ? '#166534' : '#1e293b'}`, borderRadius: 6 }}>
                        <input type="checkbox" checked={active} onChange={() => setSelectedFlatIds(prev => { const n = new Set(prev); active ? n.delete(r.id) : n.add(r.id); return n; })} style={{ accentColor: '#34d399' }} />
                        <span style={{ fontSize: 10, color: active ? '#34d399' : '#64748b', flex: 1, fontFamily: 'monospace' }}>{r.label}</span>
                        <span style={{ fontSize: 9, color: '#334155' }}>{r.values.length}pts · {formatMs(r.values.length * interval_ms)}</span>
                        <button onClick={() => removeRegionRef(r.id)}
                          style={{ background: 'none', border: 'none', color: '#475569', cursor: 'pointer', padding: 2 }}>
                          <X size={11} />
                        </button>
                      </div>
                    );
                  })}
                </div>
              </div>
            )}
          </div>
        )}
      </div>

      {/* ── Channel selector ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 4 }}>Discriminant Channels</div>
        <div style={{ fontSize: 10, color: '#64748b', marginBottom: 8 }}>Channels used to distinguish flat vs active.</div>
        <ChannelSelector sensors={sensors} selected={selectedCh} onChange={setSelectedCh} />
      </div>

      {/* ── Detection params ── */}
      <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8', marginBottom: 10 }}>Detection Parameters</div>
        <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', marginBottom: 10 }}>
          <Sldr label="Window Size" value={windowSize} min={5} max={60} onChange={setWindowSize} fmt={v => `${v}pts (${formatMs(v * interval_ms)})`} />
          <Sldr label="Threshold" value={Math.round(threshold * 10)} min={1} max={50} onChange={v => setThreshold(v / 10)} fmt={() => threshold.toFixed(1)} />
          <Sldr label="Min Flat Length" value={minFlatPts} min={5} max={100} onChange={setMinFlatPts} fmt={v => `${v}pts (${formatMs(v * interval_ms)})`} />
        </div>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button onClick={runDetector} disabled={selectedFlatIds.size === 0 || selectedCh.size === 0}
            style={{ background: selectedFlatIds.size > 0 && selectedCh.size > 0 ? '#065f46' : '#1e293b', border: 'none', color: selectedFlatIds.size > 0 && selectedCh.size > 0 ? '#34d399' : '#334155', borderRadius: 6, padding: '7px 18px', cursor: 'pointer', fontSize: 11, fontWeight: 700, fontFamily: 'inherit' }}>
            Detect Flat Regions
          </button>
          {result && <span style={{ fontSize: 10, color: '#64748b' }}>Found <span style={{ color: '#34d399' }}>{result.flatRegions.length}</span> flat regions → <span style={{ color: '#60a5fa' }}>{result.cuts.length + 1}</span> segments</span>}
          {selectedFlatIds.size === 0 && <span style={{ fontSize: 10, color: '#f87171' }}>Select at least one reference above</span>}
        </div>
      </div>

      {/* ── Activity score graph ── */}
      {result && result.scores.length > 0 && (
        <div style={{ background: '#050c1a', border: '1px solid #1e293b', borderRadius: 10, padding: 12, marginBottom: 12 }}>
          <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', marginBottom: 5 }}>
            Activity Score — <span style={{ color: '#34d399' }}>score</span> · <span style={{ color: '#f59e0b' }}>threshold</span> · <span style={{ color: '#f87171' }}>flat</span>
          </div>
          <ActivityGraph scores={result.scores} flatRegions={result.flatRegions} threshold={threshold} N={N} interval_ms={interval_ms} height={88} />
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
