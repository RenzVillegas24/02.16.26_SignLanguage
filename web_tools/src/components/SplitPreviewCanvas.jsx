import { useRef, useEffect, useCallback, useState, useMemo } from 'react';
import { SENSOR_COLORS } from '../utils/colors';
import { Scissors, ArrowLeftRight, Trash2, Plus } from 'lucide-react';

// ─── Right-click context menu ──────────────────────────────────────────────
function SegmentMenu({ menu, segments, cutPoints, removedSegments, height,
  onToggle, onSplitSeg, onCombineLeft, onCombineRight, onClose }) {
  if (!menu) return null;
  const removed = removedSegments.has(menu.seg);
  const seg = segments[menu.seg];
  const canSplit = seg && Math.round((seg.start + seg.end) / 2) > seg.start &&
                   Math.round((seg.start + seg.end) / 2) < seg.end;
  const s = (color = '#f1f5f9') => ({
    display: 'flex', alignItems: 'center', gap: 8,
    background: 'transparent', border: 'none',
    color, textAlign: 'left', padding: '8px 12px',
    fontSize: 11, cursor: 'pointer', width: '100%', fontFamily: 'inherit',
  });
  return (
    <div style={{
      position: 'absolute',
      top: Math.min(menu.y + 4, height - 120),
      left: Math.min(Math.max(menu.x - 70, 0), 780),
      background: '#0c1a2e', border: '1px solid #2563eb',
      borderRadius: 8, boxShadow: '0 8px 32px rgba(0,0,0,0.9)',
      zIndex: 100, overflow: 'hidden', minWidth: 160,
    }} onClick={e => e.stopPropagation()}>
      <div style={{ fontSize: 9, color: '#475569', padding: '6px 12px 4px', borderBottom: '1px solid #1e293b' }}>
        Segment {menu.seg + 1}
      </div>
      <button style={s(removed ? '#34d399' : '#f87171')}
        onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
        onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
        onClick={() => { onToggle(menu.seg); onClose(); }}>
        {removed ? <><Plus size={12} /> Include</> : <><Trash2 size={12} /> Remove</>}
      </button>
      {!removed && canSplit && (
        <button style={s('#a78bfa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onSplitSeg?.(menu.seg); onClose(); }}>
          <Scissors size={12} /> Split in half
        </button>
      )}
      {!removed && menu.seg > 0 && (
        <button style={s('#60a5fa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onCombineLeft?.(menu.seg); onClose(); }}>
          <ArrowLeftRight size={12} /> Merge with left
        </button>
      )}
      {!removed && menu.seg < cutPoints.length && (
        <button style={s('#60a5fa')}
          onMouseEnter={e => e.currentTarget.style.background = '#1e293b'}
          onMouseLeave={e => e.currentTarget.style.background = 'transparent'}
          onClick={() => { onCombineRight?.(menu.seg); onClose(); }}>
          <ArrowLeftRight size={12} /> Merge with right
        </button>
      )}
    </div>
  );
}

// ─── Combined canvas: shared Y axis, draggable cut lines ──────────────────
function CombinedCanvas({ values, sensors, interval_ms, cutPoints, removedSegments, padding,
  onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, onCutsChange, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered]   = useState(null);   // hovered segment idx
  const [menu, setMenu]         = useState(null);
  const [dragCut, setDragCut]   = useState(null);   // { cutIdx, x }  — currently dragging cut line
  const [hovCut, setHovCut]     = useState(null);   // hovered cut line index (for cursor)
  const N = values.length;
  const LPAD = 50;

  const allCuts  = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
  const segments = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);

  const pxToIdx = useCallback((x, W) => Math.round(((x - LPAD) / (W - LPAD)) * (N - 1)), [N]);
  const idxToPx = useCallback((idx, W) => LPAD + (idx / Math.max(N - 1, 1)) * (W - LPAD), [N]);

  // Find which cut line is near a given x (within 8px)
  const getCutNear = useCallback((x, W) => {
    for (let ci = 0; ci < cutPoints.length; ci++) {
      const cx = idxToPx(cutPoints[ci], W);
      if (Math.abs(x - cx) < 8) return ci;
    }
    return null;
  }, [cutPoints, idxToPx]);

  const getSegAt = useCallback((x, W) => {
    if (x < LPAD) return null;
    const ptIdx = pxToIdx(x, W);
    return segments.find(s => ptIdx >= s.start && ptIdx < s.end)?.idx ?? null;
  }, [segments, pxToIdx]);

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
    for (let i = 0; i <= 6; i++) {
      const val = gMin + gRng * (i / 6);
      const y = toY(val);
      ctx.strokeStyle = '#0f1e33'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(LPAD, y); ctx.lineTo(W, y); ctx.stroke();
      ctx.fillStyle = '#334155'; ctx.font = '9px monospace'; ctx.textAlign = 'right';
      ctx.fillText(val.toFixed(0), LPAD - 3, y + 3);
    }
    if (gMin < 0 && gMax > 0) {
      const zy = toY(0);
      ctx.strokeStyle = '#2563eb66'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
      ctx.beginPath(); ctx.moveTo(LPAD, zy); ctx.lineTo(W, zy); ctx.stroke();
      ctx.setLineDash([]);
    }
    // X ticks
    ctx.textAlign = 'center'; ctx.font = '8px monospace'; ctx.fillStyle = '#334155';
    const totalMs = N * interval_ms;
    for (let t = 0; t <= 10; t++) {
      ctx.fillText(`${((t / 10) * totalMs / 1000).toFixed(2)}s`, LPAD + (t / 10) * (W - LPAD), H - 2);
    }

    // Segment boxes
    segments.forEach(seg => {
      const x1 = idxToPx(seg.start, W);
      const x2 = idxToPx(seg.end, W);
      const removed = removedSegments.has(seg.idx);
      const isHov   = hovered === seg.idx && dragCut === null;

      // Padding zones
      if (!removed && padding) {
        const toPts = v => padding.unit === 'ms' ? v / interval_ms : v;
        const padMin = Math.max(0, toPts(Math.min(Number(padding.min), Number(padding.max))));
        const padMax = Math.max(0, toPts(Math.max(Number(padding.min), Number(padding.max))));
        const wMin = (padMin / N) * (W - LPAD);
        const wMax = (padMax / N) * (W - LPAD);
        if (wMax > wMin) { ctx.fillStyle = '#f59e0b0c'; ctx.fillRect(x1 - wMax, 0, wMax, H - 18); ctx.fillRect(x2, 0, wMax, H - 18); }
        ctx.fillStyle = '#f59e0b28';
        ctx.fillRect(x1 - wMin, 0, wMin, H - 18);
        ctx.fillRect(x2, 0, wMin, H - 18);
        if (wMax > 0) {
          ctx.strokeStyle = '#f59e0b55'; ctx.lineWidth = 1; ctx.setLineDash([2, 3]);
          ctx.beginPath(); ctx.rect(x1 - wMax + 0.5, 1, wMax - 1, H - 20); ctx.stroke();
          ctx.beginPath(); ctx.rect(x2 + 0.5, 1, wMax - 1, H - 20); ctx.stroke();
          ctx.setLineDash([]);
        }
      }

      ctx.fillStyle = removed ? '#450a0a44' : isHov ? '#1d4ed822' : 'transparent';
      if (removed || isHov) ctx.fillRect(x1, 0, x2 - x1, H - 18);
      ctx.strokeStyle = removed ? '#7f1d1daa' : isHov ? '#3b82f6' : '#ffffff44';
      ctx.lineWidth = isHov ? 2 : 1.5;
      ctx.strokeRect(x1 + 0.5, 2, x2 - x1 - 1, H - 22);
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

    // Cut lines — drawn on top; highlight hovered/dragging one
    cutPoints.forEach((cutIdx, ci) => {
      const x = idxToPx(cutIdx, W);
      const isDragging = dragCut?.cutIdx === ci;
      const isHoveredCut = hovCut === ci && dragCut === null;
      ctx.strokeStyle = isDragging ? '#f59e0b' : isHoveredCut ? '#60a5fa' : '#ffffff55';
      ctx.lineWidth = isDragging ? 2.5 : isHoveredCut ? 2 : 1;
      ctx.setLineDash(isDragging ? [] : []);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H - 18); ctx.stroke();
      // Drag handle
      const hw = isDragging ? 9 : isHoveredCut ? 8 : 6;
      const hh = 14;
      const hy = (H - 18) / 2 - hh / 2;
      ctx.fillStyle = isDragging ? '#f59e0b' : isHoveredCut ? '#3b82f6' : '#334155';
      ctx.beginPath();
      if (ctx.roundRect) ctx.roundRect(x - hw / 2, hy, hw, hh, 3); else ctx.rect(x - hw / 2, hy, hw, hh);
      ctx.fill();
      // ↔ arrow in handle
      ctx.fillStyle = '#fff'; ctx.font = `${isDragging ? 9 : 8}px monospace`; ctx.textAlign = 'center';
      ctx.fillText('⇿', x, hy + hh - 3);
    });

    // Hover tooltip for segments
    if (hovered !== null && !menu && dragCut === null && hovCut === null) {
      const seg = segments[hovered];
      if (seg) {
        const x1 = idxToPx(seg.start, W), x2 = idxToPx(seg.end, W);
        const cx = (x1 + x2) / 2;
        const removed = removedSegments.has(hovered);
        const lbl = removed ? '+ Add back  |  Right-click more' : '- Remove  |  Right-click more';
        ctx.font = '9px monospace';
        const tw = ctx.measureText(lbl).width + 16;
        const tx = Math.min(Math.max(cx - tw / 2, LPAD + 2), W - tw - 2);
        ctx.fillStyle = removed ? '#065f46ee' : '#1e293bee';
        ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(tx, 5, tw, 18, 4); else ctx.rect(tx, 5, tw, 18); ctx.fill();
        ctx.strokeStyle = removed ? '#34d399' : '#475569'; ctx.lineWidth = 1; ctx.stroke();
        ctx.fillStyle = removed ? '#34d399' : '#94a3b8'; ctx.textAlign = 'center';
        ctx.fillText(lbl, tx + tw / 2, 17);
      }
    }

    // Drag cursor indicator
    if (dragCut !== null) {
      const x = idxToPx(cutPoints[dragCut.cutIdx], W);
      const ptIdx = cutPoints[dragCut.cutIdx];
      const ms = ptIdx * interval_ms;
      ctx.fillStyle = '#f59e0bcc'; ctx.font = 'bold 9px monospace'; ctx.textAlign = 'center';
      const lbl = `${(ms / 1000).toFixed(3)}s (pt ${ptIdx})`;
      const tw = ctx.measureText(lbl).width + 10;
      ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x - tw / 2, H - 36, tw, 14, 3); else ctx.rect(x - tw / 2, H - 36, tw, 14);
      ctx.fill();
      ctx.fillStyle = '#000'; ctx.fillText(lbl, x, H - 25);
    }
  }, [values, sensors, activeSensors, cutPoints, removedSegments, hovered, hovCut, dragCut, menu, N, interval_ms, segments, padding, idxToPx]);

  useEffect(() => { draw(); }, [draw]);

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

  const onMouseMove = useCallback(e => {
    const { x } = evPos(e);
    const W = ref.current.width;

    if (dragCut !== null) {
      // Move the dragged cut
      const newIdx = Math.max(1, Math.min(N - 1, pxToIdx(x, W)));
      // Don't cross adjacent cuts
      const prevCut = dragCut.cutIdx > 0 ? cutPoints[dragCut.cutIdx - 1] + 1 : 1;
      const nextCut = dragCut.cutIdx < cutPoints.length - 1 ? cutPoints[dragCut.cutIdx + 1] - 1 : N - 1;
      const clamped = Math.max(prevCut, Math.min(nextCut, newIdx));
      const newCuts = [...cutPoints];
      newCuts[dragCut.cutIdx] = clamped;
      onCutsChange?.(newCuts.sort((a, b) => a - b));
      return;
    }

    const nc = getCutNear(x, W);
    setHovCut(nc);
    if (nc === null && !menu) setHovered(getSegAt(x, W));
    else setHovered(null);
  }, [dragCut, cutPoints, getCutNear, getSegAt, pxToIdx, N, menu, onCutsChange]);

  const onMouseDown = useCallback(e => {
    const { x } = evPos(e);
    const W = ref.current.width;
    const nc = getCutNear(x, W);
    if (nc !== null) {
      e.preventDefault();
      setDragCut({ cutIdx: nc });
      setMenu(null);
    }
  }, [getCutNear]);

  const onMouseUp = useCallback(() => {
    setDragCut(null);
  }, []);

  useEffect(() => {
    window.addEventListener('mouseup', onMouseUp);
    return () => window.removeEventListener('mouseup', onMouseUp);
  }, [onMouseUp]);

  const cursor = dragCut !== null ? 'ew-resize' : hovCut !== null ? 'ew-resize' : 'pointer';

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas ref={ref} width={900} height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor }}
        onMouseMove={onMouseMove}
        onMouseDown={onMouseDown}
        onMouseLeave={() => { setHovered(null); setHovCut(null); }}
        onClick={e => {
          e.stopPropagation();
          if (menu) { setMenu(null); return; }
          if (dragCut !== null || hovCut !== null) return; // was dragging
          const seg = getSegAt(evPos(e).x, ref.current.width);
          if (seg !== null) onToggleRemove(seg);
        }}
        onContextMenu={e => {
          e.preventDefault(); e.stopPropagation();
          if (dragCut !== null) return;
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

// ─── Overlay strip canvas ─────────────────────────────────────────────────
function OverlayStripCanvas({ values, sensors, interval_ms, cutPoints, removedSegments,
  onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, onCutsChange, activeSensors, height }) {
  const ref = useRef();
  const [hovered, setHovered]   = useState(null);
  const [menu, setMenu]         = useState(null);
  const [dragCut, setDragCut]   = useState(null);
  const [hovCut, setHovCut]     = useState(null);
  const N = values.length;
  const allCuts  = useMemo(() => [0, ...cutPoints, N].sort((a, b) => a - b), [cutPoints, N]);
  const segments = useMemo(() => allCuts.slice(0, -1).map((s, i) => ({ start: s, end: allCuts[i + 1], idx: i })), [allCuts]);

  const pxToIdx = useCallback((x, W) => Math.round((x / W) * (N - 1)), [N]);
  const idxToPx = useCallback((idx, W) => (idx / Math.max(N - 1, 1)) * W, [N]);

  const getCutNear = useCallback((x, W) => {
    for (let ci = 0; ci < cutPoints.length; ci++) {
      const cx = idxToPx(cutPoints[ci], W);
      if (Math.abs(x - cx) < 8) return ci;
    }
    return null;
  }, [cutPoints, idxToPx]);

  const getSegAt = useCallback((x, W) => {
    const ptIdx = pxToIdx(x, W);
    return segments.find(s => ptIdx >= s.start && ptIdx < s.end)?.idx ?? null;
  }, [segments, pxToIdx]);

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, W, H);

    allCuts.slice(0, -1).forEach((start, i) => {
      const end = allCuts[i + 1];
      const x1 = idxToPx(start, W), x2 = idxToPx(end, W);
      const isHov = hovered === i && dragCut === null;
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
        const x = idxToPx(i, W);
        const y = H - ((val - mn) / rng) * (H - 4) - 2;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });

    // Cut lines
    cutPoints.forEach((cutIdx, ci) => {
      const x = idxToPx(cutIdx, W);
      const isDragging = dragCut?.cutIdx === ci;
      const isHovCut = hovCut === ci && dragCut === null;
      ctx.strokeStyle = isDragging ? '#f59e0b' : isHovCut ? '#60a5fa' : '#ffffff55';
      ctx.lineWidth = isDragging ? 2.5 : isHovCut ? 2 : 1;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
      const hw = 6, hh = 12, hy = H / 2 - 6;
      ctx.fillStyle = isDragging ? '#f59e0b' : isHovCut ? '#3b82f6' : '#334155';
      ctx.fillRect(x - hw / 2, hy, hw, hh);
    });
  }, [values, sensors, activeSensors, allCuts, removedSegments, hovered, hovCut, dragCut, N, cutPoints, idxToPx]);

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

  const onMouseMove = useCallback(e => {
    const { x } = evPos(e);
    const W = ref.current.width;
    if (dragCut !== null) {
      const newIdx = Math.max(1, Math.min(N - 1, pxToIdx(x, W)));
      const prevCut = dragCut.cutIdx > 0 ? cutPoints[dragCut.cutIdx - 1] + 1 : 1;
      const nextCut = dragCut.cutIdx < cutPoints.length - 1 ? cutPoints[dragCut.cutIdx + 1] - 1 : N - 1;
      const newCuts = [...cutPoints];
      newCuts[dragCut.cutIdx] = Math.max(prevCut, Math.min(nextCut, newIdx));
      onCutsChange?.(newCuts.sort((a, b) => a - b));
      return;
    }
    const nc = getCutNear(x, W);
    setHovCut(nc);
    if (nc === null && !menu) setHovered(getSegAt(x, W)); else setHovered(null);
  }, [dragCut, cutPoints, getCutNear, getSegAt, pxToIdx, N, menu, onCutsChange]);

  const onMouseDown = useCallback(e => {
    const { x } = evPos(e);
    const nc = getCutNear(x, ref.current.width);
    if (nc !== null) { e.preventDefault(); setDragCut({ cutIdx: nc }); setMenu(null); }
  }, [getCutNear]);

  useEffect(() => {
    const up = () => setDragCut(null);
    window.addEventListener('mouseup', up);
    return () => window.removeEventListener('mouseup', up);
  }, []);

  return (
    <div style={{ userSelect: 'none', position: 'relative' }}>
      <canvas ref={ref} width={900} height={height}
        style={{ width: '100%', height, borderRadius: 8, display: 'block', cursor: dragCut !== null || hovCut !== null ? 'ew-resize' : 'pointer' }}
        onMouseMove={onMouseMove}
        onMouseDown={onMouseDown}
        onMouseLeave={() => { setHovered(null); setHovCut(null); }}
        onClick={e => {
          e.stopPropagation();
          if (menu) { setMenu(null); return; }
          if (dragCut !== null || hovCut !== null) return;
          const seg = getSegAt(evPos(e).x, ref.current.width);
          if (seg !== null) onToggleRemove(seg);
        }}
        onContextMenu={e => {
          e.preventDefault(); e.stopPropagation();
          if (dragCut !== null) return;
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
  removedSegments, onToggleRemove, onCombineLeft, onCombineRight, onSplitSegment, onCutsChange,
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
          {keptCount}/{cutPoints.length + 1} kept &middot; click=toggle &middot; drag cut line=move &middot; right-click=options
        </span>
      </div>

      {viewMode === 'combined' && (
        <CombinedCanvas values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments} padding={padding}
          onToggleRemove={onToggleRemove} onCombineLeft={onCombineLeft}
          onCombineRight={onCombineRight} onSplitSegment={onSplitSegment}
          onCutsChange={onCutsChange}
          activeSensors={activeSensors} height={height} />
      )}
      {viewMode === 'overlay' && (
        <OverlayStripCanvas values={values} sensors={sensors} interval_ms={interval_ms}
          cutPoints={cutPoints} removedSegments={removedSegments}
          onToggleRemove={onToggleRemove} onCombineLeft={onCombineLeft}
          onCombineRight={onCombineRight} onSplitSegment={onSplitSegment}
          onCutsChange={onCutsChange}
          activeSensors={activeSensors} height={height} />
      )}
      <div style={{ fontSize: 9, color: '#334155', marginTop: 3, textAlign: 'right' }}>
        {N} pts · {(N * interval_ms / 1000).toFixed(2)}s
      </div>
    </div>
  );
}
