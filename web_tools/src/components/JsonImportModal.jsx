import { useState, useEffect, useCallback } from 'react';
import { FileJson, X, Plus, Check, Tag, Upload, ChevronDown, ChevronUp, Trash2 } from 'lucide-react';
import { parseEIJson, uid, formatMs } from '../utils/parse';
import { CATEGORY_COLORS } from '../utils/colors';

// ─── Mini waveform preview ─────────────────────────────────────────────────
function MiniWave({ values, color = '#38bdf8', height = 36 }) {
  if (!values?.length) return null;
  const N = values.length;
  const nCh = Array.isArray(values[0]) ? values[0].length : 1;
  // Use first channel only for the mini preview
  const col = values.map(v => Array.isArray(v) ? v[0] : Number(v));
  const mn = Math.min(...col), mx = Math.max(...col), rng = mx - mn || 1;
  const pts = col.map((v, i) => {
    const x = (i / Math.max(N - 1, 1)) * 100;
    const y = height - ((v - mn) / rng) * (height - 2) - 1;
    return `${x},${y}`;
  }).join(' ');
  return (
    <svg width="100%" height={height} style={{ display: 'block' }} preserveAspectRatio="none" viewBox={`0 0 100 ${height}`}>
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.2" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}

// ─── Single sample row ─────────────────────────────────────────────────────
function SampleRow({ item, allLabels, onChange, onRemove }) {
  const [expanded, setExpanded] = useState(false);
  const { parsed, label, category, override } = item;

  return (
    <div style={{
      background: '#060d1a',
      border: `1px solid ${override ? '#2563eb44' : '#1e293b'}`,
      borderRadius: 8,
      overflow: 'hidden',
      transition: 'border-color 0.15s',
    }}>
      {/* Main row */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '7px 10px' }}>
        {/* Waveform mini preview */}
        <div style={{ width: 64, height: 36, background: '#0a1628', borderRadius: 4, overflow: 'hidden', flexShrink: 0 }}>
          <MiniWave values={parsed.values} color="#38bdf8" height={36} />
        </div>

        {/* Filename + meta */}
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 10, fontWeight: 700, color: '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
            {parsed.filename}
          </div>
          <div style={{ fontSize: 9, color: '#334155' }}>
            {parsed.values.length} pts · {formatMs(parsed.duration_ms)} · {parsed.sensors.length} ch
          </div>
        </div>

        {/* Label input */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 2, minWidth: 110 }}>
          <span style={{ fontSize: 8, color: '#475569' }}>Label</span>
          <input
            value={label}
            onChange={e => onChange({ label: e.target.value })}
            list={`labels-list-${item.id}`}
            placeholder="e.g. idle"
            style={{
              background: '#0a1628', color: '#f1f5f9',
              border: `1px solid ${label !== parsed.label ? '#3b82f6' : '#1e293b'}`,
              borderRadius: 4, padding: '3px 7px', fontSize: 10,
              fontFamily: 'inherit', width: '100%', boxSizing: 'border-box',
            }}
          />
          <datalist id={`labels-list-${item.id}`}>
            {allLabels.map(l => <option key={l} value={l} />)}
          </datalist>
        </div>

        {/* Category toggle */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
          <span style={{ fontSize: 8, color: '#475569' }}>Category</span>
          <div style={{ display: 'flex', gap: 2 }}>
            {['training', 'testing'].map(c => (
              <button key={c} onClick={() => onChange({ category: c })} style={{
                background: category === c ? (c === 'training' ? '#0d204088' : '#451a0388') : '#0a1628',
                border: `1px solid ${category === c ? CATEGORY_COLORS[c] : '#1e293b'}`,
                color: category === c ? CATEGORY_COLORS[c] : '#475569',
                borderRadius: 3, padding: '2px 6px', cursor: 'pointer',
                fontSize: 8, fontFamily: 'inherit', fontWeight: category === c ? 700 : 400,
              }}>
                {c === 'training' ? 'Train' : 'Test'}
              </button>
            ))}
          </div>
        </div>

        {/* Expand / remove */}
        <button onClick={() => setExpanded(v => !v)}
          style={{ background: 'none', border: 'none', color: '#475569', cursor: 'pointer', padding: 3, display: 'flex', alignItems: 'center' }}>
          {expanded ? <ChevronUp size={13} /> : <ChevronDown size={13} />}
        </button>
        <button onClick={onRemove}
          style={{ background: 'none', border: 'none', color: '#475569', cursor: 'pointer', padding: 3, display: 'flex', alignItems: 'center' }}
          title="Remove this file from import">
          <X size={13} />
        </button>
      </div>

      {/* Expanded details */}
      {expanded && (
        <div style={{ padding: '6px 10px 10px', borderTop: '1px solid #0f1e33', background: '#050c1a' }}>
          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap', fontSize: 9, color: '#475569', marginBottom: 8 }}>
            <span>Sensors: <b style={{ color: '#60a5fa' }}>{parsed.sensors.join(', ')}</b></span>
            <span>Interval: <b style={{ color: '#60a5fa' }}>{parsed.interval_ms.toFixed(2)} ms</b></span>
            <span>Points: <b style={{ color: '#60a5fa' }}>{parsed.values.length}</b></span>
            <span>Original label: <b style={{ color: '#fbbf24' }}>{parsed.label}</b></span>
          </div>
          {/* Sample name */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontSize: 9, color: '#475569', minWidth: 80 }}>Sample name:</span>
            <input
              value={item.sampleName}
              onChange={e => onChange({ sampleName: e.target.value })}
              placeholder="optional name"
              style={{ background: '#0a1628', color: '#94a3b8', border: '1px solid #1e293b', borderRadius: 4, padding: '3px 7px', fontSize: 9, fontFamily: 'inherit', flex: 1 }}
            />
          </div>
        </div>
      )}
    </div>
  );
}

// ─── Main modal ────────────────────────────────────────────────────────────
export default function JsonImportModal({ files, existingSamples, onImport, onClose }) {
  const [items, setItems] = useState([]);
  const [bulkLabel, setBulkLabel] = useState('');
  const [bulkCategory, setBulkCategory] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  // Existing label suggestions
  const allLabels = [...new Set(existingSamples.map(s => s.label))].sort();

  // Parse files on mount
  useEffect(() => {
    let cancelled = false;
    async function parse() {
      const results = [];
      for (const file of files) {
        try {
          const text = await file.text();
          const parsed = parseEIJson(text, file.name);
          if (parsed && parsed.values?.length > 0) {
            results.push({
              id: uid(),
              parsed,
              label: parsed.label || 'unknown',
              category: parsed.category || 'training',
              sampleName: parsed.sampleName || '',
              override: false,
            });
          }
        } catch (e) {
          console.warn('Failed to parse', file.name, e);
        }
      }
      if (!cancelled) {
        if (results.length === 0) setError('No valid EdgeImpulse .json files found.');
        setItems(results);
        setLoading(false);
      }
    }
    parse();
    return () => { cancelled = true; };
  }, [files]);

  useEffect(() => {
    const onKey = e => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  const updateItem = useCallback((id, changes) => {
    setItems(prev => prev.map(it => it.id === id
      ? { ...it, ...changes, override: true }
      : it
    ));
  }, []);

  const removeItem = useCallback((id) => {
    setItems(prev => prev.filter(it => it.id !== id));
  }, []);

  // Apply bulk label/category to all
  const applyBulk = () => {
    setItems(prev => prev.map(it => ({
      ...it,
      ...(bulkLabel    ? { label: bulkLabel, override: true }    : {}),
      ...(bulkCategory ? { category: bulkCategory, override: true } : {}),
    })));
  };

  const doImport = () => {
    if (!items.length) return;
    const finalSamples = items.map(it => ({
      ...it.parsed,
      id: uid(),
      label: it.label || it.parsed.label,
      category: it.category,
      sampleName: it.sampleName,
    }));
    onImport(finalSamples);
    onClose();
  };

  const totalPts = items.reduce((a, it) => a + (it.parsed.values?.length || 0), 0);

  return (
    <div
      style={{ position: 'fixed', inset: 0, background: '#000c', zIndex: 300, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 16, backdropFilter: 'blur(4px)' }}
      onClick={onClose}
    >
      <div
        style={{ background: '#0a1628', border: '1px solid #1e3a5f', borderRadius: 14, width: 'min(700px, 100%)', maxHeight: '90vh', display: 'flex', flexDirection: 'column', boxShadow: '0 32px 80px #0009' }}
        onClick={e => e.stopPropagation()}
      >
        {/* Header */}
        <div style={{ padding: '14px 18px', borderBottom: '1px solid #1e293b', display: 'flex', alignItems: 'center', gap: 10, flexShrink: 0 }}>
          <FileJson size={16} color="#38bdf8" />
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 14, fontWeight: 700, color: '#f1f5f9' }}>Import JSON Samples</div>
            <div style={{ fontSize: 10, color: '#475569' }}>
              {loading ? 'Parsing files…' : `${items.length} valid file${items.length !== 1 ? 's' : ''} · ${totalPts.toLocaleString()} total pts`}
            </div>
          </div>
          <button onClick={onClose}
            style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', background: 'none', border: 'none', color: '#475569', width: 28, height: 28, cursor: 'pointer', borderRadius: 4 }}
            onMouseEnter={e => e.currentTarget.style.color = '#f1f5f9'}
            onMouseLeave={e => e.currentTarget.style.color = '#475569'}>
            <X size={16} />
          </button>
        </div>

        {/* Bulk controls */}
        {!loading && items.length > 1 && (
          <div style={{ padding: '10px 18px', borderBottom: '1px solid #0f1e33', background: '#070f1e', display: 'flex', gap: 10, alignItems: 'flex-end', flexWrap: 'wrap', flexShrink: 0 }}>
            <div style={{ fontSize: 10, fontWeight: 700, color: '#64748b', alignSelf: 'center', minWidth: 60 }}>Apply all:</div>
            <div>
              <div style={{ fontSize: 8, color: '#475569', marginBottom: 3 }}>Label</div>
              <input
                value={bulkLabel}
                onChange={e => setBulkLabel(e.target.value)}
                list="bulk-labels-list"
                placeholder="override label…"
                style={{ background: '#0a1628', color: '#f1f5f9', border: '1px solid #1e293b', borderRadius: 4, padding: '4px 8px', fontSize: 10, fontFamily: 'inherit', width: 140 }}
              />
              <datalist id="bulk-labels-list">
                {allLabels.map(l => <option key={l} value={l} />)}
              </datalist>
            </div>
            <div>
              <div style={{ fontSize: 8, color: '#475569', marginBottom: 3 }}>Category</div>
              <div style={{ display: 'flex', gap: 3 }}>
                {[['', 'Keep'], ['training', 'Train'], ['testing', 'Test']].map(([v, l]) => (
                  <button key={v} onClick={() => setBulkCategory(v)} style={{
                    background: bulkCategory === v ? '#0d2040' : '#060d1a',
                    border: `1px solid ${bulkCategory === v ? '#3b82f6' : '#1e293b'}`,
                    color: bulkCategory === v ? '#60a5fa' : '#475569',
                    borderRadius: 3, padding: '3px 8px', cursor: 'pointer', fontSize: 9, fontFamily: 'inherit',
                  }}>{l}</button>
                ))}
              </div>
            </div>
            <button onClick={applyBulk}
              disabled={!bulkLabel && !bulkCategory}
              style={{
                display: 'flex', alignItems: 'center', gap: 5,
                background: (bulkLabel || bulkCategory) ? '#0d2040' : '#060d1a',
                border: `1px solid ${(bulkLabel || bulkCategory) ? '#3b82f6' : '#1e293b'}`,
                color: (bulkLabel || bulkCategory) ? '#60a5fa' : '#334155',
                borderRadius: 5, padding: '5px 12px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit', fontWeight: 600,
              }}>
              <Tag size={11} /> Apply to all
            </button>
          </div>
        )}

        {/* Sample list */}
        <div style={{ flex: 1, overflowY: 'auto', padding: '10px 18px', display: 'flex', flexDirection: 'column', gap: 7 }}>
          {loading && (
            <div style={{ textAlign: 'center', padding: 40, color: '#475569', fontSize: 12 }}>Parsing files…</div>
          )}
          {!loading && error && (
            <div style={{ textAlign: 'center', padding: 40, color: '#f87171', fontSize: 12 }}>{error}</div>
          )}
          {!loading && !error && items.map(item => (
            <SampleRow
              key={item.id}
              item={item}
              allLabels={allLabels}
              onChange={changes => updateItem(item.id, changes)}
              onRemove={() => removeItem(item.id)}
            />
          ))}
          {!loading && !error && items.length === 0 && (
            <div style={{ textAlign: 'center', padding: 40, color: '#475569', fontSize: 11 }}>All files removed.</div>
          )}
        </div>

        {/* Footer */}
        <div style={{ padding: '12px 18px', borderTop: '1px solid #1e293b', display: 'flex', gap: 8, justifyContent: 'space-between', alignItems: 'center', flexShrink: 0 }}>
          <div style={{ fontSize: 10, color: '#475569' }}>
            {items.length > 0 && (
              <>
                <span style={{ color: '#34d399' }}>{items.filter(i => i.category === 'training').length} training</span>
                {' · '}
                <span style={{ color: '#f59e0b' }}>{items.filter(i => i.category === 'testing').length} testing</span>
                {' · '}
                <span style={{ color: '#60a5fa' }}>{[...new Set(items.map(i => i.label))].length} label{[...new Set(items.map(i => i.label))].length !== 1 ? 's' : ''}</span>
              </>
            )}
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <button onClick={onClose}
              style={{ background: '#050c1a', border: '1px solid #1e293b', color: '#64748b', borderRadius: 6, padding: '7px 18px', cursor: 'pointer', fontFamily: 'inherit', fontSize: 11 }}>
              Cancel
            </button>
            <button onClick={doImport} disabled={items.length === 0 || loading}
              style={{
                display: 'flex', alignItems: 'center', gap: 6,
                background: items.length > 0 && !loading ? '#065f46' : '#1e293b',
                border: 'none',
                color: items.length > 0 && !loading ? '#34d399' : '#334155',
                borderRadius: 6, padding: '7px 20px', cursor: items.length > 0 && !loading ? 'pointer' : 'not-allowed',
                fontWeight: 700, fontFamily: 'inherit', fontSize: 11,
              }}>
              <Upload size={13} />
              Import {items.length > 0 ? `${items.length} sample${items.length !== 1 ? 's' : ''}` : ''}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
