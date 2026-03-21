import { useState, memo } from 'react';
import { Scissors, Circle, CircleDot, X, Tag, FlaskConical, Dumbbell } from 'lucide-react';
import { formatMs } from '../utils/parse';
import { CATEGORY_COLORS } from '../utils/colors';
import MiniChart from './MiniChart';

const SampleCard = memo(function SampleCard({
  sample, selected, active, viewMode, sensors,
  onSelect, onActivate, onDelete, onRename,
  onToggleEnabled, onToggleCategory, onSplit,
}) {
  const [editing, setEditing] = useState(false);
  const [newLabel, setNewLabel] = useState(sample.label);

  const edgeColor = sample.enabled
    ? (sample.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training)
    : '#4b1113';
  const borderColor = active ? '#38bdf8' : selected ? '#2563eb66' : '#1a2540';

  // ── Badges row ─────────────────────────────────────────────────────────
  const badges = (
    <div style={{ display: 'flex', alignItems: 'center', gap: 3, flexWrap: 'wrap' }}>
      <span style={{
        background: '#0a1a33', color: '#60a5fa', fontSize: 9,
        borderRadius: 3, padding: '1px 5px', fontFamily: 'inherit', letterSpacing: 0.3,
      }}>
        {(sample.values.length || 0).toLocaleString()} pt
      </span>
      <span style={{
        background: '#091a12', color: '#34d399', fontSize: 9,
        borderRadius: 3, padding: '1px 5px', fontFamily: 'inherit',
      }}>
        {formatMs(sample.duration_ms)}
      </span>
      <button
        onClick={e => { e.stopPropagation(); onToggleCategory && onToggleCategory(sample.id); }}
        title={`Category: ${sample.category} — click to toggle`}
        style={{
          display: 'flex', alignItems: 'center', gap: 2,
          background: sample.category === 'testing' ? '#1f1108' : '#09162a',
          border: `1px solid ${sample.category === 'testing' ? CATEGORY_COLORS.testing + '55' : CATEGORY_COLORS.training + '55'}`,
          color: sample.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training,
          fontSize: 8, borderRadius: 3, padding: '1px 5px',
          cursor: 'pointer', fontFamily: 'inherit', fontWeight: 700,
        }}
      >
        {sample.category === 'testing'
          ? <FlaskConical size={8} strokeWidth={2.5} />
          : <Dumbbell size={8} strokeWidth={2.5} />}
        {sample.category === 'testing' ? 'TEST' : 'TRAIN'}
      </button>
      {sample.fromLabels && (
        <span style={{
          display: 'flex', alignItems: 'center', gap: 2,
          background: '#1a1106', color: '#fbbf24', fontSize: 8,
          borderRadius: 3, padding: '1px 4px',
        }}>
          <Tag size={7} strokeWidth={2} /> ref
        </span>
      )}
      {sample.splitBaseId && (
        <span style={{
          background: '#0d1f1a', color: '#2dd4bf', fontSize: 8,
          borderRadius: 3, padding: '1px 4px', fontFamily: 'inherit',
        }}>
          seg
        </span>
      )}
    </div>
  );

  // ── Control buttons ────────────────────────────────────────────────────
  const controls = (
    <div style={{ display: 'flex', alignItems: 'center', gap: 2 }}>
      {!sample.fromLabels && !sample.splitBaseId && sample.values.length > 10 && (
        <button
          onClick={e => { e.stopPropagation(); onSplit(sample); }}
          title="Split sample"
          style={{
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            background: '#1a1008', border: '1px solid #78350f',
            color: '#fbbf24', borderRadius: 3,
            width: 20, height: 20, cursor: 'pointer', flexShrink: 0,
          }}
        >
          <Scissors size={10} strokeWidth={2} />
        </button>
      )}
      <button
        onClick={e => { e.stopPropagation(); onToggleEnabled(sample.id); }}
        title={sample.enabled ? 'Disable sample' : 'Enable sample'}
        style={{
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          background: 'none', border: 'none',
          color: sample.enabled ? '#22c55e' : '#475569',
          width: 20, height: 20, cursor: 'pointer', flexShrink: 0, padding: 0,
        }}
      >
        {sample.enabled
          ? <CircleDot size={12} strokeWidth={2} />
          : <Circle size={12} strokeWidth={1.5} />}
      </button>
      <button
        onClick={e => { e.stopPropagation(); onDelete(sample.id); }}
        title="Delete sample"
        style={{
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          background: 'none', border: 'none',
          color: '#334155', width: 20, height: 20, cursor: 'pointer',
          flexShrink: 0, padding: 0, transition: 'color 0.1s',
        }}
        onMouseEnter={e => e.currentTarget.style.color = '#f87171'}
        onMouseLeave={e => e.currentTarget.style.color = '#334155'}
      >
        <X size={11} strokeWidth={2} />
      </button>
    </div>
  );

  // ── Label / edit field ─────────────────────────────────────────────────
  const labelEl = editing ? (
    <input
      value={newLabel}
      onChange={e => setNewLabel(e.target.value)}
      onClick={e => e.stopPropagation()}
      onKeyDown={e => {
        if (e.key === 'Enter') { onRename(sample.id, newLabel); setEditing(false); }
        if (e.key === 'Escape') { setEditing(false); setNewLabel(sample.label); }
      }}
      style={{
        background: '#050c1a', color: '#f1f5f9',
        border: '1px solid #3b82f6', borderRadius: 4,
        padding: '2px 6px', fontSize: 11, flex: 1, fontFamily: 'inherit',
      }}
      autoFocus
    />
  ) : (
    <div style={{ flex: 1, minWidth: 0 }}>
      <span
        style={{
          fontWeight: 600,
          fontSize: viewMode === 'grid' ? 10 : 11,
          color: active ? '#e2e8f0' : '#94a3b8',
          display: 'block', overflow: 'hidden',
          textOverflow: 'ellipsis', whiteSpace: 'nowrap',
          letterSpacing: 0.1,
        }}
        onDoubleClick={e => { e.stopPropagation(); setEditing(true); }}
        title={`${sample.label}${sample.sampleName ? ' · ' + sample.sampleName : ''} — double-click to rename`}
      >
        {sample.label}
      </span>
      {sample.sampleName && (
        <span style={{
          fontSize: 8, color: '#2d4060', display: 'block',
          overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
        }}>
          {sample.sampleName}
        </span>
      )}
    </div>
  );

  const baseStyle = {
    background: active ? '#091d38' : selected ? '#071428' : '#080f1e',
    borderTop: `1px solid ${borderColor}`,
    borderRight: `1px solid ${borderColor}`,
    borderBottom: `1px solid ${borderColor}`,
    borderLeft: `3px solid ${edgeColor}`,
    borderRadius: 7,
    cursor: 'pointer',
    transition: 'background 0.1s, border-color 0.1s',
    userSelect: 'none',
  };

  // ── GRID view ──────────────────────────────────────────────────────────
  if (viewMode === 'grid') {
    return (
      <div onClick={() => onActivate(sample.id)} style={{ ...baseStyle, padding: 8 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, marginBottom: 5 }}>
          <input type="checkbox" checked={selected} onChange={() => {}}
            onClick={e => { e.stopPropagation(); onSelect(sample.id); }}
            style={{ accentColor: '#3b82f6', flexShrink: 0, cursor: 'pointer', width: 12, height: 12 }} />
          {labelEl}
          {controls}
        </div>
        {sample.values.length > 0 && sensors.length > 0 && (
          <div style={{ marginBottom: 5, borderRadius: 4, overflow: 'hidden' }}>
            <MiniChart values={sample.values} sensors={sensors} height={38} />
          </div>
        )}
        {badges}
      </div>
    );
  }

  // ── LIST view ──────────────────────────────────────────────────────────
  return (
    <div onClick={() => onActivate(sample.id)} style={{ ...baseStyle, padding: '6px 9px', marginBottom: 3 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 5, marginBottom: 3 }}>
        <input type="checkbox" checked={selected} onChange={() => {}}
          onClick={e => { e.stopPropagation(); onSelect(sample.id); }}
          style={{ accentColor: '#3b82f6', flexShrink: 0, cursor: 'pointer', width: 12, height: 12 }} />
        {labelEl}
        {controls}
      </div>
      {badges}
    </div>
  );
});

export default SampleCard;
