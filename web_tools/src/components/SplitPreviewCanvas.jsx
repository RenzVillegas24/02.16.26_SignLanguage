import { useRef, useEffect, useCallback, useState, useMemo } from 'react';
import { SENSOR_COLORS } from '../utils/colors';
import { Scissors, ArrowLeftRight, Trash2, Plus } from 'lucide-react';

// ─── Shared context menu (right-click) ───────────────────────────────────
function SegmentMenu({ menu, segments, cutPoints, removedSegments, height,
  onToggle, onSplitSeg, onCombineLeft, onCombineRight, onClose }) {
  if (!menu) return null;
  const removed = removedSegments.has(menu.seg);
  const seg = segments[menu.seg];
  const canSplit = seg && Math.round((seg.start + seg.end) / 2) > seg.start &&
                   Math.round((seg.start + seg.end) / 2) < seg.end;
  const menuStyle = {
    position: 'absolute',
    top: Math.min(menu.y + 4, height - 110),
    left: Math.min(Math.max(menu.x - 70, 0), 780),
    background: '#0c1a2e',
    border: '1px solid #2563eb',
    borderRadius: 8,
    boxShadow: '0 8px 32px rgba(0,0,0,0.9)',
    zIndex: 100,
    overflow: 'hidden',
    minWidth: 160,
  };
  const itemStyle = (color = '#f1f5f9') => ({
    display: 'flex', alignItems: 'center', gap: 8,
    background: 'transparent', border: 'none',
    color, textAlign: 'left', padding: '8px 12px',
    fontSize: 11, cursor: 'pointer', width: '100%',
    fontFamily: 'inherit',
    transition: 'background 0.1s',
  });
  return (
    <div style={menuStyle} onClick={e => e.stopPropagation()}>
      <div style={{ fontSize: 9, color: '#475569', padding: '6px 12px 4px', borderBottom: '1px solid #1e293b' }}>
        Segment {menu.seg + 1}
      </div>
      <button style={itemStyle(removed ? '#34d399' : '#f87171')}
        onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
        onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
        onClick={() => { onToggle(menu.seg); onClose(); }}>
        {removed
          ? <><Plus size={12} /> Include segment</>
          : <><Trash2 size={12} /> Remove segment</>}
      </button>
      {!removed && canSplit && (
        <button style={itemStyle('#a78bfa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onSplitSeg?.(menu.seg); onClose(); }}>
          <Scissors size={12} /> Split in half
        </button>
      )}
      {!removed && menu.seg > 0 && (
        <button style={itemStyle('#60a5fa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onCombineLeft?.(menu.seg); onClose(); }}>
          <ArrowLeftRight size={12} /> Merge with left
        </button>
      )}
      {!removed && menu.seg < cutPoints.length && (
        <button style={itemStyle('#60a5fa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onCombineRight?.(menu.seg); onClose(); }}>
          <ArrowLeftRight size={12} /> Merge with right
        </button>
      )}
    </div>
  );
}

// ─── Combined canvas (shared Y axis, EI-style segment boxes) ─────────────
function CombinedCanvas({ values, sensors, interval_ms, cutPoints, removedSegments, padding,
  onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered] = useState(null);
  const [menu, setMenu] = useState(null);
  const N = values.length;
  const LPAD = 50;

  const allCuts  = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
  const segments = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);

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

    const visList = [...(activeSensors || [])];
    let gMin = Infinity, gMax = -Infinity;
    visList.forEach(s => {
      const ci = sensors.indexOf(s); if (ci < 0) return;
      values.forEach(v => { const val = Array.isArray(v) ? v[ci] : Number(v); if (val < gMin) gMin = val; if (val > gMax) gMax = val; });
    });
    if (!isFinite(gMin)) { gMin = -1; gMax = 15000; }
    const gRng = gMax - gMin || 1;
    const toY = val => 4 + (H - 28) * (1 - (val - gMin) / gRng);

    // Grid + Y axis
    const nGrid = 6;
    ctx.font = '9px monospace'; ctx.textAlign = 'right'; ctx.fillStyle = '#334155';
    for (let i = 0; i <= nGrid; i++) {
      const val = gMin + gRng * (i / nGrid);
      const y = toY(val);
      ctx.strokeStyle = '#0f1e33'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(LPAD, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillText(val.toFixed(0), LPAD - 3, y + 3);
    }
    if (gMin < 0 && gMax > 0) {
      const zy = toY(0);
      ctx.strokeStyle = '#2563eb66'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(LPAD, zy); ctx.lineTo(W, zy); ctx.stroke();
      ctx.setLineDash([]);
    }

    // X axis ticks
    ctx.textAlign = 'center'; ctx.font = '8px monospace'; ctx.fillStyle = '#334155';
    const totalMs = N * interval_ms;
    for (let t = 0; t <= 10; t++) {
      ctx.fillText(`${((t / 10) * totalMs / 1000).toFixed(2)}s`, LPAD + (t / 10) * (W - LPAD), H - 2);
    }

    // Segment boxes
    segments.forEach(seg => {
      const x1 = LPAD + (seg.start / N) * (W - LPAD);
      const x2 = LPAD + (seg.end   / N) * (W - LPAD);
      const removed = removedSegments.has(seg.idx);
      const isHov   = hovered === seg.idx;

      // Padding zones
      if (!removed && padding) {
        const avgPad = (Number(padding.min) + Number(padding.max)) / 2;
        const padPts = padding.unit === 'ms' ? avgPad / interval_ms : avgPad;
        const padW = Math.abs((padPts / N) * (W - LPAD));
        ctx.fillStyle = padPts >= 0 ? '#f59e0b18' : '#ef444430';
        ctx.fillRect(x1, 0, padW, H - 18);
        ctx.fillRect(x2 - padW, 0, padW, H - 18);
      }

      // Background
      ctx.fillStyle = removed ? '#450a0a44' : isHov ? '#1d4ed822' : 'transparent';
      if (removed || isHov) ctx.fillRect(x1, 0, x2 - x1, H - 18);

      // Box border
      ctx.strokeStyle = removed ? '#7f1d1daa' : isHov ? '#3b82f6' : '#ffffff44';
      ctx.lineWidth   = isHov ? 2 : 1.5;
      ctx.strokeRect(x1 + 0.5, 2, x2 - x1 - 1, H - 22);

      // Labels
      ctx.fillStyle = removed ? '#f87171' : isHov ? '#60a5fa' : '#ffffff77';
      ctx.font = 'bold 9px monospace'; ctx.textAlign = 'center';
      ctx.fillText(`${seg.idx + 1}`, (x1 + x2) / 2, H - 20);
      ctx.font = '8px monospace'; ctx.fillStyle = '#475569';
      ctx.fillText(`${((seg.end - seg.start) * interval_ms / 1000).toFixed(2)}s`, (x1 + x2) / 2, H - 9);
    });

    // Waveforms
    visList.forEach(sensor => {
      const ci = sensors.indexOf(sensor); if (ci < 0) return;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length];
      ctx.lineWidth = 1.2;
      ctx.beginPath();
      values.forEach((v, i) => {
        const val = Array.isArray(v) ? v[ci] : Number(v);
        const x = LPAD + (i / Math.max(N - 1, 1)) * (W - LPAD);
        i === 0 ? ctx.moveTo(x, toY(val)) : ctx.lineTo(x, toY(val));
      });
      ctx.stroke();
    });

    // Hover tooltip (only when no menu open)
    if (hovered !== null && !menu) {
      const seg = segments[hovered];
      if (seg) {
        const x1 = LPAD + (seg.start / N) * (W - LPAD);
        const x2 = LPAD + (seg.end   / N) * (W - LPAD);
        const cx = (x1 + x2) / 2;
        const removed = removedSegments.has(hovered);
        const lbl = removed ? '+ Add back  |  Right-click for more' : '- Remove  |  Right-click for more';
        ctx.font = '9px monospace';
        const tw = ctx.measureText(lbl).width + 16;
        const tx = Math.min(Math.max(cx - tw / 2, LPAD + 2), W - tw - 2);
        ctx.fillStyle = removed ? '#065f46ee' : '#1e293bee';
        ctx.beginPath();
        if (ctx.roundRect) ctx.roundRect(tx, 5, tw, 18, 4); else ctx.rect(tx, 5, tw, 18);
        ctx.fill();
        ctx.strokeStyle = removed ? '#34d399' : '#475569'; ctx.lineWidth = 1;
        ctx.stroke();
        ctx.fillStyle = removed ? '#34d399' : '#94a3b8'; ctx.textAlign = 'center';
        ctx.fillText(lbl, tx + tw / 2, 17);
        ctx.beginPath();
        ctx.moveTo(cx - 5, 23); ctx.lineTo(cx + 5, 23); ctx.lineTo(cx, 28);
        ctx.fillStyle = removed ? '#065f46ee' : '#1e293bee'; ctx.fill();
      }
    }
  }, [values, sensors, activeSensors, cutPoints, removedSegments, hovered, menu, N, interval_ms, segments, padding]);

  useEffect(() => { draw(); }, [draw]);

  // Close menu on outside click
  useEffect(() => {
    if (!menu) return;
    const h = () => setMenu(null);
    window.addEventListener('click', h);
    return () => window.removeEventListener('click', h);
  }, [menu]);

  const evPos = e => {
    const c = ref.current, r = c.getBoundingClientRect();
    return {
      x: (e.clientX - r.left) * (c.width / r.width),
      offsetX: e.clientX - r.left,
      offsetY: e.clientY - r.top,
    };
  };

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas ref={ref} width={900} height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor: 'pointer' }}
        onMouseMove={e => { if (!menu) setHovered(getSegAt(evPos(e).x, ref.current.width)); }}
        onMouseLeave={() => setHovered(null)}
        onClick={e => {
          e.stopPropagation();
          if (menu) { setMenu(null); return; }
          const seg = getSegAt(evPos(e).x, ref.current.width);
          if (seg !== null) onToggleRemove(seg);
        }}
        onContextMenu={e => {
          e.preventDefault(); e.stopPropagation();
          const p = evPos(e);
          const seg = getSegAt(p.x, ref.current.width);
          if (seg !== null) setMenu({ seg, x: p.offsetX, y: p.offsetY });
        }}
      />
      <SegmentMenu menu={menu} segments={segments} cutPoints={cutPoints}
        removedSegments={removedSegments} height={height}
        onToggle={onToggleRemove} onSplitSeg={onSplitSegment}
        onCombineLeft={onCombineLeft} onCombineRight={onCombineRight}
        onClose={() => setMenu(null)} />
    </div>
  );
}

// ─── Overlay strip canvas (each channel normalized) ───────────────────────
function OverlayStripCanvas({ values, sensors, interval_ms, cutPoints, removedSegments,
  onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered] = useState(null);
  const [menu, setMenu] = useState(null);
  const N = values.length;
  const allCuts  = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
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

    allCuts.slice(0, -1).forEach((start, i) => {
      const end = allCuts[i + 1];
      const x1 = (start / N) * W, x2 = (end / N) * W;
      const isHov = hovered === i;
      ctx.fillStyle = removedSegments.has(i) ? '#450a0a33' : isHov ? '#1d4ed822' : 'transparent';
      if (removedSegments.has(i) || isHov) ctx.fillRect(x1, 0, x2 - x1, H);
      ctx.strokeStyle = removedSegments.has(i) ? '#7f1d1daa' : isHov ? '#3b82f6' : '#ffffff33';
      ctx.lineWidth = isHov ? 1.6 : 1;
      ctx.strokeRect(x1 + 0.5, 1, x2 - x1 - 1, H - 2);
    });

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

  useEffect(() => {
    if (!menu) return;
    const h = () => setMenu(null);
    window.addEventListener('click', h);
    return () => window.removeEventListener('click', h);
  }, [menu]);

  const evPos = e => {
    const c = ref.current, r = c.getBoundingClientRect();
    return { x: (e.clientX - r.left) * (c.width / r.width), offsetX: e.clientX - r.left, offsetY: e.clientY - r.top };
  };

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas ref={ref} width={900} height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor: 'pointer' }}
        onMouseMove={e => { if (!menu) setHovered(getSegAt(evPos(e).x, ref.current.width)); }}
        onMouseLeave={() => setHovered(null)}
        onClick={e => {
          e.stopPropagation();
          if (menu) { setMenu(null); return; }
          const seg = getSegAt(evPos(e).x, ref.current.width);
          if (seg !== null) onToggleRemove(seg);
        }}
        onContextMenu={e => {
          e.preventDefault(); e.stopPropagation();
          const p = evPos(e);
          const seg = getSegAt(p.x, ref.current.width);
          if (seg !== null) setMenu({ seg, x: p.offsetX, y: p.offsetY });
        }}
      />
      <SegmentMenu menu={menu} segments={segments} cutPoints={cutPoints}
        removedSegments={removedSegments} height={height}
        onToggle={onToggleRemove} onSplitSeg={onSplitSegment}
        onCombineLeft={onCombineLeft} onCombineRight={onCombineRight}
        onClose={() => setMenu(null)} />
    </div>
  );
}

// ─── Main exported component ───────────────────────────────────────────────
export default function SplitPreviewCanvas({ values, sensors, interval_ms, cutPoints,
  removedSegments, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment,
  activeSensors, padding, height = 240 }) {
  const [viewMode, setViewMode] = useState('combined');
  const N = values.length;
  const keptCount = useMemo(() => cutPoints.length + 1 - removedSegments.size, [cutPoints, removedSegments]);

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 6 }}>
        {[['combined', 'Combined'], ['overlay', 'Overlay']].map(([m, l]) => (
          <button key={m} onClick={() => setViewMode(m)} style={{
            background: viewMode === m ? '#0d2040' : '#050c1a',
            border: `1px solid ${viewMode === m ? '#3b82f6' : '#1e293b'}`,
            color: viewMode === m ? '#60a5fa' : '#475569',
            borderRadius: 4, padding: '2px 10px', fontSize: 9, cursor: 'pointer', fontFamily: 'monospace',
          }}>{l}</button>
        ))}
        <span style={{ fontSize: 9, color: '#475569', marginLeft: 4 }}>
          {keptCount}/{cutPoints.length + 1} segments kept &middot; click to toggle &middot; right-click for options
        </span>
      </div>

      {viewMode === 'combined' && (
        <CombinedCanvas values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments} padding={padding}
          onToggleRemove={onToggleRemove} onCombineLeft={onCombineLeft}
          onCombineRight={onCombineRight} onSplitSegment={onSplitSegment}
          activeSensors={activeSensors} height={height} />
      )}
      {viewMode === 'overlay' && (
        <OverlayStripCanvas values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments}
          onToggleRemove={onToggleRemove} onCombineLeft={onCombineLeft}
          onCombineRight={onCombineRight} onSplitSegment={onSplitSegment}
          activeSensors={activeSensors} height={height} />
      )}
      <div style={{ fontSize: 9, color: '#334155', marginTop: 3, textAlign: 'right' }}>
        {N} pts · {(N * interval_ms / 1000).toFixed(2)}s total
      </div>
    </div>
  );
}
