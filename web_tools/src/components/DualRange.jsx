import { useRef } from 'react';

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

export default function DualRange({ value, onChange }) {
  const [lo, hi] = value;
  const ref = useRef();

  const drag = (which) => (e) => {
    e.preventDefault();
    const move = (ev) => {
      const r = ref.current.getBoundingClientRect();
      const p = clamp(((ev.clientX - r.left) / r.width) * 100, 0, 100);
      if (which === 0) onChange([Math.min(p, hi - 2), hi]);
      else onChange([lo, Math.max(p, lo + 2)]);
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  };

  return (
    <div
      ref={ref}
      style={{ position: 'relative', height: 18, display: 'flex', alignItems: 'center', padding: '0 7px' }}
    >
      <div style={{ position: 'absolute', left: 7, right: 7, height: 3, background: '#1e293b', borderRadius: 2 }} />
      <div
        style={{
          position: 'absolute',
          left: `calc(7px + ${lo}% * ((100% - 14px) / 100))`,
          right: `calc(7px + ${100 - hi}% * ((100% - 14px) / 100))`,
          height: 3,
          background: '#3b82f6',
          borderRadius: 2,
        }}
      />
      {[lo, hi].map((v, i) => (
        <div
          key={i}
          onMouseDown={drag(i)}
          style={{
            position: 'absolute',
            left: `calc(7px + ${v}% * ((100% - 14px) / 100))`,
            transform: 'translateX(-50%)',
            width: 13,
            height: 13,
            background: '#3b82f6',
            borderRadius: '50%',
            cursor: 'ew-resize',
            border: '2px solid #060d1a',
            boxSizing: 'border-box',
          }}
        />
      ))}
    </div>
  );
}
