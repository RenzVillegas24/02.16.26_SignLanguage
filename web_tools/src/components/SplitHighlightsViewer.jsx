import { useEffect, useMemo, useRef } from 'react';
import { SENSOR_COLORS } from '../utils/colors';

export default function SplitHighlightsViewer({ sample, sensors, segments = [], preferredSensor, height = 180 }) {
  const ref = useRef();
  const N = sample?.values?.length || 0;

  const colIdx = useMemo(() => {
    if (!sample || !sample.values?.length || !sensors?.length) return -1;
    if (preferredSensor) {
      const forced = sensors.indexOf(preferredSensor);
      if (forced >= 0) return forced;
    }
    return 0;
  }, [sample, sensors, preferredSensor]);

  useEffect(() => {
    const c = ref.current;
    if (!c || !sample || colIdx < 0 || !N) return;
    const ctx = c.getContext('2d');
    const W = c.width;
    const H = c.height;
    const LPAD = 40;

    ctx.fillStyle = '#060d1a';
    ctx.fillRect(0, 0, W, H);

    // Segment highlights
    segments.forEach((seg, i) => {
      const x1 = LPAD + (Math.max(0, seg.start) / N) * (W - LPAD);
      const x2 = LPAD + (Math.min(N, seg.end) / N) * (W - LPAD);
      const color = SENSOR_COLORS[i % SENSOR_COLORS.length];
      ctx.fillStyle = color + '22';
      ctx.fillRect(x1, 0, x2 - x1, H - 16);
      ctx.strokeStyle = color + 'aa';
      ctx.lineWidth = 1.2;
      ctx.strokeRect(x1 + 0.5, 1, Math.max(1, x2 - x1 - 1), H - 18);
      ctx.font = '9px monospace';
      ctx.fillStyle = color;
      ctx.textAlign = 'center';
      ctx.fillText(`#${i + 1}`, (x1 + x2) / 2, 12);
    });

    const col = sample.values.map(v => (Array.isArray(v) ? Number(v[colIdx] ?? 0) : Number(v)));
    const mn = Math.min(...col);
    const mx = Math.max(...col);
    const rng = mx - mn || 1;

    const toY = val => 4 + (H - 26) * (1 - (val - mn) / rng);

    // Y ticks
    ctx.font = '8px monospace';
    ctx.textAlign = 'right';
    ctx.fillStyle = '#334155';
    for (let i = 0; i <= 4; i++) {
      const val = mn + (rng * i) / 4;
      const y = toY(val);
      ctx.strokeStyle = '#0f1e33';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(LPAD, y);
      ctx.lineTo(W, y);
      ctx.stroke();
      ctx.fillText(val.toFixed(0), LPAD - 3, y + 3);
    }

    // waveform
    ctx.strokeStyle = '#60a5fa';
    ctx.lineWidth = 1.4;
    ctx.beginPath();
    col.forEach((val, i) => {
      const x = LPAD + (i / Math.max(N - 1, 1)) * (W - LPAD);
      const y = toY(val);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // X ticks (pts)
    ctx.textAlign = 'center';
    ctx.fillStyle = '#334155';
    for (let t = 0; t <= 8; t++) {
      const x = LPAD + (t / 8) * (W - LPAD);
      const pt = Math.round((t / 8) * N);
      ctx.fillText(`${pt}`, x, H - 2);
    }
  }, [sample, segments, colIdx, N]);

  if (!sample || colIdx < 0 || !N) return null;

  return (
    <div style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 8, padding: 10 }}>
      <div style={{ fontSize: 10, color: '#64748b', marginBottom: 6 }}>
        Base sample with split highlights · channel <span style={{ color: '#60a5fa' }}>{sensors[colIdx]}</span>
      </div>
      <canvas ref={ref} width={900} height={height} style={{ width: '100%', height, borderRadius: 6, display: 'block' }} />
    </div>
  );
}
