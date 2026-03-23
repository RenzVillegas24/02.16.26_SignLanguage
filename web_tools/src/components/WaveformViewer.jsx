import { useRef, useEffect, useMemo, useState } from 'react';
import { LayoutDashboard, Layers, GitGraph } from 'lucide-react';
import { useTheme } from '../utils/ThemeContext';
import { SENSOR_COLORS } from '../utils/colors';

// ─── Sensor group definitions ─────────────────────────────────────────────
const SENSOR_GROUPS = {
  flex:   { label: 'Flex',   prefix: 'flex', color: '#38bdf8' },
  hall:   { label: 'Hall',   prefix: 'hall', color: '#34d399' },
  accel:  { label: 'Accel',  prefix: 'accel', match: ['ax', 'ay', 'az', 'acc_x', 'acc_y', 'acc_z'], color: '#f87171' },
  gyro:   { label: 'Gyro',   prefix: 'gyro', match: ['gx', 'gy', 'gz', 'gyro_x', 'gyro_y', 'gyro_z'], color: '#fbbf24' },
  orient: { label: 'Orient', match: ['pitch','roll','yaw'], color: '#a78bfa' },
};

export function getSensorGroup(name) {
  for (const [key, g] of Object.entries(SENSOR_GROUPS)) {
    if (g.prefix && name.startsWith(g.prefix)) return key;
    if (g.match && g.match.includes(name)) return key;
  }
  return 'other';
}

// ─── Single channel strip (used by By Group) ─────────────────────────────
function ChannelStrip({ allValues, sensor, colIdx, color, height = 56, sampleBoundaries, totalPts }) {
  const ref = useRef();
  const col = useMemo(() => allValues.map(v => Array.isArray(v) ? v[colIdx] : Number(v)), [allValues, colIdx]);
  const mn  = useMemo(() => Math.min(...col), [col]);
  const mx  = useMemo(() => Math.max(...col), [col]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);
    const rng = mx - mn || 1;
    const zy = H - ((0 - mn) / rng) * H;
    if (zy > 0 && zy < H) {
      ctx.strokeStyle = '#1e293b66'; ctx.lineWidth = 1; ctx.setLineDash([2, 4]);
      ctx.beginPath(); ctx.moveTo(0, zy); ctx.lineTo(W, zy); ctx.stroke();
      ctx.setLineDash([]);
    }
    sampleBoundaries.forEach(b => {
      const x = (b / totalPts) * W;
      ctx.strokeStyle = '#33415566'; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
      ctx.setLineDash([]);
    });
    ctx.strokeStyle = color; ctx.lineWidth = 1.4;
    ctx.beginPath();
    col.forEach((val, i) => {
      const x = (i / Math.max(col.length - 1, 1)) * W;
      const y = H - ((val - mn) / rng) * (H - 4) - 2;
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();
  }, [col, mn, mx, color, sampleBoundaries, totalPts]);

  return (
    <div style={{ display: 'flex', alignItems: 'stretch', borderBottom: '1px solid #0f1e33' }}>
      <div style={{ width: 58, flexShrink: 0, padding: '2px 4px', borderRight: '1px solid #1e293b', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
        <span style={{ fontSize: 9, color, fontFamily: 'monospace', fontWeight: 700 }}>{sensor}</span>
        <span style={{ fontSize: 7, color: '#475569', fontFamily: 'monospace' }}>{mx.toFixed(1)}</span>
        <span style={{ fontSize: 7, color: '#475569', fontFamily: 'monospace' }}>{mn.toFixed(1)}</span>
      </div>
      <canvas ref={ref} width={900} height={height} style={{ flex: 1, height, display: 'block' }} />
    </div>
  );
}

// ─── Group panel ──────────────────────────────────────────────────────────
function GroupPanel({ groupKey, groupDef, sensors, allValues, vis, onToggle, sampleBoundaries, totalPts, showAll, onShowAllToggle }) {
  const theme = useTheme();
  const [collapsed, setCollapsed] = useState(false);
  const groupSensors = useMemo(() => sensors.filter(s => getSensorGroup(s) === groupKey), [sensors, groupKey]);
  if (groupSensors.length === 0) return null;
  const shownSensors = showAll ? groupSensors : groupSensors.filter(s => vis.has(s));
  return (
    <div style={{ background: theme.bgPanel, border: `1px solid ${theme.border}`, borderRadius: 8, marginBottom: 6, overflow: 'hidden' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '5px 10px', background: '#0a1628', cursor: 'pointer' }}
        onClick={() => setCollapsed(c => !c)}>
        <span style={{ color: groupDef.color, fontSize: 10, fontWeight: 700, minWidth: 42 }}>{groupDef.label}</span>
        <span style={{ fontSize: 9, color: '#334155' }}>{groupSensors.length}ch</span>
        <button onClick={e => { e.stopPropagation(); onShowAllToggle(groupKey); }} style={{
          background: showAll ? groupDef.color + '33' : 'transparent',
          border: `1px solid ${showAll ? groupDef.color : '#1e293b'}`,
          color: showAll ? groupDef.color : '#334155',
          borderRadius: 3, padding: '1px 6px', fontSize: 8, cursor: 'pointer', fontFamily: 'monospace',
        }}>all</button>
        <div style={{ flex: 1 }} />
        {!collapsed && groupSensors.map(s => {
          const ci = sensors.indexOf(s);
          return (
            <button key={s} onClick={e => { e.stopPropagation(); onToggle(s); }} style={{
              background: vis.has(s) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] + '33' : 'transparent',
              border: `1px solid ${vis.has(s) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] : '#1e293b'}`,
              color: vis.has(s) ? SENSOR_COLORS[ci % SENSOR_COLORS.length] : '#334155',
              borderRadius: 3, padding: '1px 5px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
            }}>{s}</button>
          );
        })}
        <span style={{ fontSize: 9, color: '#334155', marginLeft: 4 }}>{collapsed ? '▶' : '▼'}</span>
      </div>
      {!collapsed && shownSensors.map(s => {
        const ci = sensors.indexOf(s);
        return (
          <ChannelStrip key={s} allValues={allValues} sensor={s} colIdx={ci}
            color={SENSOR_COLORS[ci % SENSOR_COLORS.length]}
            sampleBoundaries={sampleBoundaries} totalPts={totalPts} />
        );
      })}
    </div>
  );
}

// ─── Combined canvas: SHARED Y axis, real values ─────────────────────────
function CombinedCanvas({ allValues, sensors, vis, sampleBoundaries, totalPts, interval_ms, height = 240 }) {
  const ref = useRef();
  const visList = useMemo(() => [...vis].filter(s => sensors.includes(s)), [vis, sensors]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    const LPAD = 52;
    const BPAD = 16;
    const plotH = H - BPAD;

    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);

    // Compute global min/max across all visible channels
    let gMin = Infinity, gMax = -Infinity;
    visList.forEach(s => {
      const ci = sensors.indexOf(s); if (ci < 0) return;
      allValues.forEach(v => {
        const val = Array.isArray(v) ? v[ci] : Number(v);
        if (val < gMin) gMin = val;
        if (val > gMax) gMax = val;
      });
    });
    if (!isFinite(gMin)) { gMin = 0; gMax = 1; }
    const gRng = gMax - gMin || 1;
    const toY = val => 4 + (plotH - 8) * (1 - (val - gMin) / gRng);

    // Grid lines + Y-axis labels
    const nGrid = 6;
    ctx.font = '9px monospace'; ctx.textAlign = 'right'; ctx.fillStyle = '#334155';
    for (let i = 0; i <= nGrid; i++) {
      const val = gMin + gRng * (i / nGrid);
      const y = toY(val);
      ctx.strokeStyle = '#0f1e33'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(LPAD, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillStyle = '#334155';
      ctx.fillText(val.toFixed(0), LPAD - 3, y + 3);
    }

    // Zero line
    if (gMin < 0 && gMax > 0) {
      const zy = toY(0);
      ctx.strokeStyle = '#2563eb55'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(LPAD, zy); ctx.lineTo(W, zy); ctx.stroke();
      ctx.setLineDash([]);
    }

    // X time ticks
    const totalMs = totalPts * interval_ms;
    const nTicks = 8;
    ctx.textAlign = 'center'; ctx.font = '8px monospace'; ctx.fillStyle = '#334155';
    for (let t = 0; t <= nTicks; t++) {
      const x = LPAD + (t / nTicks) * (W - LPAD);
      ctx.fillText(`${((t / nTicks) * totalMs / 1000).toFixed(1)}s`, x, H - 1);
    }

    // Sample boundaries
    sampleBoundaries.forEach(b => {
      const x = LPAD + (b / totalPts) * (W - LPAD);
      ctx.strokeStyle = '#334155'; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, plotH); ctx.stroke();
      ctx.setLineDash([]);
    });

    // Draw each channel at true scale
    visList.forEach(sensor => {
      const ci = sensors.indexOf(sensor); if (ci < 0) return;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
      ctx.lineWidth = 1.3;
      ctx.beginPath();
      allValues.forEach((v, i) => {
        const val = Array.isArray(v) ? v[ci] : Number(v);
        const x = LPAD + (i / Math.max(totalPts - 1, 1)) * (W - LPAD);
        const y = toY(val);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });
  }, [visList, allValues, sensors, sampleBoundaries, totalPts, interval_ms, height]);

  return <canvas ref={ref} width={900} height={height} style={{ width: '100%', height, borderRadius: 8, display: 'block' }} />;
}

// ─── Overlay canvas: EACH channel normalized to its OWN [min,max] ─────────
// This is the "overlay" view — each channel is plotted 0→1 so you can
// compare shape/timing without scale dominating.  Real min/max shown in legend.
function OverlayNormCanvas({ allValues, sensors, vis, sampleBoundaries, totalPts, interval_ms, height = 220 }) {
  const ref = useRef();
  const visList = useMemo(() => [...vis].filter(s => sensors.includes(s)), [vis, sensors]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    const LPAD = 4;   // no shared Y axis labels — each channel has own scale
    const BPAD = 14;
    const plotH = H - BPAD;

    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);

    // Light grid
    const nGrid = 4;
    for (let i = 0; i <= nGrid; i++) {
      const y = 4 + (plotH - 8) * (1 - i / nGrid);
      ctx.strokeStyle = '#0f1e33'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(LPAD, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillStyle = '#1e293b'; ctx.font = '8px monospace'; ctx.textAlign = 'left';
      ctx.fillText(`${(i / nGrid * 100).toFixed(0)}%`, LPAD + 1, y - 1);
    }

    // X time ticks
    const totalMs = totalPts * interval_ms;
    ctx.textAlign = 'center'; ctx.font = '8px monospace'; ctx.fillStyle = '#334155';
    for (let t = 0; t <= 8; t++) {
      const x = LPAD + (t / 8) * (W - LPAD);
      ctx.fillText(`${((t / 8) * totalMs / 1000).toFixed(1)}s`, x, H - 1);
    }

    // Sample boundaries
    sampleBoundaries.forEach(b => {
      const x = LPAD + (b / totalPts) * (W - LPAD);
      ctx.strokeStyle = '#334155'; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, plotH); ctx.stroke();
      ctx.setLineDash([]);
    });

    // Draw each channel normalized to its own [min, max] → [0, 1]
    visList.forEach(sensor => {
      const ci = sensors.indexOf(sensor); if (ci < 0) return;
      const col = allValues.map(v => Array.isArray(v) ? v[ci] : Number(v));
      const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
      ctx.lineWidth = 1.3;
      ctx.beginPath();
      col.forEach((val, i) => {
        const x = LPAD + (i / Math.max(totalPts - 1, 1)) * (W - LPAD);
        // Normalize: 0% at bottom, 100% at top
        const norm = (val - mn) / rng;
        const y = 4 + (plotH - 8) * (1 - norm);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });
  }, [visList, allValues, sensors, sampleBoundaries, totalPts, interval_ms, height]);

  return <canvas ref={ref} width={900} height={height} style={{ width: '100%', height, borderRadius: 8, display: 'block' }} />;
}

// ─── Shared channel/group toggles row ─────────────────────────────────────
function ChannelToggles({ sensors, vis, onToggle, onToggleGroup, showGroupState }) {
  const theme = useTheme();
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, marginBottom: 8 }}>
      {Object.entries(SENSOR_GROUPS).map(([key, def]) => {
        const gs = sensors.filter(s => getSensorGroup(s) === key);
        if (!gs.length) return null;
        const allOn = gs.every(s => vis.has(s));
        const someOn = gs.some(s => vis.has(s));
        return (
          <button key={key} onClick={() => onToggleGroup(key)} style={{
            background: allOn ? def.color + '33' : someOn ? def.color + '16' : theme.bgPanel,
            border: `2px solid ${allOn ? def.color : someOn ? def.color + '77' : theme.border}`,
            color: allOn || someOn ? def.color : '#475569',
            borderRadius: 4, padding: '2px 9px', fontSize: 9, cursor: 'pointer',
            fontFamily: 'monospace', fontWeight: 700,
          }}>
            {def.label}{someOn && !allOn ? <span style={{ fontSize: 7, marginLeft: 3, opacity: 0.7 }}>~</span> : null}
          </button>
        );
      })}
      <span style={{ borderLeft: `1px solid ${theme.border}`, margin: '0 2px' }} />
      {sensors.map((s, i) => (
        <button key={s} onClick={() => onToggle(s)} style={{
          background: vis.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] + '22' : theme.bgPanel,
          border: `1px solid ${vis.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : theme.border}`,
          color: vis.has(s) ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#334155',
          borderRadius: 3, padding: '1px 5px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
        }}>{s}</button>
      ))}
    </div>
  );
}

// ─── Value legend row ─────────────────────────────────────────────────────
function ValueLegend({ allValues, sensors, vis }) {
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 5 }}>
      {[...vis].filter(s => sensors.includes(s)).map(s => {
        const ci = sensors.indexOf(s);
        const col = allValues.map(v => Array.isArray(v) ? v[ci] : 0);
        const mn = Math.min(...col), mx = Math.max(...col);
        return (
          <span key={s} style={{ fontSize: 9, color: SENSOR_COLORS[ci % SENSOR_COLORS.length], fontFamily: 'monospace' }}>
            {s}:[{mn.toFixed(0)},{mx.toFixed(0)}]
          </span>
        );
      })}
    </div>
  );
}

// ─── Main WaveformViewer ──────────────────────────────────────────────────
const DISPLAY_MODES = [
  { key: 'combined', label: 'Combined', icon: <LayoutDashboard size={11} /> },
  { key: 'grouped',  label: 'By Group',  icon: <Layers size={11} /> },
  { key: 'overlay',  label: 'Overlay',   icon: <GitGraph size={11} /> },
];

export default function WaveformViewer({ samples, sensors }) {
  const theme = useTheme();
  const [vis, setVis]                  = useState(() => new Set(sensors.slice(0, 8)));
  const [displayMode, setDisplayMode]  = useState('combined');
  const [showAllGroups, setShowAllGroups] = useState(new Set());

  const allValues = useMemo(() => samples.flatMap(s => s.values), [samples]);
  const totalPts  = allValues.length;
  const interval_ms = samples[0]?.interval_ms || 33.33;

  const sampleBoundaries = useMemo(() => {
    let acc = 0; const b = [];
    samples.forEach((s, i) => { if (i > 0) b.push(acc); acc += s.values.length; });
    return b;
  }, [samples]);

  const toggleVis = s => setVis(p => { const n = new Set(p); n.has(s) ? n.delete(s) : n.add(s); return n; });
  const toggleGroupAll = key => setShowAllGroups(p => { const n = new Set(p); n.has(key) ? n.delete(key) : n.add(key); return n; });
  const toggleGroupSensors = key => {
    const gs = sensors.filter(s => getSensorGroup(s) === key);
    const allOn = gs.every(s => vis.has(s));
    setVis(p => { const n = new Set(p); gs.forEach(s => allOn ? n.delete(s) : n.add(s)); return n; });
  };

  if (!samples.length) {
    return <div style={{ color: '#1e293b', textAlign: 'center', padding: 60, fontSize: 13 }}>← Click a sample to view its waveform</div>;
  }

  return (
    <div>
      {/* Mode bar */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 10 }}>
        <div style={{ display: 'flex', gap: 3 }}>
          {DISPLAY_MODES.map(m => (
            <button key={m.key} onClick={() => setDisplayMode(m.key)} style={{
              display: 'flex', alignItems: 'center', gap: 5,
              background: displayMode === m.key ? '#0d2040' : theme.bgPanel,
              border: `1px solid ${displayMode === m.key ? '#3b82f6' : theme.border}`,
              color: displayMode === m.key ? '#60a5fa' : theme.textMuted,
              borderRadius: 5, padding: '4px 10px', fontSize: 10, cursor: 'pointer', fontFamily: 'monospace',
            }}>
              {m.icon}
              {m.label}
            </button>
          ))}
        </div>
        <span style={{ fontSize: 9, color: '#334155' }}>
          {totalPts} pts · {(totalPts * interval_ms / 1000).toFixed(2)}s · {samples.length} sample{samples.length !== 1 ? 's' : ''}
        </span>
      </div>

      {/* COMBINED — shared Y axis, real values */}
      {displayMode === 'combined' && (
        <div>
          <ChannelToggles sensors={sensors} vis={vis} onToggle={toggleVis} onToggleGroup={toggleGroupSensors} />
          <CombinedCanvas allValues={allValues} sensors={sensors} vis={vis}
            sampleBoundaries={sampleBoundaries} totalPts={totalPts}
            interval_ms={interval_ms} height={240} />
          <ValueLegend allValues={allValues} sensors={sensors} vis={vis} />
        </div>
      )}

      {/* BY GROUP — collapsible per-group channel strips */}
      {displayMode === 'grouped' && (
        <div>
          {Object.entries(SENSOR_GROUPS).map(([key, def]) => (
            <GroupPanel key={key} groupKey={key} groupDef={def}
              sensors={sensors} allValues={allValues}
              vis={vis} onToggle={toggleVis}
              sampleBoundaries={sampleBoundaries} totalPts={totalPts}
              showAll={showAllGroups.has(key)} onShowAllToggle={toggleGroupAll} />
          ))}
        </div>
      )}

      {/* OVERLAY — each channel normalized to its own [min,max], shape comparison */}
      {displayMode === 'overlay' && (
        <div>
          <ChannelToggles sensors={sensors} vis={vis} onToggle={toggleVis} onToggleGroup={toggleGroupSensors} />
          <div style={{ fontSize: 9, color: '#475569', marginBottom: 5 }}>
            Each channel normalized to its own [min, max] — Y axis shows 0–100% of each channel's range. Good for comparing timing and shape.
          </div>
          <OverlayNormCanvas allValues={allValues} sensors={sensors} vis={vis}
            sampleBoundaries={sampleBoundaries} totalPts={totalPts}
            interval_ms={interval_ms} height={220} />
          <ValueLegend allValues={allValues} sensors={sensors} vis={vis} />
        </div>
      )}
    </div>
  );
}
