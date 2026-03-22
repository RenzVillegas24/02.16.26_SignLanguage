import { useRef, useEffect, useCallback, useState } from 'react';
import { SENSOR_COLORS } from '../utils/colors';
import { clamp, formatMs } from '../utils/parse';

export default function WaveformCutter({
  values, sensors, interval_ms,
  cutPoints, onCutsChange,
  activeSensors,
  height = 200,
}) {
  const canvasRef = useRef();
  const [dragging, setDragging] = useState(null);
  const [hover, setHover] = useState(null);
  const [mouseHoverX, setMouseHoverX] = useState(null);
  const N = values.length;

  const getX = (idx, W) => (N > 1 ? (idx / (N - 1)) * W : 0);
  const getIdx = (x, W) => Math.round(clamp((x / W) * (N - 1), 0, N - 1));

  const draw = useCallback(() => {
    const c = canvasRef.current;
    if (!c) return;
    const ctx = c.getContext('2d');
    const W = c.width, H = c.height;

    ctx.fillStyle = '#050c1a';
    ctx.fillRect(0, 0, W, H);

    // Grid lines
    ctx.strokeStyle = '#0f1e33';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 8; i++) {
      ctx.beginPath();
      ctx.moveTo(0, (i / 8) * H);
      ctx.lineTo(W, (i / 8) * H);
      ctx.stroke();
    }

    // Time axis ticks
    const totalMs = N * interval_ms;
    const nTicks = 8;
    ctx.font = '9px JetBrains Mono, monospace';
    for (let t = 1; t < nTicks; t++) {
      const x = (t / nTicks) * W;
      ctx.strokeStyle = '#1e293b';
      ctx.beginPath();
      ctx.moveTo(x, H - 12);
      ctx.lineTo(x, H);
      ctx.stroke();
      ctx.fillStyle = '#334155';
      ctx.textAlign = 'center';
      ctx.fillText(formatMs((t / nTicks) * totalMs), x, H - 2);
    }

    // Segment background shading
    const allCuts = [0, ...cutPoints, N].sort((a, b) => a - b);
    const segBg = ['#1e3a5f18', '#0d2a1a22', '#2d1b4e18', '#3d151518', '#1a2a0018'];
    allCuts.forEach((c, si) => {
      if (si === allCuts.length - 1) return;
      const x1 = getX(c, W);
      const x2 = getX(allCuts[si + 1], W);
      ctx.fillStyle = segBg[si % segBg.length];
      ctx.fillRect(x1, 0, x2 - x1, H);
    });

    // Sensor waveforms
    (activeSensors || []).forEach(sensor => {
      const ci = sensors.indexOf(sensor);
      if (ci < 0) return;
      const col = values.map(v => (Array.isArray(v) ? v[ci] : Number(v)));
      const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
      ctx.strokeStyle = SENSOR_COLORS[ci % SENSOR_COLORS.length] + 'dd';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      col.forEach((val, i) => {
        const x = getX(i, W);
        const y = (H - 14) - ((val - mn) / rng) * (H - 22);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });

    // Cut handles
    cutPoints.forEach((pt, ci) => {
      const x = getX(pt, W);
      const isDrag = dragging === ci;
      const isHov = hover === ci;
      const col = isDrag ? '#f59e0b' : isHov ? '#60a5fa' : '#3b82f6';

      // Glow
      const gr = ctx.createLinearGradient(x - 20, 0, x + 20, 0);
      gr.addColorStop(0, 'transparent');
      gr.addColorStop(0.5, col + '55');
      gr.addColorStop(1, 'transparent');
      ctx.fillStyle = gr;
      ctx.fillRect(x - 20, 0, 40, H);

      // Line
      ctx.strokeStyle = col;
      ctx.lineWidth = isDrag ? 2.5 : 1.5;
      ctx.setLineDash(isDrag ? [] : [5, 4]);
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
      ctx.setLineDash([]);

      // Top handle
      ctx.fillStyle = col;
      ctx.beginPath();
      if (ctx.roundRect) ctx.roundRect(x - 7, 2, 14, 20, 4);
      else ctx.rect(x - 7, 2, 14, 20);
      ctx.fill();
      ctx.fillStyle = '#f1f5f9';
      ctx.font = 'bold 9px JetBrains Mono, monospace';
      ctx.textAlign = 'center';
      ctx.fillText(ci + 1, x, 16);

      // Bottom handle
      ctx.fillStyle = col;
      ctx.beginPath();
      if (ctx.roundRect) ctx.roundRect(x - 7, H - 22, 14, 20, 4);
      else ctx.rect(x - 7, H - 22, 14, 20);
      ctx.fill();

      // Time label
      ctx.fillStyle = isDrag ? '#f59e0b' : '#64748b';
      ctx.font = '9px JetBrains Mono, monospace';
      ctx.textAlign = x < W * 0.8 ? 'left' : 'right';
      ctx.fillText(
        `${pt}pt · ${formatMs(pt * interval_ms)}`,
        x < W * 0.8 ? x + 9 : x - 9,
        H - 25
      );
    });

    // Hover add cut line
    if (hover === null && dragging === null && mouseHoverX !== null) {
      ctx.strokeStyle = '#38bdf8aa';
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(mouseHoverX, 0);
      ctx.lineTo(mouseHoverX, H);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = '#38bdf8';
      ctx.font = '9px JetBrains Mono, monospace';
      ctx.textAlign = 'center';
      ctx.fillText('[ split ]', mouseHoverX, 40);
    }
  }, [values, sensors, cutPoints, activeSensors, dragging, hover, mouseHoverX, interval_ms, N]);

  useEffect(() => { draw(); }, [draw]);

  const evX = (e) => {
    const c = canvasRef.current;
    const r = c.getBoundingClientRect();
    return (e.clientX - r.left) * (c.width / r.width);
  };

  const hitCut = (x, W) => {
    const tol = Math.max(10, W * 0.015);
    for (let i = 0; i < cutPoints.length; i++) {
      if (Math.abs(getX(cutPoints[i], W) - x) < tol) return i;
    }
    return null;
  };

  const onMouseDown = (e) => {
    const x = evX(e), W = canvasRef.current.width;
    const hit = hitCut(x, W);
    if (hit !== null) { setDragging(hit); return; }
    const idx = getIdx(x, W);
    const minG = Math.max(5, Math.floor(N * 0.02));
    if (!cutPoints.some(c => Math.abs(c - idx) < minG)) {
      onCutsChange([...cutPoints, idx].sort((a, b) => a - b));
    }
  };

  const onMouseMove = (e) => {
    const x = evX(e), W = canvasRef.current.width;
    if (dragging !== null) {
      const idx = getIdx(x, W);
      const minG = Math.max(5, Math.floor(N * 0.02));
      const prev = dragging > 0 ? cutPoints[dragging - 1] : 0;
      const next = dragging < cutPoints.length - 1 ? cutPoints[dragging + 1] : N;
      const nc = [...cutPoints];
      nc[dragging] = clamp(idx, prev + minG, next - minG);
      onCutsChange(nc);
      setMouseHoverX(null);
    } else {
      setHover(hitCut(x, W));
      setMouseHoverX(x);
    }
  };

  const onMouseUp = () => setDragging(null);
  const onMouseLeave = () => { setDragging(null); setHover(null); setMouseHoverX(null); };
  const onDoubleClick = (e) => {
    const x = evX(e), W = canvasRef.current.width;
    const hit = hitCut(x, W);
    if (hit !== null) onCutsChange(cutPoints.filter((_, i) => i !== hit));
  };

  return (
    <div style={{ userSelect: 'none' }}>
      <canvas
        ref={canvasRef}
        width={860}
        height={height}
        style={{
          width: '100%',
          height,
          borderRadius: 8,
          cursor: dragging !== null ? 'ew-resize' : 'crosshair',
          display: 'block',
        }}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={onMouseLeave}
        onDoubleClick={onDoubleClick}
      />
      <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, color: '#334155', marginTop: 3 }}>
        <span>Click → add cut &nbsp;·&nbsp; Drag handle → move &nbsp;·&nbsp; Dbl-click handle → remove</span>
        <span>{cutPoints.length} cut{cutPoints.length !== 1 ? 's' : ''} → {cutPoints.length + 1} segment{cutPoints.length + 1 !== 1 ? 's' : ''}</span>
      </div>
    </div>
  );
}
