import { useRef, useEffect } from 'react';
import { SENSOR_COLORS } from '../utils/colors';

export default function MiniChart({ values, sensors, height = 48, width = 200 }) {
  const ref = useRef();

  useEffect(() => {
    const c = ref.current; if (!c) return;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#060d1a'; ctx.fillRect(0, 0, c.width, c.height);
    if (!values || !values.length || !sensors.length) return;

    // Draw up to 5 channels
    sensors.slice(0, 5).forEach((sensor, si) => {
      const col = values.map(v => Array.isArray(v) ? v[si] : Number(v));
      const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
      ctx.strokeStyle = SENSOR_COLORS[si % SENSOR_COLORS.length] + 'cc';
      ctx.lineWidth = 1;
      ctx.beginPath();
      col.forEach((val, i) => {
        const x = (i / (col.length - 1)) * c.width;
        const y = c.height - ((val - mn) / rng) * (c.height - 4) - 2;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
    });
  }, [values, sensors]);

  return (
    <canvas ref={ref} width={width} height={height}
      style={{ width: '100%', height, display: 'block', borderRadius: 4 }} />
  );
}
