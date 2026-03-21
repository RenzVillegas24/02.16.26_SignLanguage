import { useRef, useEffect, useCallback, useState, useMemo } from 'react';
import { SENSOR_COLORS } from '../utils/colors';
import { getSensorGroup } from './WaveformViewer';
import { groupSensorsByDiscriminant } from '../utils/flatDetector';

const SENSOR_GROUPS_COLOR = {
  flex: '#38bdf8', hall: '#34d399', accel: '#f87171',
  gyro: '#fbbf24', orient: '#a78bfa', other: '#94a3b8',
};

// ─── EI-style combined waveform with segment boxes ─────────────────────────
function CombinedCanvas({ values, sensors, interval_ms, cutPoints, removedSegments, padding, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered] = useState(null);
  const [menu, setMenu] = useState(null);
  const N = values.length;

  const allCuts = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
  const segments = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);

  const LPAD = 50;

  const getSegAt = useCallback((x, W) => {
    if (x < LPAD) return null;
    const pct = (x - LPAD) / (W - LPAD);
    const ptIdx = Math.round(pct * (N - 1));
    return segments.find(s => ptIdx >= s.start && ptIdx < s.end)?.idx ?? null;
  }, [segments, N]);

  const draw = useCallback(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#0d1a2e'; ctx.fillRect(0, 0, W, H);

    // Y axis from active sensors
    const visList = [...(activeSensors || [])];
    let gMin = Infinity, gMax = -Infinity;
    visList.forEach(s => {
      const ci = sensors.indexOf(s); if (ci < 0) return;
      values.forEach(v => {
        const val = Array.isArray(v) ? v[ci] : Number(v);
        if (val < gMin) gMin = val;
        if (val > gMax) gMax = val;
      });
    });
    if (gMin === Infinity) { gMin = -1; gMax = 15000; }
    const gRng = gMax - gMin || 1;
    const toY = val => 4 + (H - 28) * (1 - (val - gMin) / gRng);

    // Grid + Y labels
    const nGrid = 6;
    ctx.font = '9px monospace'; ctx.textAlign = 'right'; ctx.fillStyle = '#334155';
    for (let i = 0; i <= nGrid; i++) {
      const val = gMin + gRng * (i / nGrid);
      const y = toY(val);
      ctx.strokeStyle = '#0f1e33'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(LPAD, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillText(val.toFixed(0), LPAD - 3, y + 3);
    }
    // Zero line
    if (gMin < 0 && gMax > 0) {
      const zy = toY(0);
      ctx.strokeStyle = '#2563eb66'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(LPAD, zy); ctx.lineTo(W, zy); ctx.stroke();
      ctx.setLineDash([]);
    }
    // X axis time labels
    const totalMs = N * interval_ms;
    const nTicks = 10;
    ctx.textAlign = 'center'; ctx.font = '8px monospace'; ctx.fillStyle = '#334155';
    for (let t = 0; t <= nTicks; t++) {
      const x = LPAD + (t / nTicks) * (W - LPAD);
      ctx.fillText(`${((t / nTicks) * totalMs / 1000).toFixed(2)}s`, x, H - 2);
    }

    // Segment backgrounds + padding visualization
    segments.forEach(seg => {
      const x1 = LPAD + (seg.start / N) * (W - LPAD);
      const x2 = LPAD + (seg.end   / N) * (W - LPAD);
      const removed  = removedSegments.has(seg.idx);
      const isHov    = hovered === seg.idx;

      // Padding shading (show where padding would be added)
      if (!removed && padding) {
        const pMin = Number(padding.min) || 0;
        const pMax = Number(padding.max) || 0;
        const avgPad = (pMin + pMax) / 2;
        const padPts = padding.unit === 'ms' ? avgPad / interval_ms : avgPad;
        const padW = Math.abs((padPts / N) * (W - LPAD));
        if (padPts >= 0) {
          ctx.fillStyle = '#f59e0b18';
          ctx.fillRect(x1, 0, padW, H - 18);
          ctx.fillRect(x2 - padW, 0, padW, H - 18);
        } else {
          // Negative padding (shrinks the kept segment, show red where it cuts)
          ctx.fillStyle = '#ef444444';
          ctx.fillRect(x1, 0, padW, H - 18);
          ctx.fillRect(x2 - padW, 0, padW, H - 18);
        }
      }

      // Segment background
      if (removed) {
        ctx.fillStyle = '#450a0a44';
        ctx.fillRect(x1, 0, x2 - x1, H - 18);
      } else if (isHov) {
        ctx.fillStyle = '#1d4ed822';
        ctx.fillRect(x1, 0, x2 - x1, H - 18);
      }

      // White bounding box (EI style)
      ctx.strokeStyle = removed ? '#7f1d1daa' : isHov ? '#3b82f6' : '#ffffff44';
      ctx.lineWidth = removed ? 1 : isHov ? 2 : 1.5;
      ctx.strokeRect(x1 + 0.5, 2, x2 - x1 - 1, H - 22);

      // Bottom labels
      ctx.fillStyle = removed ? '#f87171' : isHov ? '#60a5fa' : '#ffffff77';
      ctx.font = 'bold 9px monospace'; ctx.textAlign = 'center';
      ctx.fillText(`${seg.idx + 1}`, (x1 + x2) / 2, H - 20);
      const ms = (seg.end - seg.start) * interval_ms;
      ctx.font = '8px monospace'; ctx.fillStyle = '#475569';
      ctx.fillText(`${(ms / 1000).toFixed(2)}s`, (x1 + x2) / 2, H - 9);
    });

    // Waveforms drawn OVER the segment boxes
    visList.forEach(sensor => {
      const ci = sensors.indexOf(sensor); if (ci < 0) return;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      values.forEach((v, i) => {
        const val = Array.isArray(v) ? v[ci] : Number(v);
        const x = LPAD + (i / Math.max(N - 1, 1)) * (W - LPAD);
        const y = toY(val);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });

    // Hover pill tooltip
    if (hovered !== null && !menu) {
      const seg = segments[hovered];
      if (seg) {
        const x1 = LPAD + (seg.start / N) * (W - LPAD);
        const x2 = LPAD + (seg.end   / N) * (W - LPAD);
        const cx = (x1 + x2) / 2;
        const removed = removedSegments.has(hovered);
        const lbl = removed ? '＋ Add back (Ctrl for more)' : '✕ Remove (Ctrl for more)';
        ctx.font = 'bold 9px monospace';
        const tw = ctx.measureText(lbl).width + 16;
        const tx = Math.min(Math.max(cx - tw / 2, LPAD + 2), W - tw - 2);
        ctx.fillStyle = removed ? '#065f46ee' : '#dc2626ee';
        ctx.beginPath();
        if (ctx.roundRect) ctx.roundRect(tx, 5, tw, 18, 4);
        else ctx.rect(tx, 5, tw, 18);
        ctx.fill();
        ctx.fillStyle = '#fff'; ctx.textAlign = 'center';
        ctx.fillText(lbl, tx + tw / 2, 17);
        // Arrow
        ctx.beginPath();
        ctx.moveTo(cx - 5, 23); ctx.lineTo(cx + 5, 23); ctx.lineTo(cx, 28);
        ctx.fillStyle = removed ? '#065f46ee' : '#dc2626ee';
        ctx.fill();
      }
    }
  }, [values, sensors, activeSensors, cutPoints, removedSegments, hovered, menu, N, interval_ms, segments, padding]);

  useEffect(() => { draw(); }, [draw]);

  const evX = e => {
    const c = ref.current, r = c.getBoundingClientRect();
    return (e.clientX - r.left) * (c.width / r.width);
  };

  const closeMenu = () => setMenu(null);

  useEffect(() => {
    if (!menu) return;
    const clickOut = () => closeMenu();
    window.addEventListener('click', clickOut);
    return () => window.removeEventListener('click', clickOut);
  }, [menu]);

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas ref={ref} width={900} height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor: 'pointer' }}
        onMouseMove={e => { if (!menu) setHovered(getSegAt(evX(e), ref.current.width)); }}
        onMouseLeave={() => setHovered(null)}
        onClick={e => {
          e.stopPropagation();
          const seg = getSegAt(evX(e), ref.current.width);
          if (seg === null) { closeMenu(); return; }
          if (e.ctrlKey || e.metaKey) {
            setMenu({ x: e.nativeEvent.offsetX || e.clientX, y: e.nativeEvent.offsetY || e.clientY, seg });
          } else {
            closeMenu();
            onToggleRemove(seg);
          }
        }}
      />
      {menu && (
        <div onClick={e => e.stopPropagation()} style={{
          position: 'absolute', top: Math.min(menu.y, height - 80), left: Math.min(menu.x, 800),
          background: '#0a1628', border: '1px solid #3b82f6', borderRadius: 6,
          boxShadow: '0 8px 32px rgba(0,0,0,0.8)', zIndex: 50, padding: 5, display: 'flex', flexDirection: 'column', gap: 2, minWidth: 140
        }}>
          <div style={{ fontSize: 9, color: '#64748b', padding: '2px 6px 4px', borderBottom: '1px solid #1e293b', marginBottom: 2 }}>
            Segment {menu.seg + 1} Options
          </div>
          <button onClick={() => { onToggleRemove(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#f1f5f9', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>
            {removedSegments.has(menu.seg) ? '＋ Include Segment' : '✕ Exclude Segment'}
          </button>
          {!removedSegments.has(menu.seg) && (
            <>
              {(() => {
                const seg = segments[menu.seg];
                if (!seg) return null;
                const mid = Math.round((seg.start + seg.end) / 2);
                const canSplit = mid > seg.start && mid < seg.end;
                if (!canSplit) return null;
                return (
                  <button onClick={() => { onSplitSegment?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#a78bfa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>
                    ✂ Split Segment
                  </button>
                );
              })()}
              {menu.seg > 0 && <button onClick={() => { onCombineLeft?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#60a5fa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>⬱ Combine with Left</button>}
              {menu.seg < cutPoints.length && <button onClick={() => { onCombineRight?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#60a5fa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>⇲ Combine with Right</button>}
            </>
          )}
        </div>
      )}
    </div>
  );
}

// ─── Per-channel overlay strips (normalized) in split view ─────────────────
function OverlayStripCanvas({ values, sensors, interval_ms, cutPoints, removedSegments, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered] = useState(null);
  const [menu, setMenu] = useState(null);
  const N = values.length;
  const allCuts = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
  const segments = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);

  const getSegAt = useCallback((x, W) => {
    const pct = x / W;
    const ptIdx = Math.round(pct * (N - 1));
    return segments.find(s => ptIdx >= s.start && ptIdx < s.end)?.idx ?? null;
  }, [segments, N]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);
    // Segment shading
    allCuts.slice(0, -1).forEach((start, i) => {
      const end = allCuts[i + 1];
      const x1 = (start / N) * W, x2 = (end / N) * W;
      const isHov = hovered === i;
      if (removedSegments.has(i)) {
        ctx.fillStyle = '#450a0a33'; ctx.fillRect(x1, 0, x2 - x1, H);
      } else if (isHov) {
        ctx.fillStyle = '#1d4ed822'; ctx.fillRect(x1, 0, x2 - x1, H);
      }
      ctx.strokeStyle = removedSegments.has(i) ? '#7f1d1daa' : isHov ? '#3b82f6' : '#ffffff33';
      ctx.lineWidth = isHov ? 1.6 : 1;
      ctx.strokeRect(x1 + 0.5, 1, x2 - x1 - 1, H - 2);
    });
    // Channels (each normalized to its own range)
    ;[...(activeSensors || [])].forEach(sensor => {
      const ci = sensors.indexOf(sensor); if (ci < 0) return;
      const col = values.map(v => Array.isArray(v) ? v[ci] : Number(v));
      const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length] + 'cc';
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      col.forEach((val, i) => {
        const x = (i / Math.max(N - 1, 1)) * W;
        const y = H - ((val - mn) / rng) * (H - 4) - 2;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });
  }, [values, sensors, activeSensors, allCuts, removedSegments, hovered, N]);

  const evX = e => {
    const c = ref.current, r = c.getBoundingClientRect();
    return (e.clientX - r.left) * (c.width / r.width);
  };

  const closeMenu = () => setMenu(null);

  useEffect(() => {
    if (!menu) return;
    const clickOut = () => closeMenu();
    window.addEventListener('click', clickOut);
    return () => window.removeEventListener('click', clickOut);
  }, [menu]);

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas
        ref={ref}
        width={900}
        height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor: 'pointer' }}
        onMouseMove={e => { if (!menu) setHovered(getSegAt(evX(e), ref.current.width)); }}
        onMouseLeave={() => setHovered(null)}
        onClick={e => {
          e.stopPropagation();
          const seg = getSegAt(evX(e), ref.current.width);
          if (seg === null) { closeMenu(); return; }
          if (e.ctrlKey || e.metaKey) {
            setMenu({ x: e.nativeEvent.offsetX || e.clientX, y: e.nativeEvent.offsetY || e.clientY, seg });
          } else {
            closeMenu();
            onToggleRemove(seg);
          }
        }}
      />
      {menu && (
        <div onClick={e => e.stopPropagation()} style={{
          position: 'absolute', top: Math.min(menu.y, height - 80), left: Math.min(menu.x, 800),
          background: '#0a1628', border: '1px solid #3b82f6', borderRadius: 6,
          boxShadow: '0 8px 32px rgba(0,0,0,0.8)', zIndex: 50, padding: 5, display: 'flex', flexDirection: 'column', gap: 2, minWidth: 140
        }}>
          <div style={{ fontSize: 9, color: '#64748b', padding: '2px 6px 4px', borderBottom: '1px solid #1e293b', marginBottom: 2 }}>
            Segment {menu.seg + 1} Options
          </div>
          <button onClick={() => { onToggleRemove(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#f1f5f9', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>
            {removedSegments.has(menu.seg) ? '＋ Include Segment' : '✕ Exclude Segment'}
          </button>
          {!removedSegments.has(menu.seg) && (
            <>
              {(() => {
                const seg = segments[menu.seg];
                if (!seg) return null;
                const mid = Math.round((seg.start + seg.end) / 2);
                const canSplit = mid > seg.start && mid < seg.end;
                if (!canSplit) return null;
                return (
                  <button onClick={() => { onSplitSegment?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#a78bfa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>
                    ✂ Split Segment
                  </button>
                );
              })()}
              {menu.seg > 0 && <button onClick={() => { onCombineLeft?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#60a5fa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>⬱ Combine with Left</button>}
              {menu.seg < cutPoints.length && <button onClick={() => { onCombineRight?.(menu.seg); closeMenu(); }} style={{ background: 'transparent', border: 'none', color: '#60a5fa', textAlign: 'left', padding: '6px 8px', fontSize: 10, cursor: 'pointer', borderRadius: 4 }}>⇲ Combine with Right</button>}
            </>
          )}
        </div>
      )}
    </div>
  );
}

// ─── Main exported component ───────────────────────────────────────────────
export default function SplitPreviewCanvas({ values, sensors, interval_ms, cutPoints, removedSegments, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, activeSensors, padding, height = 240 }) {
  const [viewMode, setViewMode] = useState('combined'); // 'combined' | 'overlay'
  const N = values.length;
  const keptCount = useMemo(() => {
    const total = cutPoints.length + 1;
    return total - removedSegments.size;
  }, [cutPoints, removedSegments]);

  // Show which discriminant levels are represented in active sensors
  const activeLevels = useMemo(() => {
    if (!activeSensors || !activeSensors.length) return new Set();
    const grouped = groupSensorsByDiscriminant(sensors);
    const levels = new Set();
    activeSensors.forEach(s => {
      if (grouped.high.includes(s)) levels.add('high');
      if (grouped.medium.includes(s)) levels.add('medium');
      if (grouped.low.includes(s)) levels.add('low');
    });
    return levels;
  }, [activeSensors, sensors]);

  return (
    <div>
      {/* View mode toggle */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 6 }}>
        {[['combined', '📈 Combined'], ['overlay', '⊕ Overlay']].map(([m, l]) => (
          <button key={m} onClick={() => setViewMode(m)} style={{
            background: viewMode === m ? '#0d2040' : '#050c1a',
            border: `1px solid ${viewMode === m ? '#3b82f6' : '#1e293b'}`,
            color: viewMode === m ? '#60a5fa' : '#475569',
            borderRadius: 4, padding: '2px 10px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
          }}>{l}</button>
        ))}
        <span style={{ fontSize: 9, color: '#475569', marginLeft: 4 }}>
          {keptCount}/{cutPoints.length + 1} segments kept · click to remove/restore
        </span>
              {/* Discriminant levels indicator */}
              <div style={{ marginLeft: 'auto', display: 'flex', gap: 4, alignItems: 'center' }}>
                <span style={{ fontSize: 8, color: '#64748b' }}>Discriminant:</span>
                {activeLevels.has('high') && <span style={{ fontSize: 8, fontWeight: 700, color: '#ef4444' }}>🔴 HIGH</span>}
                {activeLevels.has('medium') && <span style={{ fontSize: 8, fontWeight: 700, color: '#f59e0b' }}>🟡 MED</span>}
                {activeLevels.has('low') && <span style={{ fontSize: 8, fontWeight: 700, color: '#10b981' }}>🟢 LOW</span>}
              </div>
      </div>

      {viewMode === 'combined' && (
        <CombinedCanvas
          values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments} padding={padding}
          onToggleRemove={onToggleRemove} 
          onCombineLeft={onCombineLeft} onCombineRight={onCombineRight}
          onSplitSegment={onSplitSegment}
          activeSensors={activeSensors} height={height}
        />
      )}
      {viewMode === 'overlay' && (
        <OverlayStripCanvas
          values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments}
          onToggleRemove={onToggleRemove}
          onCombineLeft={onCombineLeft}
          onCombineRight={onCombineRight}
          onSplitSegment={onSplitSegment}
          activeSensors={activeSensors} height={height}
        />
      )}
      <div style={{ fontSize: 9, color: '#334155', marginTop: 3, textAlign: 'right' }}>
        {N} pts · {(N * interval_ms / 1000).toFixed(2)}s total
      </div>
    </div>
  );
}
