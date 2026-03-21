import { useEffect, useState } from 'react';

// Animated spinner using pure CSS/SVG — no emoji
function Spinner({ size = 36, color = '#38bdf8' }) {
  return (
    <svg width={size} height={size} viewBox="0 0 36 36" fill="none"
      style={{ animation: 'spin 0.8s linear infinite' }}>
      <style>{`@keyframes spin { to { transform: rotate(360deg); } }`}</style>
      <circle cx="18" cy="18" r="14" stroke={color + '33'} strokeWidth="3" />
      <path d="M18 4 A14 14 0 0 1 32 18" stroke={color} strokeWidth="3" strokeLinecap="round" />
    </svg>
  );
}

// Animated progress bar
function ProgressBar({ value, color = '#38bdf8' }) {
  return (
    <div style={{ width: 280, height: 4, background: '#1e293b', borderRadius: 2, overflow: 'hidden' }}>
      <div style={{
        height: '100%',
        width: `${Math.min(100, Math.max(0, value))}%`,
        background: `linear-gradient(90deg, ${color}, ${color}cc)`,
        borderRadius: 2,
        transition: 'width 0.3s ease',
      }} />
    </div>
  );
}

export default function LoadingOverlay({ loading }) {
  const [dots, setDots] = useState('');
  const [progress, setProgress] = useState(0);

  useEffect(() => {
    if (!loading) { setProgress(0); return; }
    // Animated dots
    const dotTimer = setInterval(() => setDots(d => d.length >= 3 ? '' : d + '.'), 400);
    // Simulate progress (indeterminate — advances toward 90 then waits)
    const progTimer = setInterval(() => {
      setProgress(p => {
        if (p >= 90) return p + (100 - p) * 0.01; // slow crawl near end
        return p + (90 - p) * 0.08;
      });
    }, 120);
    return () => { clearInterval(dotTimer); clearInterval(progTimer); };
  }, [loading]);

  // When loading finishes, snap to 100 then fade
  useEffect(() => {
    if (!loading) setProgress(100);
  }, [loading]);

  if (!loading && progress >= 100) return null;

  return (
    <div style={{
      position: 'fixed', inset: 0, zIndex: 9000,
      background: '#060d1add',
      backdropFilter: 'blur(6px)',
      display: 'flex', flexDirection: 'column',
      alignItems: 'center', justifyContent: 'center',
      gap: 20,
      opacity: !loading ? 0 : 1,
      transition: 'opacity 0.3s ease',
      pointerEvents: loading ? 'all' : 'none',
    }}>
      <Spinner size={48} color="#38bdf8" />
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 8 }}>
        <span style={{ fontSize: 14, fontWeight: 700, color: '#f1f5f9', fontFamily: 'inherit' }}>
          {loading?.label || 'Loading'}{dots}
        </span>
        {loading?.sub && (
          <span style={{ fontSize: 11, color: '#475569', fontFamily: 'inherit' }}>{loading.sub}</span>
        )}
        <ProgressBar value={progress} color="#38bdf8" />
        {typeof loading?.count === 'number' && typeof loading?.total === 'number' && loading.total > 0 && (
          <span style={{ fontSize: 10, color: '#334155', fontFamily: 'monospace' }}>
            {loading.count} / {loading.total}
          </span>
        )}
      </div>
    </div>
  );
}
