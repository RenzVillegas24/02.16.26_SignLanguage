import { useEffect, useMemo, useRef, useState } from 'react';
import { LayoutDashboard, Layers, GitGraph } from 'lucide-react';
import { SENSOR_COLORS } from '../utils/colors';
import { groupSensorsByDiscriminant } from '../utils/flatDetector';
import { getSensorGroup } from './WaveformViewer';

const MODES = [
  { key: 'combined', label: 'Combined', icon: <LayoutDashboard size={10} /> },
  { key: 'grouped',  label: 'By Group',  icon: <Layers size={10} /> },
  { key: 'overlay',  label: 'Overlay',   icon: <GitGraph size={10} /> },
];

export default function SplitHighlightsViewer({
  sample,
  sensors,
  segments = [],
  preferredSensor,
  selectedSensors = [],
  onSelectedSensorsChange,
  activeSegmentSampleId = null,
  onUpdateSegmentRange,
  height = 220,
}) {
  const ref = useRef();
  const [mode, setMode] = useState('combined');
  const [dragEdge, setDragEdge] = useState(null); // 'start' | 'end' | null
  const [hoverEdge, setHoverEdge] = useState(null);
  const N = sample?.values?.length || 0;

  const activeSegIndex = useMemo(() => segments.findIndex(seg => seg.sampleId === activeSegmentSampleId), [segments, activeSegmentSampleId]);
  const hasActiveSegment = activeSegIndex >= 0;
  const activeSeg = hasActiveSegment ? segments[activeSegIndex] : null;

  const displaySensors = useMemo(() => {
    if (!sample?.values?.length || !sensors?.length) return [];
    const valid = (selectedSensors || []).filter(s => sensors.includes(s));
    if (valid.length) return valid;
    if (preferredSensor && sensors.includes(preferredSensor)) return [preferredSensor];
    return [sensors[0]];
  }, [sample, sensors, selectedSensors, preferredSensor]);

  const LPAD = 48;
  const BPAD = 16;

  const xToIdx = (x, W) => {
    const r = Math.max(0, Math.min(1, (x - LPAD) / Math.max(1, W - LPAD)));
    return Math.round(r * Math.max(1, N - 1));
  };

  const idxToX = (idx, W) => LPAD + (idx / Math.max(1, N)) * (W - LPAD);

  const getMinMaxForSensor = (sensor) => {
    const ci = sensors.indexOf(sensor);
    if (ci < 0) return { mn: 0, mx: 1, rng: 1 };
    let mn = Infinity;
    let mx = -Infinity;
    sample.values.forEach(v => {
      const val = Array.isArray(v) ? Number(v[ci] ?? 0) : Number(v);
      if (val < mn) mn = val;
      if (val > mx) mx = val;
    });
    if (!isFinite(mn) || !isFinite(mx)) return { mn: 0, mx: 1, rng: 1 };
    return { mn, mx, rng: mx - mn || 1 };
  };

  const updateActiveRange = (edge, pt) => {
    if (!activeSeg || !onUpdateSegmentRange) return;
    const sorted = [...segments].sort((a, b) => a.start - b.start);
    const idx = sorted.findIndex(s => s.sampleId === activeSeg.sampleId);
    if (idx < 0) return;
    const prevEnd = idx > 0 ? Number(sorted[idx - 1].end) : 0;
    const nextStart = idx < sorted.length - 1 ? Number(sorted[idx + 1].start) : N;

    const curStart = Number(activeSeg.start);
    const curEnd = Number(activeSeg.end);
    let nextStartVal = curStart;
    let nextEndVal = curEnd;

    if (edge === 'start') {
      nextStartVal = Math.max(prevEnd + 1, Math.min(pt, curEnd - 1));
    } else if (edge === 'end') {
      nextEndVal = Math.min(nextStart - 1, Math.max(pt, curStart + 1));
    }

    if (nextStartVal !== curStart || nextEndVal !== curEnd) {
      onUpdateSegmentRange(activeSeg.sampleId, nextStartVal, nextEndVal);
    }
  };

  useEffect(() => {
    const c = ref.current;
    if (!c || !sample || !N || !displaySensors.length) return;
    const ctx = c.getContext('2d');
    const W = c.width;
    const H = c.height;
    const plotH = H - BPAD;

    ctx.fillStyle = '#060d1a';
    ctx.fillRect(0, 0, W, H);

    // Highlights first
    segments.forEach((seg, i) => {
      const x1 = idxToX(Math.max(0, Number(seg.start) || 0), W);
      const x2 = idxToX(Math.min(N, Number(seg.end) || 0), W);
      const color = SENSOR_COLORS[i % SENSOR_COLORS.length];
      const isActive = seg.sampleId === activeSegmentSampleId;
      const dimOthers = hasActiveSegment && !isActive;

      ctx.fillStyle = dimOthers ? color + '10' : isActive ? color + '66' : color + '2a';
      ctx.fillRect(x1, 0, Math.max(1, x2 - x1), plotH);
      ctx.strokeStyle = dimOthers ? color + '44' : isActive ? color + 'ff' : color + 'aa';
      ctx.lineWidth = isActive ? 2.4 : 1.1;
      ctx.strokeRect(x1 + 0.5, 1, Math.max(1, x2 - x1 - 1), plotH - 2);

      ctx.font = isActive ? 'bold 10px monospace' : '9px monospace';
      ctx.fillStyle = dimOthers ? '#64748b' : color;
      ctx.textAlign = 'center';
      ctx.fillText(`#${i + 1}`, (x1 + x2) / 2, 12);
    });

    // Draw data by mode
    if (mode === 'grouped') {
      const lanes = Math.max(1, displaySensors.length);
      const laneH = plotH / lanes;
      displaySensors.forEach((s, si) => {
        const ci = sensors.indexOf(s);
        if (ci < 0) return;
        const { mn, rng } = getMinMaxForSensor(s);
        const yTop = si * laneH;

        ctx.strokeStyle = '#1e293b';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(LPAD, yTop);
        ctx.lineTo(W, yTop);
        ctx.stroke();

        ctx.fillStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
        ctx.font = '9px monospace';
        ctx.textAlign = 'left';
        ctx.fillText(s, 4, yTop + 12);

        ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        sample.values.forEach((v, i) => {
          const val = Array.isArray(v) ? Number(v[ci] ?? 0) : Number(v);
          const x = LPAD + (i / Math.max(N - 1, 1)) * (W - LPAD);
          const y = yTop + 4 + (laneH - 8) * (1 - (val - mn) / rng);
          i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
      });
    } else {
      let gMin = Infinity;
      let gMax = -Infinity;
      displaySensors.forEach(s => {
        const ci = sensors.indexOf(s);
        if (ci < 0) return;
        sample.values.forEach(v => {
          const val = Array.isArray(v) ? Number(v[ci] ?? 0) : Number(v);
          if (val < gMin) gMin = val;
          if (val > gMax) gMax = val;
        });
      });
      if (!isFinite(gMin) || !isFinite(gMax)) {
        gMin = 0;
        gMax = 1;
      }
      const gRng = gMax - gMin || 1;

      for (let i = 0; i <= 4; i++) {
        const y = 4 + (plotH - 8) * (1 - i / 4);
        ctx.strokeStyle = '#0f1e33';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(LPAD, y);
        ctx.lineTo(W, y);
        ctx.stroke();
      }

      displaySensors.forEach(s => {
        const ci = sensors.indexOf(s);
        if (ci < 0) return;
        const { mn, rng } = getMinMaxForSensor(s);
        ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
        ctx.lineWidth = 1.25;
        ctx.beginPath();
        sample.values.forEach((v, i) => {
          const raw = Array.isArray(v) ? Number(v[ci] ?? 0) : Number(v);
          const val = mode === 'overlay' ? (raw - mn) / rng : (raw - gMin) / gRng;
          const x = LPAD + (i / Math.max(N - 1, 1)) * (W - LPAD);
          const y = 4 + (plotH - 8) * (1 - val);
          i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        });
        ctx.stroke();
      });
    }

    // Active drag handles
    if (activeSeg) {
      const x1 = idxToX(Number(activeSeg.start) || 0, W);
      const x2 = idxToX(Number(activeSeg.end) || 0, W);
      const handleColor = '#fbbf24';
      [
        { x: x1, edge: 'start' },
        { x: x2, edge: 'end' },
      ].forEach(h => {
        const hot = dragEdge === h.edge || hoverEdge === h.edge;
        ctx.fillStyle = hot ? '#fde68a' : handleColor;
        ctx.strokeStyle = '#7c2d12';
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        ctx.arc(h.x, 9, hot ? 5 : 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
      });
    }

    // X ticks
    ctx.textAlign = 'center';
    ctx.fillStyle = '#334155';
    ctx.font = '8px monospace';
    for (let t = 0; t <= 8; t++) {
      const x = LPAD + (t / 8) * (W - LPAD);
      const pt = Math.round((t / 8) * N);
      ctx.fillText(`${pt}`, x, H - 2);
    }
  }, [sample, sensors, displaySensors, segments, mode, N, activeSegmentSampleId, hasActiveSegment, hoverEdge, dragEdge]);

  if (!sample || !N || !displaySensors.length) return null;

  return (
    <div style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 8, padding: 10 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6, flexWrap: 'wrap' }}>
        <div style={{ fontSize: 10, color: '#64748b' }}>
          Base sample with split highlights
          {hasActiveSegment && <span style={{ color: '#fbbf24' }}> · drag yellow handles to crop active split</span>}
        </div>
        <div style={{ marginLeft: 'auto', display: 'flex', gap: 4, flexWrap: 'wrap' }}>
          {MODES.map(m => (
            <button
              key={m.key}
              onClick={() => setMode(m.key)}
              style={{
                display: 'flex', alignItems: 'center', gap: 4,
                background: mode === m.key ? '#0d2040' : '#060d1a',
                border: `1px solid ${mode === m.key ? '#3b82f6' : '#1e293b'}`,
                color: mode === m.key ? '#60a5fa' : '#64748b',
                borderRadius: 4,
                padding: '3px 8px',
                fontSize: 9,
                cursor: 'pointer',
                fontFamily: 'inherit',
              }}
            >
              {m.icon}
              {m.label}
            </button>
          ))}
        </div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 5, marginBottom: 6 }}>
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
            const next = new Set(displaySensors);
            const allSelected = valid.every(ch => next.has(ch));
            valid.forEach(ch => (allSelected ? next.delete(ch) : next.add(ch)));
            onSelectedSensorsChange?.([...next]);
          };

          const selectDiscriminantGroup = (groupSensors) => {
            const valid = (groupSensors || []).filter(ch => sensors.includes(ch));
            if (!valid.length) return;
            onSelectedSensorsChange?.(valid);
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
                      background: '#060d1a',
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
              <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
                <span style={{ fontSize: 8, color: '#64748b' }}>Discriminant:</span>
                {groups.map(group => (
                  <button
                    key={`disc-all-${group.key}`}
                    onClick={() => selectDiscriminantGroup(group.sensors)}
                    style={{
                      background: '#060d1a',
                      border: `1px solid ${group.color}`,
                      color: group.color,
                      borderRadius: 4,
                      padding: '2px 8px',
                      fontSize: 8,
                      cursor: 'pointer',
                      fontFamily: 'inherit',
                      fontWeight: 700,
                    }}
                    title={`Select all channels in ${group.label}`}
                  >
                    All {group.label.replace(/^.*\s/, '')}
                  </button>
                ))}
              </div>
              <div style={{ height: 1, background: '#1e293b', margin: '1px 0 2px' }} />
              {groups.map(group => (
                <div key={group.key} style={{ display: 'flex', alignItems: 'center', gap: 2, flexWrap: 'wrap' }}>
                  <span style={{ fontSize: 8, fontWeight: 700, color: group.color, minWidth: 58 }}>
                    {group.label}
                  </span>
                  <button
                    onClick={() => selectDiscriminantGroup(group.sensors)}
                    style={{
                      background: '#060d1a',
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
                      const i = sensors.indexOf(s);
                      const selected = displaySensors.includes(s);
                      return (
                        <button
                          key={s}
                          onClick={() => {
                            const next = new Set(displaySensors);
                            if (selected) {
                              if (next.size <= 1) return;
                              next.delete(s);
                            } else {
                              next.add(s);
                            }
                            onSelectedSensorsChange?.([...next]);
                          }}
                          style={{
                            background: selected ? SENSOR_COLORS[i % SENSOR_COLORS.length] + '2a' : '#060d1a',
                            border: `1px solid ${selected ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#1e293b'}`,
                            color: selected ? SENSOR_COLORS[i % SENSOR_COLORS.length] : '#64748b',
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

      <canvas
        ref={ref}
        width={950}
        height={height}
        style={{ width: '100%', height, borderRadius: 6, display: 'block', cursor: dragEdge ? 'ew-resize' : 'default' }}
        onMouseMove={(e) => {
          const c = ref.current;
          if (!c || !activeSeg) return;
          const r = c.getBoundingClientRect();
          const W = c.width;
          const x = (e.clientX - r.left) * (W / r.width);

          const sx = idxToX(Number(activeSeg.start) || 0, W);
          const ex = idxToX(Number(activeSeg.end) || 0, W);
          const nearStart = Math.abs(x - sx) <= 8;
          const nearEnd = Math.abs(x - ex) <= 8;
          const edge = nearStart ? 'start' : nearEnd ? 'end' : null;
          setHoverEdge(edge);

          if (dragEdge) {
            const idx = xToIdx(x, W);
            updateActiveRange(dragEdge, idx);
          }
        }}
        onMouseDown={(e) => {
          const c = ref.current;
          if (!c || !activeSeg) return;
          const r = c.getBoundingClientRect();
          const W = c.width;
          const x = (e.clientX - r.left) * (W / r.width);
          const sx = idxToX(Number(activeSeg.start) || 0, W);
          const ex = idxToX(Number(activeSeg.end) || 0, W);
          if (Math.abs(x - sx) <= 8) setDragEdge('start');
          else if (Math.abs(x - ex) <= 8) setDragEdge('end');
        }}
        onMouseUp={() => setDragEdge(null)}
        onMouseLeave={() => { setHoverEdge(null); setDragEdge(null); }}
      />
    </div>
  );
}
