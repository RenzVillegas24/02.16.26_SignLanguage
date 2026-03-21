import { useState, useMemo } from 'react';
import { CATEGORY_COLORS } from '../utils/colors';

export default function LabelsModal({ labelsData, onImport, onClose }) {
  const [sel, setSel] = useState(() => new Set(labelsData.map((_, i) => i)));
  const [catFilter, setCatFilter] = useState('all'); // all | training | testing

  // Group by label
  const groups = useMemo(() => {
    const m = {};
    labelsData.forEach((f, i) => {
      if (!m[f.label]) m[f.label] = [];
      m[f.label].push({ ...f, idx: i });
    });
    return m;
  }, [labelsData]);

  // Stats
  const totalTraining = labelsData.filter(f => f.category === 'training').length;
  const totalTesting = labelsData.filter(f => f.category === 'testing').length;

  const filteredGroups = useMemo(() => {
    if (catFilter === 'all') return groups;
    const result = {};
    Object.entries(groups).forEach(([label, files]) => {
      const filtered = files.filter(f => f.category === catFilter);
      if (filtered.length) result[label] = filtered;
    });
    return result;
  }, [groups, catFilter]);

  const toggleGroup = (label) => {
    const idxs = (groups[label] || []).map(f => f.idx);
    const allSel = idxs.every(i => sel.has(i));
    setSel(p => {
      const n = new Set(p);
      idxs.forEach(i => allSel ? n.delete(i) : n.add(i));
      return n;
    });
  };

  const toggleAll = () => {
    if (sel.size === labelsData.length) setSel(new Set());
    else setSel(new Set(labelsData.map((_, i) => i)));
  };

  const selectCategory = (cat) => {
    setSel(new Set(
      labelsData
        .map((f, i) => ({ f, i }))
        .filter(({ f }) => f.category === cat)
        .map(({ i }) => i)
    ));
  };

  return (
    <div style={{
      position: 'fixed', inset: 0, background: '#000c', zIndex: 200,
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      padding: 16, backdropFilter: 'blur(4px)',
    }}>
      <div style={{
        background: '#0a1628', border: '1px solid #1e3a5f', borderRadius: 14,
        width: 'min(760px, 100%)', maxHeight: '88vh', display: 'flex', flexDirection: 'column',
      }}>
        {/* Header */}
        <div style={{ padding: '14px 20px', borderBottom: '1px solid #1e293b', display: 'flex', alignItems: 'center', gap: 12 }}>
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 14, fontWeight: 700, color: '#f1f5f9' }}>📋 Labels File</div>
            <div style={{ fontSize: 10, color: '#475569', marginTop: 2, display: 'flex', gap: 10 }}>
              <span>{labelsData.length} entries</span>
              <span>·</span>
              <span>{Object.keys(groups).length} labels</span>
              <span>·</span>
              <span style={{ color: CATEGORY_COLORS.training }}>{totalTraining} training</span>
              <span>·</span>
              <span style={{ color: CATEGORY_COLORS.testing }}>{totalTesting} testing</span>
            </div>
          </div>
          <button onClick={onClose} style={{ background: 'none', border: 'none', color: '#475569', fontSize: 20, cursor: 'pointer' }}>✕</button>
        </div>

        {/* Category filter tabs */}
        <div style={{ padding: '10px 20px', borderBottom: '1px solid #1e293b', display: 'flex', gap: 6, alignItems: 'center' }}>
          {['all', 'training', 'testing'].map(cat => (
            <button
              key={cat}
              onClick={() => setCatFilter(cat)}
              style={{
                background: catFilter === cat
                  ? (cat === 'testing' ? '#451a03' : cat === 'training' ? '#0d2040' : '#1e293b')
                  : '#050c1a',
                border: `1px solid ${catFilter === cat
                  ? (cat === 'testing' ? CATEGORY_COLORS.testing : cat === 'training' ? CATEGORY_COLORS.training : '#64748b')
                  : '#1e293b'}`,
                color: catFilter === cat
                  ? (cat === 'testing' ? CATEGORY_COLORS.testing : cat === 'training' ? CATEGORY_COLORS.training : '#94a3b8')
                  : '#475569',
                borderRadius: 5, padding: '4px 12px', cursor: 'pointer', fontSize: 11, fontFamily: 'inherit',
              }}
            >
              {cat === 'all' ? `All (${labelsData.length})`
                : cat === 'training' ? `Training (${totalTraining})`
                : `Testing (${totalTesting})`}
            </button>
          ))}
          <div style={{ flex: 1 }} />
          <button onClick={() => selectCategory('training')} style={{ background: 'none', border: '1px solid #1e293b', color: CATEGORY_COLORS.training, borderRadius: 4, padding: '3px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit' }}>
            Select Training
          </button>
          <button onClick={() => selectCategory('testing')} style={{ background: 'none', border: '1px solid #1e293b', color: CATEGORY_COLORS.testing, borderRadius: 4, padding: '3px 10px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit' }}>
            Select Testing
          </button>
        </div>

        {/* File list */}
        <div style={{ flex: 1, overflowY: 'auto', padding: 16 }}>
          {Object.entries(filteredGroups).sort(([a], [b]) => a.localeCompare(b)).map(([label, files]) => (
            <div key={label} style={{ marginBottom: 14 }}>
              {/* Label header */}
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 5 }}>
                <span style={{ fontSize: 11, fontWeight: 700, color: '#38bdf8' }}>{label}</span>
                <span style={{ fontSize: 9, color: '#334155' }}>{files.length} files</span>
                <div style={{ flex: 1 }} />
                <button
                  onClick={() => toggleGroup(label)}
                  style={{ background: 'none', border: '1px solid #1e293b', color: '#475569', borderRadius: 3, padding: '1px 8px', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}
                >
                  toggle
                </button>
              </div>

              {/* File rows */}
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(260px, 1fr))', gap: 3 }}>
                {files.map(f => (
                  <div
                    key={f.idx}
                    onClick={() => setSel(p => {
                      const n = new Set(p);
                      n.has(f.idx) ? n.delete(f.idx) : n.add(f.idx);
                      return n;
                    })}
                    style={{
                      display: 'flex', alignItems: 'flex-start', gap: 7,
                      padding: '6px 8px', borderRadius: 5,
                      background: sel.has(f.idx) ? '#0d2040' : '#050c1a',
                      border: `1px solid ${sel.has(f.idx) ? '#1e3a5f' : '#1e293b'}`,
                      cursor: 'pointer',
                    }}
                  >
                    <input
                      type="checkbox"
                      checked={sel.has(f.idx)}
                      readOnly
                      style={{ accentColor: '#3b82f6', flexShrink: 0, marginTop: 2 }}
                    />
                    <div style={{ flex: 1, minWidth: 0 }}>
                      {/* Sample name */}
                      {f.name && (
                        <div style={{ fontSize: 10, color: '#94a3b8', fontWeight: 600, marginBottom: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                          {f.name}
                        </div>
                      )}
                      {/* Filename / path */}
                      <div style={{ fontSize: 9, color: '#475569', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                        {f.filename || f.path}
                      </div>
                    </div>
                    {/* Category badge */}
                    <span style={{
                      fontSize: 8, fontWeight: 700, flexShrink: 0,
                      color: f.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training,
                      background: f.category === 'testing' ? '#451a0322' : '#0d204044',
                      borderRadius: 3, padding: '1px 4px', marginTop: 2,
                    }}>
                      {f.category === 'testing' ? 'TEST' : 'TRAIN'}
                    </span>
                    {/* Enabled indicator */}
                    <span style={{ fontSize: 9, color: f.enabled ? '#34d399' : '#f87171', flexShrink: 0, marginTop: 2 }}>
                      {f.enabled ? '●' : '○'}
                    </span>
                  </div>
                ))}
              </div>
            </div>
          ))}

          {Object.keys(filteredGroups).length === 0 && (
            <div style={{ color: '#334155', textAlign: 'center', padding: 40 }}>
              No entries match the current filter
            </div>
          )}
        </div>

        {/* Footer */}
        <div style={{ padding: '12px 20px', borderTop: '1px solid #1e293b', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <button
              onClick={toggleAll}
              style={{ background: '#0d2040', border: '1px solid #1e293b', color: '#64748b', borderRadius: 6, padding: '6px 14px', cursor: 'pointer', fontSize: 11, fontFamily: 'inherit' }}
            >
              {sel.size === labelsData.length ? 'Deselect All' : 'Select All'}
            </button>
            <span style={{ fontSize: 10, color: '#334155' }}>{sel.size} selected</span>
          </div>
          <div style={{ display: 'flex', gap: 8 }}>
            <button
              onClick={onClose}
              style={{ background: '#050c1a', border: '1px solid #1e293b', color: '#475569', borderRadius: 6, padding: '6px 14px', cursor: 'pointer', fontFamily: 'inherit' }}
            >
              Cancel
            </button>
            <button
              onClick={() => { onImport(labelsData.filter((_, i) => sel.has(i))); onClose(); }}
              style={{ background: '#1d4ed8', border: 'none', color: '#fff', borderRadius: 6, padding: '6px 20px', cursor: 'pointer', fontWeight: 700, fontFamily: 'inherit' }}
            >
              Import {sel.size}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
