import { useState } from 'react';
import { formatMs } from '../utils/parse';
import { CATEGORY_COLORS } from '../utils/colors';
import MiniChart from './MiniChart';

export default function SampleCard({
  sample, selected, active, viewMode, sensors,
  onSelect, onActivate, onDelete, onRename,
  onToggleEnabled, onToggleCategory, onSplit,
}) {
  const [editing, setEditing] = useState(false);
  const [newLabel, setNewLabel] = useState(sample.label);

  const edgeColor = sample.enabled
    ? (sample.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training)
    : '#7f1d1d';
  const borderColor = active ? '#38bdf8' : selected ? '#2563eb66' : '#1e293b';

  const badges = (
    <div style={{ display: 'flex', alignItems: 'center', gap: 3, flexWrap: 'wrap' }}>
      <span style={{ background: '#0d2040', color: '#60a5fa', fontSize: 9, borderRadius: 3, padding: '1px 5px', fontFamily: 'inherit' }}>
        {sample.values.length || 0}pt
      </span>
      <span style={{ background: '#0a2a1a', color: '#34d399', fontSize: 9, borderRadius: 3, padding: '1px 5px', fontFamily: 'inherit' }}>
        {formatMs(sample.duration_ms)}
      </span>
      <button onClick={e => { e.stopPropagation(); onToggleCategory && onToggleCategory(sample.id); }}
        style={{
          background: sample.category === 'testing' ? '#451a0322' : '#0d204044',
          border: `1px solid ${sample.category === 'testing' ? CATEGORY_COLORS.testing + '66' : CATEGORY_COLORS.training + '66'}`,
          color: sample.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training,
          fontSize: 8, borderRadius: 3, padding: '1px 5px', cursor: 'pointer', fontFamily: 'inherit', fontWeight: 700,
        }}>
        {sample.category === 'testing' ? 'TEST' : 'TRAIN'}
      </button>
      {sample.fromLabels && <span style={{ background: '#2a1a0a', color: '#fbbf24', fontSize: 8, borderRadius: 3, padding: '1px 4px' }}>ref</span>}
    </div>
  );

  const controls = (
    <div style={{ display: 'flex', alignItems: 'center', gap: 3 }}>
      {!sample.fromLabels && sample.values.length > 10 && (
        <button onClick={e => { e.stopPropagation(); onSplit(sample); }} title="Split"
          style={{ background: '#1a0f00', border: '1px solid #92400e', color: '#fbbf24', borderRadius: 3, padding: '2px 5px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit', flexShrink: 0 }}>
          ✂️
        </button>
      )}
      <button onClick={e => { e.stopPropagation(); onToggleEnabled(sample.id); }}
        style={{ background: 'none', border: 'none', color: sample.enabled ? '#34d399' : '#ef4444', cursor: 'pointer', fontSize: 10, padding: 0, flexShrink: 0 }}>
        {sample.enabled ? '●' : '○'}
      </button>
      <button onClick={e => { e.stopPropagation(); onDelete(sample.id); }}
        style={{ background: 'none', border: 'none', color: '#1e293b', cursor: 'pointer', fontSize: 10, padding: 0, flexShrink: 0 }}>
        ✕
      </button>
    </div>
  );

  const labelEl = editing ? (
    <input value={newLabel} onChange={e => setNewLabel(e.target.value)}
      onClick={e => e.stopPropagation()}
      onKeyDown={e => {
        if (e.key === 'Enter') { onRename(sample.id, newLabel); setEditing(false); }
        if (e.key === 'Escape') { setEditing(false); setNewLabel(sample.label); }
      }}
      style={{ background: '#050c1a', color: '#f1f5f9', border: '1px solid #3b82f6', borderRadius: 4, padding: '2px 6px', fontSize: 11, flex: 1, fontFamily: 'inherit' }}
      autoFocus />
  ) : (
    <div style={{ flex: 1, minWidth: 0 }}>
      <span style={{ fontWeight: 600, fontSize: viewMode === 'grid' ? 10 : 11, color: active ? '#e2e8f0' : '#94a3b8', display: 'block', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}
        onDoubleClick={e => { e.stopPropagation(); setEditing(true); }}>
        {sample.label}
      </span>
      {sample.sampleName && (
        <span style={{ fontSize: 8, color: '#334155', display: 'block', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {sample.sampleName}
        </span>
      )}
    </div>
  );

  // ── GRID mode ──────────────────────────────────────────────────────────────
  if (viewMode === 'grid') {
    return (
      <div onClick={() => onActivate(sample.id)}
        style={{
          background: active ? '#0a1e38' : selected ? '#081628' : '#080f1e',
          borderTop: `1px solid ${borderColor}`,
          borderRight: `1px solid ${borderColor}`,
          borderBottom: `1px solid ${borderColor}`,
          borderLeft: `3px solid ${edgeColor}`,
          borderRadius: 7, padding: 8, cursor: 'pointer', transition: 'all 0.1s',
        }}>
        {/* Top row */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, marginBottom: 5 }}>
          <input type="checkbox" checked={selected} onChange={() => {}}
            onClick={e => { e.stopPropagation(); onSelect(sample.id); }}
            style={{ accentColor: '#3b82f6', flexShrink: 0, cursor: 'pointer' }} />
          {labelEl}
          {controls}
        </div>
        {/* Mini chart */}
        {sample.values.length > 0 && sensors.length > 0 && (
          <div style={{ marginBottom: 5 }}>
            <MiniChart values={sample.values} sensors={sensors} height={40} />
          </div>
        )}
        {badges}
      </div>
    );
  }

  // ── LIST mode (default) ───────────────────────────────────────────────────
  return (
    <div onClick={() => onActivate(sample.id)}
      style={{
        background: active ? '#0a1e38' : selected ? '#081628' : '#080f1e',
        borderTop: `1px solid ${borderColor}`,
        borderRight: `1px solid ${borderColor}`,
        borderBottom: `1px solid ${borderColor}`,
        borderLeft: `3px solid ${edgeColor}`,
        borderRadius: 7, padding: '7px 9px', cursor: 'pointer',
        marginBottom: 4, transition: 'all 0.1s',
      }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 5, marginBottom: 3 }}>
        <input type="checkbox" checked={selected} onChange={() => {}}
          onClick={e => { e.stopPropagation(); onSelect(sample.id); }}
          style={{ accentColor: '#3b82f6', flexShrink: 0, cursor: 'pointer' }} />
        {labelEl}
        {controls}
      </div>
      {badges}
    </div>
  );
}
