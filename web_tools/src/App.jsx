import { useState, useCallback, useRef, useMemo, useEffect } from 'react';
import JSZip from 'jszip';
import {
  Save, FolderOpen, Package, Upload, FileJson,
  Scissors, GitMerge, Fingerprint, Download,
  List, LayoutGrid, Zap, ChevronRight,
  CheckSquare, Square, Trash2, Filter, Search, X, Sun, Moon,
} from 'lucide-react';
import { uid, syncUid, parseEIJson, parseLabelsFile, buildEIJson, downloadJSON, simpleHash, formatMs } from './utils/parse';
import { SENSOR_COLORS, CATEGORY_COLORS } from './utils/colors';
import { DARK, LIGHT } from './utils/theme';
import { ThemeContext } from './utils/ThemeContext';
import { pickBestChannels } from './utils/algorithms';
import DualRange from './components/DualRange';
import SplitModal from './components/SplitModal';
import LabelsModal from './components/LabelsModal';
import WaveformViewer from './components/WaveformViewer';
import SampleCard from './components/SampleCard';
import SplitHighlightsViewer from './components/SplitHighlightsViewer';
import LoadingOverlay from './components/LoadingOverlay';
import PredictTab from './components/PredictTab';
import WindowedCountTab from './components/WindowedCountTab';
import JsonImportModal from './components/JsonImportModal';

// ─── ZIP loader ────────────────────────────────────────────────────────────
async function loadZip(file) {
  const zip = await JSZip.loadAsync(file);
  let manifest = [];
  const labelsEntry = Object.values(zip.files).find(f =>
    !f.dir && (f.name.endsWith('.labels') || f.name.endsWith('info.labels'))
  );
  if (labelsEntry) {
    const text = await labelsEntry.async('text');
    manifest = parseLabelsFile(text) || [];
  }
  const manifestByPath = new Map();
  const manifestByFilename = new Map();
  manifest.forEach(m => {
    if (m.path) manifestByPath.set(m.path, m);
    if (m.filename) manifestByFilename.set(m.filename, m);
  });

  const samples = [];
  const jsonFiles = Object.values(zip.files).filter(f =>
    !f.dir && f.name.endsWith('.json') && f !== labelsEntry
  );
  for (const zipEntry of jsonFiles) {
    const text = await zipEntry.async('text');
    const fullPath = zipEntry.name;
    const normPath = fullPath.replace(/^[^/]+\/(?=(testing|training)\/)/, '');
    const filename = fullPath.split('/').pop();
    if (!filename || filename === 'info.labels') continue;
    const parsed = parseEIJson(text, filename);
    if (!parsed) continue;
    const match = manifestByPath.get(normPath) || manifestByFilename.get(filename);
    if (match) {
      parsed.label = String(match.label);
      parsed.category = match.category === 'testing' ? 'testing' : 'training';
      parsed.sampleName = String(match.name || '');
      parsed.enabled = match.enabled !== false;
    } else if (normPath.startsWith('testing/') || fullPath.includes('/testing/')) {
      parsed.category = 'testing';
    } else if (normPath.startsWith('training/') || fullPath.includes('/training/')) {
      parsed.category = 'training';
    }
    samples.push(parsed);
  }
  return { manifest, samples };
}

// ─── Resizable sidebar hook ────────────────────────────────────────────────
function useResizable(initial = 295, min = 200, max = 600) {
  const [width, setWidth] = useState(initial);
  const dragging = useRef(false);
  const startX = useRef(0);
  const startW = useRef(initial);

  const onMouseDown = useCallback((e) => {
    dragging.current = true;
    startX.current = e.clientX;
    startW.current = width;
    e.preventDefault();
  }, [width]);

  useEffect(() => {
    const onMove = (e) => {
      if (!dragging.current) return;
      const next = Math.max(min, Math.min(max, startW.current + e.clientX - startX.current));
      setWidth(next);
    };
    const onUp = () => { dragging.current = false; };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, [min, max]);

  return { width, onMouseDown };
}

// ─── Main App ──────────────────────────────────────────────────────────────
export default function App() {
  const [samples, setSamples] = useState([]);
  const [selectedIds, setSelectedIds] = useState(new Set());
  const [activeId, setActiveId] = useState(null);
  const [filterLabel, setFilterLabel] = useState('all');
  const [filterStatus, setFilterStatus] = useState('all');
  const [filterCategory, setFilterCategory] = useState('all');
  const [timeRange, setTimeRange] = useState([0, 100]);
  const [splitTarget, setSplitTarget] = useState(null);   // single sample split
  const [batchSplitTargets, setBatchSplitTargets] = useState(null); // multi split
  const [labelsPayload, setLabelsPayload] = useState(null);
  const [tab, setTab] = useState('waveform');
  const [toast, setToast] = useState(null);
  const [dragOver, setDragOver] = useState(false);
  const [loading, setLoading] = useState(null); // null | { label, sub, count, total }
  const [sidebarViewMode, setSidebarViewMode] = useState('list'); // 'list' | 'grid'
  const [sampleSearch, setSampleSearch] = useState('');
  const [splitHighlightsByBase, setSplitHighlightsByBase] = useState({});
  const [splitHighlightSensors, setSplitHighlightSensors] = useState([]);
  const [visibleSampleTypes, setVisibleSampleTypes] = useState(new Set(['unmodified', 'segment']));
  const [themeName, setThemeName] = useState('dark');
  const [importMergeMode, setImportMergeMode] = useState('merge'); // 'merge' | 'replace' | 'add'
  const theme = themeName === 'light' ? LIGHT : DARK;

  useEffect(() => {
    document.body.className = themeName === 'light' ? 'light' : '';
    document.body.style.background = theme.bgBase;
    document.body.style.color = theme.textPrimary;
  }, [themeName, theme]);

  const fileRef = useRef();
  const zipRef = useRef();
  const projectRef = useRef();
  const jsonImportRef = useRef();
  const labelsManifestRef = useRef([]);
  const [jsonImportFiles, setJsonImportFiles] = useState(null); // null | File[]
  const sidebar = useResizable(295, 180, 620);

  // ── Derived ────────────────────────────────────────────────────────────────
  const sensors = useMemo(() => samples.find(s => s.sensors?.length)?.sensors || [], [samples]);

  const allLabels = useMemo(() => {
    const s = new Set(samples.map(s => s.label));
    return ['all', ...Array.from(s).sort()];
  }, [samples]);

  const [minDur, maxDur] = useMemo(() => {
    if (!samples.length) return [0, 1];
    const durs = samples.map(s => s.duration_ms);
    return [0, Math.max(...durs)];
  }, [samples]);
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const durationSpan = maxDur - minDur || 1;
  const pctToDuration = useCallback((pct) => minDur + (durationSpan * (pct / 100)), [minDur, durationSpan]);
  const durationToPct = useCallback((durationMs) => {
    if (durationSpan <= 0) return 0;
    return clamp(((durationMs - minDur) / durationSpan) * 100, 0, 100);
  }, [minDur, durationSpan]);

  const splitBaseIdSet = useMemo(() => new Set(samples.filter(s => s.splitBaseId).map(s => s.splitBaseId)), [samples]);
  const getSampleType = useCallback((s) => {
    if (!s) return 'unmodified';
    if (s.splitBaseId) return 'segment';
    if (s.hiddenAfterSplit || splitBaseIdSet.has(s.id)) return 'trimmed';
    return 'unmodified';
  }, [splitBaseIdSet]);

  const filteredSamples = useMemo(() => {
    const q = sampleSearch.trim().toLowerCase();
    return samples.filter(s => {
      if (!visibleSampleTypes.has(getSampleType(s))) return false;
      if (filterLabel !== 'all' && s.label !== filterLabel) return false;
      if (filterStatus === 'enabled' && !s.enabled) return false;
      if (filterStatus === 'disabled' && s.enabled) return false;
      if (filterCategory !== 'all' && s.category !== filterCategory) return false;
      const range = maxDur - minDur || 1;
      const pct = ((s.duration_ms - minDur) / range) * 100;
      if (pct < timeRange[0] || pct > timeRange[1]) return false;
      if (q) {
        const label = (s.label || '').toLowerCase();
        const name  = (s.sampleName || '').toLowerCase();
        const file  = (s.filename || '').toLowerCase();
        if (!label.includes(q) && !name.includes(q) && !file.includes(q)) return false;
      }
      return true;
    });
  }, [samples, visibleSampleTypes, getSampleType, filterLabel, filterStatus, filterCategory, timeRange, minDur, maxDur, sampleSearch]);

  const viewSamples = useMemo(() => {
    const ids = new Set(selectedIds);
    if (activeId) ids.add(activeId);
    return samples.filter(s => ids.has(s.id) && s.values.length > 0);
  }, [samples, selectedIds, activeId]);

  const selectedSamples = useMemo(() => samples.filter(s => selectedIds.has(s.id)), [samples, selectedIds]);
  const activeSample = useMemo(() => samples.find(s => s.id === activeId) || null, [samples, activeId]);
  const samplesById = useMemo(() => new Map(samples.map(s => [s.id, s])), [samples]);

  const groupedByLabel = useMemo(() => {
    const m = {};
    samples.forEach(s => { if (!m[s.label]) m[s.label] = []; m[s.label].push(s); });
    return m;
  }, [samples]);

  const sampleTypeCounts = useMemo(() => {
    const out = { unmodified: 0, segment: 0, trimmed: 0 };
    samples.forEach(s => {
      const t = getSampleType(s);
      out[t] = (out[t] || 0) + 1;
    });
    return out;
  }, [samples, getSampleType]);

  // ── Toast ──────────────────────────────────────────────────────────────────
  const showToast = useCallback((msg, type = 'info') => {
    setToast({ msg, type }); setTimeout(() => setToast(null), 4000);
  }, []);

  // ── ZIP import ─────────────────────────────────────────────────────────────
  const handleZip = useCallback(async (file) => {
    setLoading({ label: 'Reading ZIP', sub: file.name });
    try {
      const { manifest, samples: newSamples } = await loadZip(file);
      if (manifest.length) labelsManifestRef.current = manifest;
      if (!newSamples.length) { showToast('No valid sample .json files found in ZIP', 'error'); setLoading(null); return; }

      setSamples(prev => {
        if (importMergeMode === 'replace') {
          // Wipe all existing non-segment samples and load fresh
          const segments = prev.filter(s => s.splitBaseId);
          return [...segments, ...newSamples];
        }
        if (importMergeMode === 'merge') {
          // Smart merge: skip samples that match an existing one by filename OR by data hash
          // Also update label/category/sampleName if filename matches (label update)
          const existingByFilename = new Map(prev.map(s => [s.filename, s]));
          const existingByHash     = new Map(prev.filter(s => s.hash).map(s => [s.hash, s]));
          let added = 0, updated = 0, skipped = 0;
          const result = [...prev];

          newSamples.forEach(ns => {
            const byFile = existingByFilename.get(ns.filename);
            const byHash = existingByHash.get(ns.hash);
            if (byFile) {
              // Same filename — update label/category/sampleName if they changed
              const idx = result.findIndex(s => s.id === byFile.id);
              if (idx >= 0 && (byFile.label !== ns.label || byFile.category !== ns.category || byFile.sampleName !== ns.sampleName)) {
                result[idx] = { ...result[idx], label: ns.label, category: ns.category, sampleName: ns.sampleName };
                updated++;
              } else {
                skipped++;
              }
            } else if (byHash) {
              // Same data but different filename (re-exported) — skip as duplicate
              skipped++;
            } else {
              result.push(ns);
              added++;
            }
          });
          showToast(`Import: ${added} added · ${updated} updated · ${skipped} skipped (duplicates)`, 'success');
          return result;
        }
        // 'add' — just append everything without dedup
        return [...prev, ...newSamples];
      });

      if (importMergeMode === 'add') {
        showToast(`Loaded ${newSamples.length} samples${manifest.length ? ` · ${manifest.length} manifest entries` : ''}`, 'success');
      } else if (importMergeMode === 'replace') {
        showToast(`Replaced with ${newSamples.length} new samples`, 'success');
      }
      setActiveId(prev => prev || newSamples[0]?.id || null);
    } catch (err) {
      showToast(`ZIP error: ${err.message}`, 'error');
    }
    setLoading(null);
  }, [showToast, importMergeMode]);

  // ── Plain files ────────────────────────────────────────────────────────────
  const handleJsonFiles = useCallback(async (files) => {
    const results = []; let labelsFound = false;
    for (const file of files) {
      const text = await file.text();
      if (file.name.endsWith('.labels')) {
        const parsed = parseLabelsFile(text);
        if (parsed?.length) { labelsManifestRef.current = parsed; setLabelsPayload(parsed); labelsFound = true; }
        continue;
      }
      if (file.name.endsWith('.json')) {
        const asLabels = parseLabelsFile(text);
        if (asLabels && asLabels.length > 1) { labelsManifestRef.current = asLabels; setLabelsPayload(asLabels); labelsFound = true; continue; }
        const parsed = parseEIJson(text, file.name);
        if (parsed) {
          const manifest = labelsManifestRef.current;
          const match = manifest.find(m => m.filename === file.name || m.path?.endsWith('/' + file.name));
          if (match) { parsed.label = String(match.label); parsed.category = match.category === 'testing' ? 'testing' : 'training'; parsed.sampleName = String(match.name || ''); parsed.enabled = match.enabled !== false; }
          results.push(parsed);
        }
      }
    }
    if (results.length) { setSamples(p => [...p, ...results]); if (results.length > 0) setActiveId(results[0].id); showToast(`Loaded ${results.length} samples`, 'success'); }
    else if (!labelsFound) showToast('No valid files found', 'error');
  }, [showToast]);

  const processFiles = useCallback(async (files) => {
    const zips = files.filter(f => f.name.endsWith('.zip'));
    const others = files.filter(f => !f.name.endsWith('.zip'));
    for (const z of zips) await handleZip(z);
    if (others.length) await handleJsonFiles(others);
  }, [handleZip, handleJsonFiles]);

  const handleDrop = useCallback((e) => {
    e.preventDefault(); setDragOver(false);
    const files = Array.from(e.dataTransfer.files);
    const zips  = files.filter(f => f.name.endsWith('.zip'));
    const jsons = files.filter(f => f.name.endsWith('.json'));
    const others = files.filter(f => !f.name.endsWith('.zip') && !f.name.endsWith('.json'));
    for (const z of zips) handleZip(z);
    if (jsons.length) setJsonImportFiles(jsons); // open modal for json files
    if (others.length) handleJsonFiles(others);
  }, [handleZip, handleJsonFiles]);

  // ── Selection ──────────────────────────────────────────────────────────────
  const toggleSel = (id) => setSelectedIds(p => { const n = new Set(p); n.has(id) ? n.delete(id) : n.add(id); return n; });
  const selectAll = () => setSelectedIds(new Set(filteredSamples.map(s => s.id)));
  const selectNone = () => setSelectedIds(new Set());
  const activate = (id) => setActiveId(prev => prev === id ? null : id);

  // ── Mutations ──────────────────────────────────────────────────────────────

  // Smart delete:
  // • Normal sample → just remove it.
  // • Segment (has splitBaseId) → remove ONLY that segment from the samples list,
  //   remove its entry from the highlights segments array, and keep all other
  //   siblings intact. Only when the LAST sibling is deleted does the source
  //   get un-hidden and the highlights entry get cleaned up.
  const del = useCallback((id) => {
    const target = samples.find(s => s.id === id);
    if (!target) return;

    if (target.splitBaseId) {
      const baseId = target.splitBaseId;

      // All siblings in this split group (excluding the one being deleted)
      const remainingSiblings = samples.filter(
        s => s.splitBaseId === baseId && s.id !== id
      );
      const isLastSegment = remainingSiblings.length === 0;

      // Remove only this segment; if it was the last one also restore the source
      setSamples(prev =>
        prev
          .filter(s => s.id !== id)
          .map(s => s.id === baseId && isLastSegment
            ? { ...s, hiddenAfterSplit: false }   // restore source when no segments left
            : s
          )
      );

      // Update (or remove) the highlights entry
      setSplitHighlightsByBase(prev => {
        if (isLastSegment) {
          // Last segment gone — clean up the whole entry
          const next = { ...prev };
          delete next[baseId];
          return next;
        }
        // Partial removal — keep the entry but drop this segment from it
        const entry = prev[baseId];
        if (!entry) return prev;
        return {
          ...prev,
          [baseId]: {
            ...entry,
            segments: entry.segments.filter(seg => seg.sampleId !== id),
          },
        };
      });

      setSelectedIds(prev => { const n = new Set(prev); n.delete(id); return n; });

      if (activeId === id) {
        // Activate next sibling if available, else the source
        const nextActive = remainingSiblings[0]?.id ?? (isLastSegment ? baseId : null);
        setActiveId(nextActive);
      }

      if (isLastSegment) {
        showToast('Last segment deleted — source sample restored', 'info');
      } else {
        showToast(`Segment deleted — ${remainingSiblings.length} segment${remainingSiblings.length !== 1 ? 's' : ''} remaining`, 'info');
      }
    } else {
      // Plain delete for non-segment samples
      setSamples(prev => prev.filter(s => s.id !== id));
      setSelectedIds(prev => { const n = new Set(prev); n.delete(id); return n; });
      if (activeId === id) setActiveId(null);
    }
  }, [samples, activeId, showToast]);

  // Batch delete — handles segments properly in one atomic pass:
  // For each deleted ID:
  //   • Plain sample → just remove
  //   • Segment → remove; if ALL siblings in that group are also being deleted
  //     (or become the last one), also restore the source and clean highlights
  const delBatch = useCallback((ids) => {
    if (!ids.length) return;
    const idSet = new Set(ids);

    // Snapshot current state for decisions
    const snap = samples;

    // Gather all affected split groups
    const affectedGroups = new Map(); // baseId → { allSiblings, deletedSiblings }
    snap.forEach(s => {
      if (!s.splitBaseId) return;
      if (!affectedGroups.has(s.splitBaseId)) {
        affectedGroups.set(s.splitBaseId, { allSiblings: [], deletedSiblings: [] });
      }
      const g = affectedGroups.get(s.splitBaseId);
      g.allSiblings.push(s.id);
      if (idSet.has(s.id)) g.deletedSiblings.push(s.id);
    });

    // Determine which source samples to restore (all siblings being deleted)
    const sourcesToRestore = new Set();
    const highlightsToClean = new Set();
    const highlightsToTrim = new Map(); // baseId → [keepSampleIds]

    affectedGroups.forEach((g, baseId) => {
      const remaining = g.allSiblings.filter(sid => !idSet.has(sid));
      if (remaining.length === 0 && g.deletedSiblings.length > 0) {
        sourcesToRestore.add(baseId);
        highlightsToClean.add(baseId);
      } else if (g.deletedSiblings.length > 0) {
        highlightsToTrim.set(baseId, remaining);
      }
    });

    setSamples(prev =>
      prev
        .filter(s => !idSet.has(s.id))
        .map(s => sourcesToRestore.has(s.id) ? { ...s, hiddenAfterSplit: false } : s)
    );

    setSplitHighlightsByBase(prev => {
      const next = { ...prev };
      highlightsToClean.forEach(baseId => { delete next[baseId]; });
      highlightsToTrim.forEach((keepIds, baseId) => {
        if (next[baseId]) {
          const keepSet = new Set(keepIds);
          next[baseId] = {
            ...next[baseId],
            segments: next[baseId].segments.filter(seg => keepSet.has(seg.sampleId)),
          };
        }
      });
      return next;
    });

    setSelectedIds(prev => {
      const n = new Set(prev);
      ids.forEach(id => n.delete(id));
      return n;
    });

    if (idSet.has(activeId)) {
      setActiveId(null);
    }
  }, [samples, activeId]);
  const rename = (id, label) => setSamples(p => p.map(s => s.id === id ? { ...s, label } : s));
  const toggleEnabled = (id) => setSamples(p => p.map(s => s.id === id ? { ...s, enabled: !s.enabled } : s));
  const toggleCategory = (id) => setSamples(p => p.map(s => s.id === id ? { ...s, category: s.category === 'testing' ? 'training' : 'testing' } : s));

  const combine = () => {
    const toMerge = selectedSamples.filter(s => !s.fromLabels && s.values.length > 0);
    if (toMerge.length < 2) { showToast('Select 2+ data samples', 'error'); return; }
    const base = toMerge[0];
    const merged = { ...base, id: uid(), filename: `combined_${base.label}.json`, values: toMerge.flatMap(s => s.values), duration_ms: toMerge.reduce((a, s) => a + s.duration_ms, 0), hash: uid(), sampleName: '' };
    setSamples(p => [...p.filter(s => !selectedIds.has(s.id)), merged]);
    setSelectedIds(new Set()); setActiveId(merged.id);
    showToast(`Combined ${toMerge.length} samples`, 'success');
  };

  const dedup = () => {
    const seen = new Map(); let removed = 0; const keep = [];
    samples.forEach(s => { const k = `${s.label}|${s.values.length}|${s.hash}`; if (!seen.has(k)) { seen.set(k, true); keep.push(s); } else removed++; });
    setSamples(keep);
    showToast(removed > 0 ? `Removed ${removed} duplicate${removed !== 1 ? 's' : ''}` : 'No duplicates found', removed > 0 ? 'success' : 'info');
  };

  const splitDo = (sample, parts, partLabels, meta = {}) => {
    const ns = parts.map((values, i) => {
      const seg = meta.segments?.[i] || null;
      return {
        ...sample,
        id: uid(),
        label: partLabels[i] !== undefined && partLabels[i] !== '' ? partLabels[i] : sample.label,
        filename: `${sample.label}_part${i + 1}.json`,
        values,
        duration_ms: values.length * sample.interval_ms,
        hash: simpleHash(values),
        sampleName: sample.sampleName ? `${sample.sampleName}.part${i + 1}` : `part${i + 1}`,
        splitBaseId: sample.id,
        splitPartIndex: i + 1,
        splitRange: seg ? { start: seg.start, end: seg.end } : null,
      };
    });
    setSamples(p => p.map(s => s.id === sample.id ? { ...s, hiddenAfterSplit: true } : s).concat(ns));
    setSelectedIds(new Set(ns.map(s => s.id)));
    setActiveId(ns[0]?.id || sample.id);

    if (meta?.segments?.length) {
      setSplitHighlightsByBase(prev => ({
        ...prev,
        [sample.id]: {
          baseId: sample.id,
          baseLabel: sample.label,
          createdAt: Date.now(),
          segments: meta.segments.map((seg, i) => ({
            ...seg,
            label: ns[i]?.label || sample.label,
            filename: ns[i]?.filename || `${sample.label}_part${i + 1}.json`,
            sampleId: ns[i]?.id,
          })),
        },
      }));
    }

    showToast(`Split → ${parts.length} segments (source hidden)`, 'success');
  };

  const batchSplitDo = (results, partLabels) => {
    const allNew = [];
    const newHighlights = {};
    const sourceIds = new Set();
    results.forEach(r => {
      if (r.parts.length < 2) { return; }
      sourceIds.add(r.sample.id);
      r.parts.forEach((values, i) => {
        const seg = r.meta?.segments?.[i] || null;
        allNew.push({
          ...r.sample,
          id: uid(),
          label: partLabels?.[i] !== undefined && partLabels?.[i] !== '' ? partLabels[i] : r.sample.label,
          filename: `${r.sample.label}_part${i + 1}.json`,
          values,
          duration_ms: values.length * r.sample.interval_ms,
          hash: simpleHash(values),
          sampleName: r.sample.sampleName ? `${r.sample.sampleName}.part${i + 1}` : `part${i + 1}`,
          splitBaseId: r.sample.id,
          splitPartIndex: i + 1,
          splitRange: seg ? { start: seg.start, end: seg.end } : null,
        });
      });

      if (r.meta?.segments?.length) {
        newHighlights[r.sample.id] = {
          baseId: r.sample.id,
          baseLabel: r.sample.label,
          createdAt: Date.now(),
          segments: r.meta.segments.map((seg, i) => {
            const created = allNew[allNew.length - r.parts.length + i];
            return {
              ...seg,
              label: created?.label || r.sample.label,
              filename: created?.filename || `${r.sample.label}_part${i + 1}.json`,
              sampleId: created?.id,
            };
          }),
        };
      }
    });
    setSamples(p => p.map(s => sourceIds.has(s.id) ? { ...s, hiddenAfterSplit: true } : s).concat(allNew));
    if (Object.keys(newHighlights).length) {
      setSplitHighlightsByBase(prev => ({ ...prev, ...newHighlights }));
    }
    setSelectedIds(new Set(allNew.map(s => s.id)));
    if (allNew.length) setActiveId(allNew[0].id);
    showToast(`Batch split → ${allNew.length} segments (sources hidden)`, 'success');
  };

  const exportSelected = async () => {
    const toExport = selectedSamples.filter(s => !s.fromLabels && s.values.length > 0 && getSampleType(s) !== 'trimmed');
    if (!toExport.length) { showToast('No exportable samples selected', 'error'); return; }

    setLoading({ label: 'Building EI export ZIP', sub: `${toExport.length} samples` });
    try {
      const zip = new JSZip();
      const usedPaths = new Set();
      const normalize = name => {
        const base = String(name || 'sample.json').replace(/[\\/:*?"<>|]+/g, '_').replace(/\s+/g, '_');
        return base.toLowerCase().endsWith('.json') ? base : `${base}.json`;
      };
      const makeEIFilename = (s, idx) => {
        const labelPart = (s.label || 'unknown').replace(/\s+/g, '_').toLowerCase();
        const namePart  = s.sampleName ? `.${s.sampleName.replace(/\s+/g, '_')}` : `_${idx + 1}`;
        return `${labelPart}${namePart}.json`;
      };

      const manifestFiles = toExport.map((s, i) => {
        const category = s.category === 'testing' ? 'testing' : 'training';
        const baseName = normalize(makeEIFilename(s, i));
        let finalName = baseName, k = 2;
        while (usedPaths.has(`${category}/${finalName}`)) {
          finalName = baseName.replace(/\.json$/i, `_${k++}.json`);
        }
        const relPath = `${category}/${finalName}`;
        usedPaths.add(relPath);
        zip.file(relPath, JSON.stringify(buildEIJson(s), null, 2));
        return { path: relPath, category, name: s.sampleName || finalName.replace(/\.json$/i, ''), label: s.label, enabled: s.enabled !== false, length: s.values.length };
      });

      zip.file('info.labels', JSON.stringify({
        version: 1,
        files: manifestFiles.map(f => ({
          path: f.path, name: f.name, category: f.category,
          label: { type: 'label', label: f.label },
          enabled: f.enabled, length: f.length, metadata: {},
        })),
      }, null, 2));

      const blob = await zip.generateAsync({ type: 'blob' });
      const stamp = new Date().toISOString().replace(/[:.]/g, '-');
      const url1 = URL.createObjectURL(blob);
      const a1 = Object.assign(document.createElement('a'), { href: url1, download: `ei_export_${stamp}.zip` });
      document.body.appendChild(a1); a1.click(); document.body.removeChild(a1);
      setTimeout(() => URL.revokeObjectURL(url1), 1000);

      const train = manifestFiles.filter(f => f.category === 'training').length;
      const test  = manifestFiles.filter(f => f.category === 'testing').length;
      showToast(`Exported ${toExport.length} samples (${train} train / ${test} test) + info.labels`, 'success');
    } catch (err) {
      showToast(`Export failed: ${err.message}`, 'error');
    }
    setLoading(null);
  };

  const exportSplitSamplesZip = async () => {
    // Export ALL non-hidden, non-ref samples (segments + originals that aren't hidden)
    // in proper EI format: testing/ training/ directories + info.labels
    const toExport = samples.filter(s =>
      !s.fromLabels && s.values.length > 0 && !s.hiddenAfterSplit
    );
    if (!toExport.length) {
      showToast('No samples available to export', 'error');
      return;
    }

    setLoading({ label: 'Building EI export ZIP', sub: `${toExport.length} samples` });
    try {
      const zip = new JSZip();
      const usedPaths = new Set();

      const normalize = (name) => {
        const base = String(name || 'sample.json')
          .replace(/[\\/:*?"<>|]+/g, '_')
          .replace(/\s+/g, '_');
        return base.toLowerCase().endsWith('.json') ? base : `${base}.json`;
      };

      // Build EI-compatible filename: label.json.HASH.ingestion-0.s0.json
      // or simpler: label.samplename.json in category directory
      const makeEIFilename = (s, idx) => {
        const labelPart = (s.label || 'unknown').replace(/\s+/g, '_').toLowerCase();
        const namePart  = s.sampleName ? `.${s.sampleName.replace(/\s+/g, '_')}` : `_${idx + 1}`;
        return `${labelPart}${namePart}.json`;
      };

      const manifestFiles = toExport.map((s, i) => {
        const category = s.category === 'testing' ? 'testing' : 'training';
        const baseName = normalize(makeEIFilename(s, i));
        let finalName = baseName;
        let k = 2;
        while (usedPaths.has(`${category}/${finalName}`)) {
          finalName = baseName.replace(/\.json$/i, `_${k}.json`);
          k++;
        }
        const relPath = `${category}/${finalName}`;
        usedPaths.add(relPath);

        // Write sample JSON — EI expects payload-wrapped format
        zip.file(relPath, JSON.stringify(buildEIJson(s), null, 2));

        return {
          path: relPath,
          category,
          name: s.sampleName || finalName.replace(/\.json$/i, ''),
          label: s.label,
          enabled: s.enabled !== false,
          length: s.values.length,
          interval_ms: s.interval_ms,
        };
      });

      // Build info.labels in exact EI format
      const labelsObj = {
        version: 1,
        files: manifestFiles.map(f => ({
          path: f.path,
          name: f.name,
          category: f.category,
          label: { type: 'label', label: f.label },
          enabled: f.enabled,
          length: f.length,
          metadata: {},
        })),
      };
      zip.file('info.labels', JSON.stringify(labelsObj, null, 2));

      const blob = await zip.generateAsync({ type: 'blob' });
      const stamp = new Date().toISOString().replace(/[:.]/g, '-');
      const url = URL.createObjectURL(blob);
      const a2 = Object.assign(document.createElement('a'), { href: url, download: `ei_export_${stamp}.zip` });
      document.body.appendChild(a2); a2.click(); document.body.removeChild(a2);
      setTimeout(() => URL.revokeObjectURL(url), 1000);

      const train = manifestFiles.filter(f => f.category === 'training').length;
      const test  = manifestFiles.filter(f => f.category === 'testing').length;
      showToast(`Exported ${toExport.length} samples (${train} train / ${test} test) + info.labels`, 'success');
    } catch (err) {
      showToast(`Export failed: ${err.message}`, 'error');
    }
    setLoading(null);
  };

  const saveProject = async () => {
    setLoading({ label: 'Saving project', sub: `${samples.length} samples` });
    try {
      const zip = new JSZip();
      const sampleEntries = [];

      samples.forEach((s, i) => {
        const sid = s.id ?? uid();
        const rel = `samples/${i + 1}.json`;
        sampleEntries.push({ id: sid, file: rel });
        const samplePayload = { ...s, id: sid, raw: undefined };
        zip.file(rel, JSON.stringify(samplePayload));
      });

      const manifest = {
        type: 'ei-studio-project',
        version: 2,
        savedAt: Date.now(),
        state: {
          samples: sampleEntries,
          selectedIds: [...selectedIds],
          activeId,
          filterLabel, filterStatus, filterCategory, timeRange, tab,
          sidebarViewMode, splitHighlightsByBase, splitHighlightSensors,
          visibleSampleTypes: [...visibleSampleTypes],
          labelsManifest: labelsManifestRef.current,
        },
      };

      zip.file('project.json', JSON.stringify(manifest));
      const blob = await zip.generateAsync({ type: 'blob' });
      const stamp = new Date().toISOString().replace(/[:.]/g, '-');
      const url = URL.createObjectURL(blob);
      const a3 = Object.assign(document.createElement('a'), { href: url, download: `ei_studio_project_${stamp}.eisproj.zip` });
      document.body.appendChild(a3); a3.click(); document.body.removeChild(a3);
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      showToast(`Project saved (${samples.length} samples)`, 'success');
    } catch (err) {
      showToast(`Save project failed: ${err.message}`, 'error');
    }
    setLoading(null);
  };

  const openProject = useCallback(async (file) => {
    setLoading({ label: 'Opening project', sub: file.name });
    try {
      const k = (v) => String(v);
      const enforceUniqueAndRepairRefs = (loadedSamples, state) => {
        // normalizeSample already gave every sample a fresh uid().
        // _loadedId holds the original persisted id for building the remapping.
        const items = loadedSamples.map((s) => ({
          s,
          oldId: s._loadedId ?? s.id,  // original saved id
          newId: s.id,                  // already-fresh id from normalizeSample
        }));

        const oldToAnyNew = new Map();
        const oldToBaseNew = new Map();
        items.forEach((it) => {
          const oldKey = k(it.oldId);
          if (!oldToAnyNew.has(oldKey)) oldToAnyNew.set(oldKey, it.newId);
          if (!it.s.splitBaseId && !oldToBaseNew.has(oldKey)) oldToBaseNew.set(oldKey, it.newId);
        });

        const mapId = (id, preferBase = false) => {
          const key = k(id);
          if (preferBase && oldToBaseNew.has(key)) return oldToBaseNew.get(key);
          if (oldToAnyNew.has(key)) return oldToAnyNew.get(key);
          return null;
        };

        const repairedSamples = items.map((it) => {
          const { _loadedId, ...rest } = it.s;  // strip temp field
          const nextSplitBase = rest.splitBaseId != null
            ? (mapId(rest.splitBaseId, true) ?? mapId(rest.splitBaseId, false))
            : rest.splitBaseId;
          return { ...rest, splitBaseId: nextSplitBase };
        });

        const remappedSelected = Array.isArray(state.selectedIds)
          ? state.selectedIds.map(id => mapId(id, false)).filter(Boolean)
          : [];
        const remappedActive = state.activeId != null ? (mapId(state.activeId, false) ?? null) : null;

        const remappedHighlights = {};
        const srcHighlights = state.splitHighlightsByBase && typeof state.splitHighlightsByBase === 'object'
          ? state.splitHighlightsByBase
          : {};

        Object.entries(srcHighlights).forEach(([baseId, meta]) => {
          const mappedBaseId = mapId(baseId, true) ?? mapId(baseId, false);
          if (mappedBaseId == null || !meta) return;
          remappedHighlights[mappedBaseId] = {
            ...meta,
            baseId: mappedBaseId,
            segments: (meta.segments || []).map(seg => ({
              ...seg,
              sampleId: seg.sampleId != null ? (mapId(seg.sampleId, false) ?? seg.sampleId) : seg.sampleId,
            })),
          };
        });

        return {
          samples: repairedSamples,
          selectedIds: remappedSelected,
          activeId: remappedActive,
          splitHighlightsByBase: remappedHighlights,
        };
      };

      const normalizeSample = (s) => {
        if (!s || !Array.isArray(s.values) || !Array.isArray(s.sensors)) return null;
        const interval = Number(s.interval_ms) || 33.33;
        const values = s.values;
        // IMPORTANT: do NOT keep the old numeric id — always generate a fresh one.
        // The old id is preserved in _loadedId so enforceUniqueAndRepairRefs can
        // build a correct old→new mapping for splitBaseId and splitHighlights.
        return {
          ...s,
          _loadedId: s.id,   // stash original for reference repair
          id: uid(),         // always fresh — prevents collision after project reload
          interval_ms: interval,
          values,
          duration_ms: Number.isFinite(Number(s.duration_ms)) ? Number(s.duration_ms) : values.length * interval,
          hash: Number.isFinite(Number(s.hash)) ? Number(s.hash) : simpleHash(values),
        };
      };

      let parsed = null;
      let st = null;
      let nextSamples = [];

      const isZipLike = /\.zip$/i.test(file.name || '');
      if (isZipLike) {
        const zip = await JSZip.loadAsync(file);
        const manifestFile = zip.file('project.json') || Object.values(zip.files).find(f => !f.dir && /project\.json$/i.test(f.name));
        if (!manifestFile) {
          showToast('Invalid project ZIP: missing project.json', 'error');
          return;
        }
        parsed = JSON.parse(await manifestFile.async('text'));
        if (!parsed || parsed.type !== 'ei-studio-project' || !parsed.state) {
          showToast('Invalid project manifest', 'error');
          return;
        }
        st = parsed.state;

        const sampleRefs = Array.isArray(st.samples) ? st.samples : [];
        const loaded = await Promise.all(sampleRefs.map(async (ref) => {
          const entry = zip.file(String(ref.file || ''));
          if (!entry) return null;
          const content = await entry.async('text');
          return normalizeSample(JSON.parse(content));
        }));
        nextSamples = loaded.filter(Boolean);
      } else {
        // Legacy single JSON project file support.
        const text = await file.text();
        parsed = JSON.parse(text);
        if (!parsed || parsed.type !== 'ei-studio-project' || !parsed.state) {
          showToast('Invalid project file format', 'error');
          return;
        }
        st = parsed.state;
        const nextSamplesRaw = Array.isArray(st.samples) ? st.samples : [];
        nextSamples = nextSamplesRaw.map(normalizeSample).filter(Boolean);
      }

      const repaired = enforceUniqueAndRepairRefs(nextSamples, st || {});
      nextSamples = repaired.samples;

      setSamples(nextSamples);
      setSelectedIds(new Set(repaired.selectedIds));
      setActiveId(repaired.activeId);
      setFilterLabel(st.filterLabel || 'all');
      setFilterStatus(st.filterStatus || 'all');
      setFilterCategory(st.filterCategory || 'all');
      setTimeRange(Array.isArray(st.timeRange) && st.timeRange.length === 2 ? st.timeRange : [0, 100]);
      setTab(st.tab || 'waveform');
      setSidebarViewMode(st.sidebarViewMode === 'grid' ? 'grid' : 'list');
      setSplitHighlightsByBase(repaired.splitHighlightsByBase);
      setSplitHighlightSensors(
        Array.isArray(st.splitHighlightSensors)
          ? st.splitHighlightSensors
          : st.splitHighlightSensor
            ? [st.splitHighlightSensor]
            : []
      );
      setVisibleSampleTypes(
        Array.isArray(st.visibleSampleTypes) && st.visibleSampleTypes.length
          ? new Set(st.visibleSampleTypes)
          : new Set(['unmodified', 'segment'])
      );
      labelsManifestRef.current = Array.isArray(st.labelsManifest) ? st.labelsManifest : [];

      // close transient UI states
      setSplitTarget(null);
      setBatchSplitTargets(null);
      setLabelsPayload(null);

      showToast(`Project opened (${nextSamples.length} samples)`, 'success');
    } catch (err) {
      showToast(`Open project failed: ${err.message}`, 'error');
    }
    setLoading(null);
  }, [showToast]);

  const activeBaseId = activeSample ? (activeSample.splitBaseId || activeSample.id) : null;
  const activeBaseSample = activeBaseId ? samplesById.get(activeBaseId) : null;
  const activeSplitMeta = activeBaseId ? splitHighlightsByBase[activeBaseId] : null;
  const splitPreferredSensors = useMemo(() => {
    if (!activeBaseSample || !activeBaseSample.values?.length || !sensors.length) return [];
    const picks = pickBestChannels(activeBaseSample.values, sensors, Math.min(4, sensors.length));
    return picks.length ? picks : sensors.slice(0, Math.min(4, sensors.length));
  }, [activeBaseSample, sensors]);

  const activeSegmentSampleId = activeSample?.splitBaseId ? activeSample.id : null;

  useEffect(() => {
    if (!activeBaseSample || !sensors.length) {
      setSplitHighlightSensors([]);
      return;
    }
    setSplitHighlightSensors(prev => {
      const valid = (prev || []).filter(s => sensors.includes(s));
      if (valid.length) return valid;
      return splitPreferredSensors.length ? splitPreferredSensors : [sensors[0]];
    });
  }, [activeBaseSample, sensors, splitPreferredSensors]);

  const updateSplitSegmentRange = useCallback((sampleId, newStart, newEnd) => {
    if (!activeBaseSample?.values?.length || !sampleId) return;
    const start = Math.max(0, Math.min(activeBaseSample.values.length - 1, Math.round(newStart)));
    const end = Math.max(start + 1, Math.min(activeBaseSample.values.length, Math.round(newEnd)));

    setSamples(prev => prev.map(s => {
      if (s.id !== sampleId) return s;
      const nextValues = activeBaseSample.values.slice(start, end);
      return {
        ...s,
        values: nextValues,
        duration_ms: nextValues.length * s.interval_ms,
        hash: simpleHash(nextValues),
        splitRange: { start, end },
      };
    }));

    setSplitHighlightsByBase(prev => {
      if (!activeBaseId || !prev[activeBaseId]) return prev;
      return {
        ...prev,
        [activeBaseId]: {
          ...prev[activeBaseId],
          segments: (prev[activeBaseId].segments || []).map(seg => (
            seg.sampleId === sampleId ? { ...seg, start, end, length: end - start } : seg
          )),
        },
      };
    });
  }, [activeBaseSample, activeBaseId]);

  const importLabelsEntries = (entries) => {
    const ns = entries.map(e => ({ filename: String(e.filename || e.path || ''), path: String(e.path || ''), sampleName: String(e.name || ''), label: String(e.label), sensors: [], interval_ms: 33.33, values: [], raw: {}, id: uid(), enabled: e.enabled !== false, category: e.category === 'testing' ? 'testing' : 'training', duration_ms: Number(e.length || 0) * 33.33, hash: uid(), fromLabels: true }));
    setSamples(p => [...p, ...ns]);
    showToast(`Imported ${ns.length} label entries`, 'success');
  };

  // ── Batch split: selected non-ref data samples ─────────────────────────────
  const openBatchSplit = () => {
    const eligible = selectedSamples.filter(s => !s.fromLabels && !s.splitBaseId && s.values.length > 10);
    if (eligible.length < 2) { showToast('Select 2+ non-segment data samples for batch split', 'error'); return; }
    setBatchSplitTargets(eligible);
  };

  const toggleVisibleType = (type) => {
    setVisibleSampleTypes(prev => {
      const n = new Set(prev);
      if (n.has(type)) {
        if (n.size <= 1) return n;
        n.delete(type);
      } else {
        n.add(type);
      }
      return n;
    });
  };

  // ── Icon button helpers ──────────────────────────────────────────────────
  const Btn = (bg, fg, disabled = false) => ({
    background: disabled ? theme.bgCard : bg, color: disabled ? theme.textDim : fg,
    border: 'none', borderRadius: 5, padding: '5px 11px',
    cursor: disabled ? 'not-allowed' : 'pointer', fontSize: 11,
    fontFamily: 'inherit', whiteSpace: 'nowrap', fontWeight: 600, opacity: disabled ? 0.5 : 1,
  });

  const TopBtn = ({ icon, label, onClick, disabled, color, bg, border, title }) => (
    <button onClick={onClick} disabled={disabled} title={title}
      style={{
        display: 'flex', alignItems: 'center', gap: 5,
        background: disabled ? theme.bgCard : bg,
        border: `1px solid ${disabled ? theme.border : border}`,
        color: disabled ? theme.textDim : color,
        borderRadius: 6, padding: '5px 10px',
        cursor: disabled ? 'not-allowed' : 'pointer',
        fontSize: 11, fontFamily: 'inherit', fontWeight: 600,
        whiteSpace: 'nowrap', opacity: disabled ? 0.5 : 1,
        transition: 'opacity 0.15s',
      }}
    >
      {icon}
      <span>{label}</span>
    </button>
  );

  const SideBtn = ({ icon, label, onClick, disabled, color, bg, fullWidth }) => (
    <button onClick={onClick} disabled={disabled}
      style={{
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 4,
        ...(fullWidth ? { width: '100%' } : { flex: 1 }),
        background: disabled ? theme.bgCard : (bg || theme.bgCard),
        border: `1px solid ${disabled ? theme.border : theme.border}`,
        color: disabled ? theme.textDim : (color || theme.textSecondary),
        borderRadius: 5, padding: '4px 6px',
        cursor: disabled ? 'not-allowed' : 'pointer',
        fontSize: 10, fontFamily: 'inherit', fontWeight: 600,
        whiteSpace: 'nowrap', opacity: disabled ? 0.5 : 1,
      }}
    >
      {icon}
      <span>{label}</span>
    </button>
  );

  const IcoBtn = ({ icon, label, onClick, color, bg, border }) => (
    <button onClick={onClick}
      style={{
        display: 'flex', alignItems: 'center', gap: 5,
        background: bg || theme.bgCard, border: `1px solid ${border || theme.border}`,
        color: color || theme.textSecondary, borderRadius: 6, padding: '5px 10px',
        cursor: 'pointer', fontSize: 11, fontFamily: 'inherit', fontWeight: 600,
      }}
    >
      {icon}
      <span>{label}</span>
    </button>
  );

  return (
    <ThemeContext.Provider value={theme}>
    <div style={{ fontFamily: "'JetBrains Mono', monospace", background: theme.bgBase, minHeight: '100vh', color: theme.textPrimary }}>
      <style>{`
        @keyframes spin { to { transform: rotate(360deg); } }
        ::-webkit-scrollbar { width: 5px; height: 5px; }
        ::-webkit-scrollbar-track { background: ${theme.scrollTrack}; }
        ::-webkit-scrollbar-thumb { background: ${theme.scrollThumb}; border-radius: 3px; }
        ::-webkit-scrollbar-thumb:hover { background: ${theme.accent}; }
      `}</style>

      {/* TOP BAR */}
      <div style={{
        background: theme.bgTopbar,
        borderBottom: `1px solid ${theme.border}`,
        padding: '0 14px',
        display: 'flex', alignItems: 'center', gap: 8,
        height: 50, flexShrink: 0,
        boxShadow: `0 1px 0 ${theme.border}`,
      }}>
        {/* Logo */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginRight: 6 }}>
          <Zap size={18} color={theme.accent} strokeWidth={2.5} />
          <span style={{ fontSize: 15, fontWeight: 900, letterSpacing: -0.5, color: theme.textPrimary }}>
            EI<span style={{ color: theme.accent }}>Studio</span>
          </span>
        </div>
        <div style={{ width: 1, height: 24, background: theme.border, marginRight: 2 }} />

        {/* File actions */}
        <TopBtn icon={<Save size={13} />} label="Save" onClick={saveProject} disabled={!samples.length}
          color="#a78bfa" bg={theme.bgCard} border="#4c1d95" title="Save project" theme={theme} />
        <TopBtn icon={<FolderOpen size={13} />} label="Open" onClick={() => projectRef.current?.click()}
          color={theme.textSecondary} bg={theme.bgCard} border={theme.border} title="Open saved project" theme={theme} />
        <div style={{ width: 1, height: 24, background: theme.border }} />

        {/* Import mode selector */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 3, background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 6, padding: '2px 3px' }}>
          {[['merge', 'Merge'], ['add', 'Add'], ['replace', 'Replace']].map(([m, l]) => (
            <button key={m} onClick={() => setImportMergeMode(m)} title={
              m === 'merge' ? 'Skip duplicates (by filename/hash), update label changes' :
              m === 'add'   ? 'Always add all samples, no dedup' :
              'Replace all existing samples with new ones'
            } style={{
              background: importMergeMode === m ? theme.accent + '33' : 'transparent',
              border: `1px solid ${importMergeMode === m ? theme.accent : 'transparent'}`,
              color: importMergeMode === m ? theme.accent : theme.textDim,
              borderRadius: 4, padding: '2px 7px', cursor: 'pointer', fontSize: 9, fontFamily: 'inherit', fontWeight: importMergeMode === m ? 700 : 400,
            }}>{l}</button>
          ))}
        </div>

        <TopBtn icon={<Package size={13} />} label="Import ZIP" onClick={() => zipRef.current?.click()}
          color="#34d399" bg={theme.bgCard} border="#065f46" title="Import EdgeImpulse dataset ZIP" theme={theme} />
        <TopBtn icon={<FileJson size={13} />} label="Import JSON" onClick={() => jsonImportRef.current?.click()}
          color="#38bdf8" bg={theme.bgCard} border="#0e4a6a" title="Import individual .json sample files with label assignment" theme={theme} />
        <TopBtn
          icon={<Download size={13} />} label="Export ZIP"
          onClick={exportSplitSamplesZip}
          disabled={!samples.some(s => !s.hiddenAfterSplit && !s.fromLabels && s.values.length > 0)}
          color="#93c5fd" bg={theme.bgCard} border={theme.borderHi}
          title="Export all samples as EI-compatible ZIP" theme={theme}
        />
        <TopBtn icon={<FileJson size={13} />} label="Files" onClick={() => fileRef.current?.click()}
          color={theme.accentAlt} bg={theme.bgCard} border={theme.borderHi} title="Open individual .json/.labels files" theme={theme} />

        <div style={{ flex: 1 }} />

        {/* Stats */}
        <div style={{ display: 'flex', gap: 12, fontSize: 10, color: theme.textFaint, fontFamily: 'monospace' }}>
          <span>{samples.length} <span style={{ color: theme.textDim }}>samples</span></span>
          <span>{sensors.length} <span style={{ color: theme.textDim }}>channels</span></span>
        </div>

        {/* Theme toggle */}
        <button onClick={() => setThemeName(t => t === 'dark' ? 'light' : 'dark')}
          title={`Switch to ${themeName === 'dark' ? 'light' : 'dark'} mode`}
          style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 30, height: 30, background: theme.bgCard, border: `1px solid ${theme.border}`, color: theme.textMuted, borderRadius: 6, cursor: 'pointer' }}>
          {themeName === 'dark' ? <Sun size={14} /> : <Moon size={14} />}
        </button>

        {/* Hidden file inputs */}
        <input ref={projectRef} type="file" accept=".eisproj,.eisproj.json,.json,.zip,.eisproj.zip"
          style={{ display: 'none' }} onChange={e => { if (e.target.files[0]) openProject(e.target.files[0]); e.target.value = ''; }} />
        <input ref={zipRef} type="file" accept=".zip" style={{ display: 'none' }}
          onChange={e => { if (e.target.files[0]) handleZip(e.target.files[0]); e.target.value = ''; }} />
        <input ref={jsonImportRef} type="file" accept=".json" multiple style={{ display: 'none' }}
          onChange={e => { if (e.target.files.length) { setJsonImportFiles(Array.from(e.target.files)); } e.target.value = ''; }} />
        <input ref={fileRef} type="file" accept=".json,.labels" multiple style={{ display: 'none' }}
          onChange={e => { processFiles(Array.from(e.target.files)); e.target.value = ''; }} />
      </div>

      {/* LOADING OVERLAY */}
      <LoadingOverlay loading={loading} />

      {/* JSON IMPORT MODAL */}
      {jsonImportFiles && (
        <JsonImportModal
          files={jsonImportFiles}
          existingSamples={samples}
          onImport={newSamples => {
            setSamples(prev => {
              if (importMergeMode === 'replace') return newSamples;
              if (importMergeMode === 'merge') {
                const existingByFilename = new Map(prev.map(s => [s.filename, s]));
                const existingByHash     = new Map(prev.filter(s => s.hash).map(s => [s.hash, s]));
                let added = 0, skipped = 0;
                const result = [...prev];
                newSamples.forEach(ns => {
                  if (existingByFilename.has(ns.filename) || existingByHash.has(ns.hash)) {
                    skipped++;
                  } else {
                    result.push(ns);
                    added++;
                  }
                });
                showToast(`Import: ${added} added · ${skipped} skipped (duplicates)`, 'success');
                return result;
              }
              return [...prev, ...newSamples];
            });
            if (importMergeMode !== 'merge') {
              showToast(`Imported ${newSamples.length} sample${newSamples.length !== 1 ? 's' : ''}`, 'success');
            }
            setActiveId(newSamples[0]?.id ?? null);
          }}
          onClose={() => setJsonImportFiles(null)}
        />
      )}

      {/* TOAST */}
      {toast && (
        <div style={{
          position: 'fixed', top: 60, right: 16, zIndex: 400,
          background: toast.type === 'success' ? '#041e10' : toast.type === 'error' ? '#1f0a0a' : theme.bgHover,
          border: `1px solid ${toast.type === 'success' ? '#16a34a' : toast.type === 'error' ? '#dc2626' : '#2563eb'}`,
          color: toast.type === 'success' ? '#4ade80' : toast.type === 'error' ? '#f87171' : '#93c5fd',
          borderRadius: 8, padding: '10px 16px', fontSize: 12,
          boxShadow: '0 8px 32px #00000066', maxWidth: 400,
          display: 'flex', alignItems: 'center', gap: 8,
          fontFamily: 'inherit',
          animation: 'slideIn 0.2s ease',
        }}>
          <style>{`@keyframes slideIn { from { opacity:0; transform:translateX(12px); } to { opacity:1; transform:translateX(0); } }`}</style>
          <div style={{ width: 6, height: 6, borderRadius: '50%', background: 'currentColor', flexShrink: 0 }} />
          {toast.msg}
        </div>
      )}

      {/* MODALS */}
      {splitTarget && (
        <SplitModal sample={splitTarget} allSamples={samples} onSplit={splitDo} onClose={() => setSplitTarget(null)} />
      )}
      {batchSplitTargets && (
        <SplitModal samples={batchSplitTargets} allSamples={samples} onBatchSplit={batchSplitDo} onClose={() => setBatchSplitTargets(null)} />
      )}
      {labelsPayload && (
        <LabelsModal labelsData={labelsPayload} onImport={importLabelsEntries} onClose={() => setLabelsPayload(null)} />
      )}

      <div style={{ display: 'flex', height: 'calc(100vh - 50px)' }}>

        {/* ── SIDEBAR ── */}
        <div style={{ width: sidebar.width, borderRight: `1px solid ${theme.border}`, display: 'flex', flexDirection: 'column', background: theme.bgSidebar, flexShrink: 0, position: 'relative' }}>

          {/* Drop zone */}
          <div
            onDrop={handleDrop}
            onDragOver={e => { e.preventDefault(); setDragOver(true); }}
            onDragLeave={() => setDragOver(false)}
            onClick={() => zipRef.current?.click()}
            style={{
              margin: '8px 8px 4px',
              padding: '10px 8px',
              borderRadius: 8,
              border: `2px dashed ${dragOver ? '#34d399' : '#1a2a3a'}`,
              display: 'flex', alignItems: 'center', gap: 8,
              cursor: 'pointer',
              background: dragOver ? '#071a10' : 'transparent',
              transition: 'all 0.2s',
            }}
          >
            <Package size={16} color={dragOver ? '#34d399' : '#1e3a5f'} strokeWidth={1.5} />
            <div>
              <div style={{ fontSize: 10, fontWeight: 600, color: dragOver ? '#34d399' : '#334155' }}>
                Drop or click to import
              </div>
              <div style={{ fontSize: 8, color: theme.textFaint, marginTop: 1 }}>
                .zip · info.labels + testing/ + training/
              </div>
            </div>
          </div>

          {/* Filters */}
          <div style={{ padding: '0 8px 6px', borderBottom: `1px solid ${theme.border}`, marginBottom: 5 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 5, fontSize: 9, color: theme.textFaint, marginBottom: 5, letterSpacing: 1 }}>
              <Filter size={10} color="#1e3a5f" /> FILTERS
            </div>
            <select value={filterLabel} onChange={e => setFilterLabel(e.target.value)}
              style={{ width: '100%', background: theme.bgPanel, color: theme.textSecondary, border: `1px solid ${theme.border}`, borderRadius: 5, padding: '4px 6px', fontSize: 10, marginBottom: 4, fontFamily: 'inherit' }}>
              {allLabels.map(l => <option key={l} value={l}>{l}{l !== 'all' ? ` (${(groupedByLabel[l] || []).length})` : ` (${samples.length})`}</option>)}
            </select>
            <div style={{ display: 'flex', gap: 3, marginBottom: 4 }}>
              {['all', 'training', 'testing'].map(v => (
                <button key={v} onClick={() => setFilterCategory(v)} style={{ flex: 1, background: filterCategory === v ? (v === 'testing' ? '#451a0333' : v === 'training' ? '#0d204066' : '#1e293b') : theme.bgPanel, border: `1px solid ${filterCategory === v ? (v === 'testing' ? CATEGORY_COLORS.testing : v === 'training' ? CATEGORY_COLORS.training : '#64748b') : theme.border}`, color: filterCategory === v ? (v === 'testing' ? CATEGORY_COLORS.testing : v === 'training' ? CATEGORY_COLORS.training : theme.textSecondary) : theme.textDim, borderRadius: 4, padding: '3px 0', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
                  {v === 'all' ? 'All' : v === 'training' ? 'Train' : 'Test'}
                </button>
              ))}
            </div>
            <div style={{ display: 'flex', gap: 3, marginBottom: 5 }}>
              {['all', 'enabled', 'disabled'].map(v => (
                <button key={v} onClick={() => setFilterStatus(v)} style={{ flex: 1, background: filterStatus === v ? '#0d2040' : theme.bgPanel, border: `1px solid ${filterStatus === v ? '#2563eb' : theme.border}`, color: filterStatus === v ? '#60a5fa' : '#334155', borderRadius: 4, padding: '3px 0', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>{v}</button>
              ))}
            </div>
            <div style={{ fontSize: 9, color: theme.textDim, marginBottom: 3 }}>View Samples</div>
            <div style={{ display: 'flex', gap: 3, marginBottom: 6, flexWrap: 'wrap' }}>
              {[
                ['unmodified', 'Unmodified'],
                ['segment', 'Segment'],
                ['trimmed', 'Trimmed'],
              ].map(([k, lbl]) => {
                const on = visibleSampleTypes.has(k);
                return (
                  <button
                    key={k}
                    onClick={() => toggleVisibleType(k)}
                    style={{
                      flex: 1,
                      minWidth: 75,
                      background: on ? theme.bgActive : theme.bgPanel,
                      border: `1px solid ${on ? '#3b82f6' : theme.border}`,
                      color: on ? '#60a5fa' : '#334155',
                      borderRadius: 4,
                      padding: '3px 0',
                      fontSize: 9,
                      cursor: 'pointer',
                      fontFamily: 'inherit',
                    }}
                    title={`${lbl} (${sampleTypeCounts[k] || 0})`}
                  >
                    {lbl} ({sampleTypeCounts[k] || 0})
                  </button>
                );
              })}
            </div>
            {samples.length > 0 && (
              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 9, color: theme.textDim, marginBottom: 2 }}>
                  <span>Duration</span>
                  <span style={{ color: theme.textMuted }}>{formatMs(minDur + (maxDur - minDur) * timeRange[0] / 100)} – {formatMs(minDur + (maxDur - minDur) * timeRange[1] / 100)}</span>
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                  <input
                    type="number"
                    value={Math.round(pctToDuration(timeRange[0]))}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      if (!Number.isFinite(v)) return;
                      const p = durationToPct(v);
                      setTimeRange(([lo, hi]) => [Math.min(p, hi - 0.1), hi]);
                    }}
                    min={0}
                    max={Math.round(pctToDuration(timeRange[1]))}
                    step={1}
                    style={{
                      width: 74,
                      background: theme.bgPanel,
                      color: theme.textSecondary,
                      border: `1px solid ${theme.border}`,
                      borderRadius: 4,
                      padding: '2px 4px',
                      fontSize: 9,
                      fontFamily: 'inherit',
                      textAlign: 'center',
                    }}
                    title="Minimum duration (ms)"
                  />
                  <div style={{ flex: 1 }}>
                    <DualRange value={timeRange} onChange={setTimeRange} />
                  </div>
                  <input
                    type="number"
                    value={Math.round(pctToDuration(timeRange[1]))}
                    onChange={(e) => {
                      const v = Number(e.target.value);
                      if (!Number.isFinite(v)) return;
                      const p = durationToPct(v);
                      setTimeRange(([lo, hi]) => [lo, Math.max(p, lo + 0.1)]);
                    }}
                    min={Math.round(pctToDuration(timeRange[0]))}
                    max={Math.round(maxDur)}
                    step={1}
                    style={{
                      width: 74,
                      background: theme.bgPanel,
                      color: theme.textSecondary,
                      border: `1px solid ${theme.border}`,
                      borderRadius: 4,
                      padding: '2px 4px',
                      fontSize: 9,
                      fontFamily: 'inherit',
                      textAlign: 'center',
                    }}
                    title="Maximum duration (ms)"
                  />
                </div>
              </div>
            )}
          </div>

          {/* Search */}
          <div style={{ padding: '0 8px 6px' }}>
            <div style={{ position: 'relative' }}>
              <div style={{ position: 'absolute', left: 8, top: '50%', transform: 'translateY(-50%)', pointerEvents: 'none', display: 'flex', alignItems: 'center' }}>
                <Search size={11} color="#334155" />
              </div>
              <input
                value={sampleSearch}
                onChange={e => setSampleSearch(e.target.value)}
                placeholder="Search label, name, filename…"
                style={{
                  width: '100%', boxSizing: 'border-box',
                  background: theme.bgPanel, color: theme.textPrimary,
                  border: `1px solid ${sampleSearch ? '#3b82f6' : '#1a2540'}`,
                  borderRadius: 6, padding: '5px 24px 5px 26px',
                  fontSize: 10, fontFamily: 'inherit', outline: 'none',
                  transition: 'border-color 0.15s',
                }}
              />
              {sampleSearch && (
                <button
                  onClick={() => setSampleSearch('')}
                  style={{
                    position: 'absolute', right: 6, top: '50%', transform: 'translateY(-50%)',
                    display: 'flex', alignItems: 'center', justifyContent: 'center',
                    background: 'none', border: 'none', color: theme.textMuted,
                    cursor: 'pointer', padding: 2, borderRadius: 3,
                  }}
                >
                  <X size={11} />
                </button>
              )}
            </div>
          </div>

          {/* Actions + view mode toggle */}
          <div style={{ padding: '0 8px 5px' }}>
            <div style={{ display: 'flex', gap: 3, flexWrap: 'wrap', marginBottom: 4 }}>
              <SideBtn icon={<CheckSquare size={11} />} label="All" onClick={selectAll} />
              <SideBtn icon={<Square size={11} />} label="None" onClick={selectNone} />
              <SideBtn icon={<GitMerge size={11} />} label="Merge" onClick={combine}
                disabled={selectedIds.size < 2} color="#60a5fa" bg="#0d2040" />
              <SideBtn icon={<Fingerprint size={11} />} label="DeDup" onClick={dedup}
                color="#c4b5fd" bg="#1a0d3a" />
              <SideBtn icon={<Upload size={11} />} label="Export" onClick={exportSelected}
                disabled={!selectedIds.size} color="#34d399" bg="#0a2018" />
            </div>
            <div style={{ display: 'flex', gap: 3, marginBottom: 4 }}>
              {selectedIds.size >= 2 && (
                <SideBtn
                  icon={<Scissors size={11} />}
                  label={`Batch Split (${selectedIds.size})`}
                  onClick={openBatchSplit}
                  color="#fbbf24" bg="#1a1008" fullWidth
                />
              )}
              {selectedIds.size > 0 && (
                <SideBtn
                  icon={<Trash2 size={11} />}
                  label={`Delete (${selectedIds.size})`}
                  onClick={() => {
                    const ids = [...selectedIds];
                    delBatch(ids);
                    showToast(`Deleted ${ids.length} sample${ids.length !== 1 ? 's' : ''}`, 'info');
                  }}
                  color="#f87171" bg="#1a0808" fullWidth={selectedIds.size < 2}
                />
              )}
            </div>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <span style={{ fontSize: 9, color: theme.textFaint, fontFamily: 'monospace' }}>
                {filteredSamples.length}/{samples.length}
                {sampleSearch && <span style={{ color: '#3b82f6' }}> filtered</span>}
                {selectedIds.size > 0 && <span style={{ color: '#60a5fa' }}> · {selectedIds.size} sel</span>}
              </span>
              <div style={{ display: 'flex', gap: 2 }}>
                <button onClick={() => setSidebarViewMode('list')} title="List view"
                  style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 24, height: 24, background: sidebarViewMode === 'list' ? theme.border : 'transparent', border: `1px solid ${sidebarViewMode === 'list' ? '#3b82f6' : theme.border}`, color: sidebarViewMode === 'list' ? '#60a5fa' : '#334155', borderRadius: 4, cursor: 'pointer' }}>
                  <List size={12} />
                </button>
                <button onClick={() => setSidebarViewMode('grid')} title="Grid view"
                  style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 24, height: 24, background: sidebarViewMode === 'grid' ? theme.border : 'transparent', border: `1px solid ${sidebarViewMode === 'grid' ? '#3b82f6' : theme.border}`, color: sidebarViewMode === 'grid' ? '#60a5fa' : '#334155', borderRadius: 4, cursor: 'pointer' }}>
                  <LayoutGrid size={12} />
                </button>
              </div>
            </div>
          </div>

          {/* Sample list / grid */}
          <div style={{ flex: 1, overflowY: 'auto', padding: sidebarViewMode === 'grid' ? '0 6px 10px' : '0 8px 10px' }}>
            {filteredSamples.length === 0 && (
              <div style={{ color: theme.textFaint, fontSize: 10, textAlign: 'center', paddingTop: 24, lineHeight: 1.9 }}>
                {samples.length ? 'No samples match filters' : <>Drop a <b style={{ color: '#34d399' }}>.zip</b> file<br />(info.labels + testing/ + training/)</>}
              </div>
            )}
            <div style={sidebarViewMode === 'grid' ? { display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(140px, 1fr))', gap: 5, paddingTop: 4 } : {}}>
              {filteredSamples.map(s => (
                <SampleCard key={s.id} sample={s} selected={selectedIds.has(s.id)} active={activeId === s.id}
                  viewMode={sidebarViewMode} sensors={sensors}
                  onSelect={toggleSel} onActivate={activate}
                  onDelete={del} onRename={rename}
                  onToggleEnabled={toggleEnabled} onToggleCategory={toggleCategory}
                  onSplit={setSplitTarget} />
              ))}
            </div>
          </div>

          {/* Resize handle */}
          <div
            onMouseDown={sidebar.onMouseDown}
            style={{ position: 'absolute', top: 0, right: -3, width: 6, height: '100%', cursor: 'col-resize', zIndex: 10, background: 'transparent' }}
            title="Drag to resize sidebar"
          >
            <div style={{ position: 'absolute', top: '50%', right: 1, transform: 'translateY(-50%)', width: 3, height: 40, background: theme.border, borderRadius: 2, transition: 'background 0.15s' }}
              onMouseEnter={e => e.currentTarget.style.background = '#3b82f6'}
              onMouseLeave={e => e.currentTarget.style.background = theme.border} />
          </div>
        </div>

        {/* ── MAIN PANEL ── */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden', minWidth: 0 }}>
          <div style={{ background: theme.bgTopbar, borderBottom: `1px solid ${theme.border}`, display: 'flex', padding: '0 16px', flexShrink: 0 }}>
            {[['waveform', 'Waveform'], ['groups', 'Groups'], ['stats', 'Stats'], ['windows', 'Windows'], ['predict', 'Predict']].map(([t, l]) => (
              <button key={t} onClick={() => setTab(t)} style={{ background: 'none', border: 'none', borderBottom: `2px solid ${tab === t ? theme.accent : 'transparent'}`, color: tab === t ? theme.accent : theme.textDim, padding: '11px 14px', cursor: 'pointer', fontSize: 11, fontFamily: 'inherit', fontWeight: tab === t ? 700 : 400, whiteSpace: 'nowrap' }}>{l}</button>
            ))}
          </div>

          <div style={{ flex: 1, overflowY: 'auto', padding: 16 }}>

            {/* ── WAVEFORM ── */}
            {tab === 'waveform' && (
              <div>
                {activeSample && !activeSample.fromLabels && activeSample.values.length > 0 && (
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 12, padding: '8px 12px', background: theme.bgPanel, border: `1px solid ${activeSample.splitBaseId ? '#1e3a5f' : '#1e3a5f'}`, borderRadius: 8, flexWrap: 'wrap' }}>
                    <span style={{ fontSize: 11, color: '#38bdf8', fontWeight: 700, flex: 1, minWidth: 0 }}>
                      <span style={{ color: theme.textSecondary, fontWeight: 400 }}>Active: </span>
                      <span style={{ color: theme.textPrimary }}>{activeSample.label}</span>
                      {activeSample.sampleName && <span style={{ color: theme.textMuted, fontWeight: 400 }}> · {activeSample.sampleName}</span>}
                      {activeSample.splitBaseId && (
                        <span style={{ marginLeft: 8, fontSize: 9, background: '#0d1f1a', color: '#2dd4bf', border: '1px solid #0f4a40', borderRadius: 3, padding: '1px 6px', fontWeight: 400 }}>
                          segment — cannot be split further
                        </span>
                      )}
                    </span>
                    {/* Only show Split for non-segment samples */}
                    {!activeSample.splitBaseId && (
                      <IcoBtn icon={<Scissors size={12} />} label="Split" onClick={() => setSplitTarget(activeSample)}
                        color="#fbbf24" bg="#1a1008" border="#78350f" />
                    )}
                    <IcoBtn icon={<Trash2 size={12} />} label="Delete" onClick={() => del(activeSample.id)}
                      color="#f87171" bg="#1a0808" border="#7f1d1d" />
                    <IcoBtn icon={<Download size={12} />} label="Export" onClick={() => downloadJSON(buildEIJson(activeSample), activeSample.filename)}
                      color="#34d399" bg="#091a12" border="#065f46" />
                  </div>
                )}
                <WaveformViewer samples={viewSamples} sensors={sensors} />
                {activeSplitMeta && activeBaseSample && activeBaseSample.values.length > 0 && (
                  <div style={{ marginTop: 12 }}>
                    <SplitHighlightsViewer
                      sample={activeBaseSample}
                      sensors={sensors}
                      segments={activeSplitMeta.segments || []}
                      preferredSensor={splitPreferredSensors[0] || null}
                      selectedSensors={splitHighlightSensors}
                      onSelectedSensorsChange={setSplitHighlightSensors}
                      activeSegmentSampleId={activeSegmentSampleId}
                      onUpdateSegmentRange={updateSplitSegmentRange}
                    />
                    <div style={{ marginTop: 8, background: theme.bgPanel, border: `1px solid ${theme.border}`, borderRadius: 8, padding: 10 }}>
                      <div style={{ fontSize: 10, color: theme.textMuted, marginBottom: 6 }}>
                        Split data from base sample <span style={{ color: theme.textPrimary }}>{activeBaseSample.label}</span>
                      </div>
                      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))', gap: 6 }}>
                        {(activeSplitMeta.segments || []).map((seg, i) => (
                          <div
                            key={i}
                            onClick={() => seg.sampleId && setActiveId(seg.sampleId)}
                            style={{
                              background: seg.sampleId === activeSegmentSampleId ? theme.bgActive : theme.bgCard,
                              border: `1px solid ${seg.sampleId === activeSegmentSampleId ? SENSOR_COLORS[i % SENSOR_COLORS.length] : SENSOR_COLORS[i % SENSOR_COLORS.length] + '55'}`,
                              boxShadow: seg.sampleId === activeSegmentSampleId ? `0 0 0 1px ${SENSOR_COLORS[i % SENSOR_COLORS.length]}55` : 'none',
                              borderRadius: 6,
                              padding: 8,
                              cursor: seg.sampleId ? 'pointer' : 'default',
                              opacity: activeSegmentSampleId && seg.sampleId !== activeSegmentSampleId ? 0.55 : 1,
                            }}
                            title={seg.sampleId ? 'Click to focus this split sample' : undefined}
                          >
                            <div style={{ fontSize: 10, color: SENSOR_COLORS[i % SENSOR_COLORS.length], fontWeight: 700, marginBottom: 2 }}>Part {i + 1}</div>
                            <div style={{ fontSize: 9, color: theme.textMuted, lineHeight: 1.5 }}>
                              <div>Label: <span style={{ color: theme.textSecondary }}>{seg.label || activeBaseSample.label}</span></div>
                              <div>Range: <span style={{ color: theme.textSecondary }}>{seg.start} → {seg.end}</span></div>
                              <div>Length: <span style={{ color: theme.textSecondary }}>{seg.length} pts ({formatMs(seg.length * activeBaseSample.interval_ms)})</span></div>
                              <div>File: <span style={{ color: theme.textMuted }}>{seg.filename}</span></div>
                            </div>
                          </div>
                        ))}
                      </div>
                    </div>
                  </div>
                )}
                {viewSamples.length > 0 && (
                  <div style={{ marginTop: 14, display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(175px, 1fr))', gap: 8 }}>
                    {viewSamples.map(s => (
                      <div key={s.id} style={{ background: s.id === activeId ? theme.bgActive : theme.bgPanel, border: `1px solid ${s.id === activeId ? theme.accent : theme.border}`, borderRadius: 8, padding: 12 }}>
                        <div style={{ fontSize: 12, fontWeight: 700, color: '#38bdf8', marginBottom: 3 }}>{s.label}</div>
                        {s.sampleName && <div style={{ fontSize: 9, color: theme.textDim, marginBottom: 3 }}>{s.sampleName}</div>}
                        <div style={{ fontSize: 10, color: theme.textDim, lineHeight: 1.7 }}>
                          <div>Points: <span style={{ color: theme.textMuted }}>{s.values.length}</span></div>
                          <div>Duration: <span style={{ color: theme.textMuted }}>{formatMs(s.duration_ms)}</span></div>
                          <div>Interval: <span style={{ color: theme.textMuted }}>{s.interval_ms.toFixed(2)}ms</span></div>
                          <div>Category: <span style={{ color: s.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training }}>{s.category}</span></div>
                          <div>Status: <span style={{ color: s.enabled ? '#34d399' : '#f87171' }}>{s.enabled ? 'enabled' : 'disabled'}</span></div>
                        </div>
                      </div>
                    ))}
                  </div>
                )}
                {!viewSamples.length && <div style={{ color: theme.textFaint, textAlign: 'center', padding: 60, fontSize: 13 }}>← Click a sample to view its waveform</div>}
              </div>
            )}

            {/* ── GROUPS ── */}
            {tab === 'groups' && (
              <div>
                {Object.entries(groupedByLabel).sort(([a], [b]) => a.localeCompare(b)).map(([label, group]) => {
                  const totalPts = group.reduce((a, s) => a + s.values.length, 0);
                  const totalMs = group.reduce((a, s) => a + s.duration_ms, 0);
                  const tr = group.filter(s => s.category === 'training').length;
                  const te = group.filter(s => s.category === 'testing').length;
                  return (
                    <div key={label} style={{ background: theme.bgPanel, border: `1px solid ${theme.border}`, borderRadius: 10, marginBottom: 10, overflow: 'hidden' }}>
                      <div style={{ padding: '10px 14px', display: 'flex', alignItems: 'center', gap: 8, borderBottom: `1px solid ${theme.border}`, flexWrap: 'wrap' }}>
                        <span style={{ fontWeight: 700, fontSize: 13, color: theme.textPrimary, flex: 1 }}>{label}</span>
                        <span style={{ background: theme.bgActive, color: theme.accent, fontSize: 9, borderRadius: 3, padding: '2px 6px' }}>{group.length} files</span>
                        <span style={{ background: '#052e16', color: '#34d399', fontSize: 9, borderRadius: 3, padding: '2px 6px' }}>{totalPts} pts</span>
                        <span style={{ background: '#1a1a40', color: '#a78bfa', fontSize: 9, borderRadius: 3, padding: '2px 6px' }}>{formatMs(totalMs)}</span>
                        <span style={{ color: CATEGORY_COLORS.training, fontSize: 9 }}>{tr}tr</span>
                        <span style={{ color: CATEGORY_COLORS.testing, fontSize: 9 }}>{te}te</span>
                        <button onClick={() => { const g = group.filter(s => !s.fromLabels && s.values.length > 0); if (g.length < 2) { showToast('Need 2+ data samples', 'error'); return; } const base = g[0]; const m = { ...base, id: uid(), filename: `merged_${label}.json`, sampleName: '', values: g.flatMap(s => s.values), duration_ms: g.reduce((a, s) => a + s.duration_ms, 0), hash: uid() }; setSamples(p => [...p.filter(s => !(s.label === label && !s.fromLabels)), m]); setActiveId(m.id); showToast(`Merged ${g.length} → 1`, 'success'); }} style={Btn('#1d4ed8', '#fff')}>Merge</button>
                        <button onClick={() => group.filter(s => !s.fromLabels && s.values.length > 0).forEach(s => downloadJSON(buildEIJson(s), s.filename))} style={Btn('#065f46', '#34d399')}>Export</button>
                      </div>
                      <div style={{ padding: 8, display: 'flex', flexWrap: 'wrap', gap: 4 }}>
                        {group.map(s => (
                          <div key={s.id} onClick={() => { setActiveId(s.id); setTab('waveform'); }}
                            style={{ background: s.id === activeId ? theme.bgActive : theme.bgCard, borderTop: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? theme.border : '#450a0a'}`, borderRight: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? theme.border : '#450a0a'}`, borderBottom: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? theme.border : '#450a0a'}`, borderLeft: `2px solid ${s.category === 'testing' ? CATEGORY_COLORS.testing + '88' : CATEGORY_COLORS.training + '88'}`, borderRadius: 5, padding: '5px 9px', cursor: 'pointer', fontSize: 9 }}>
                            <div style={{ color: theme.textMuted }}>{s.values.length || '—'}pt</div>
                            <div style={{ color: theme.textDim }}>{formatMs(s.duration_ms)}</div>
                            <div style={{ color: s.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training, fontSize: 8 }}>{s.category === 'testing' ? 'TEST' : 'TRAIN'}</div>
                          </div>
                        ))}
                      </div>
                    </div>
                  );
                })}
                {!Object.keys(groupedByLabel).length && <div style={{ color: theme.textFaint, textAlign: 'center', padding: 60 }}>Import a ZIP to see groups</div>}
              </div>
            )}

            {/* ── STATS ── */}
            {tab === 'stats' && (
              <div>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 8, marginBottom: 16 }}>
                  {[['Samples', samples.length, '#38bdf8'], ['Labels', Object.keys(groupedByLabel).length, '#34d399'], ['Total Points', samples.reduce((a, s) => a + s.values.length, 0).toLocaleString(), '#fbbf24'], ['Total Duration', formatMs(samples.reduce((a, s) => a + s.duration_ms, 0)), '#a78bfa']].map(([k, v, c]) => (
                    <div key={k} style={{ background: theme.bgPanel, border: `1px solid ${c}22`, borderRadius: 10, padding: 14, textAlign: 'center' }}>
                      <div style={{ fontSize: 20, fontWeight: 800, color: c }}>{v}</div>
                      <div style={{ fontSize: 9, color: theme.textDim, marginTop: 2, letterSpacing: 0.5 }}>{k}</div>
                    </div>
                  ))}
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, marginBottom: 16 }}>
                  {['training', 'testing'].map(cat => { const cs = samples.filter(s => s.category === cat); return (
                    <div key={cat} style={{ background: theme.bgPanel, border: `1px solid ${CATEGORY_COLORS[cat]}33`, borderRadius: 10, padding: 14 }}>
                      <div style={{ fontSize: 12, fontWeight: 700, color: CATEGORY_COLORS[cat], marginBottom: 8, textTransform: 'uppercase', letterSpacing: 1 }}>{cat}</div>
                      <div style={{ fontSize: 20, fontWeight: 800, color: theme.textPrimary, marginBottom: 4 }}>{cs.length}</div>
                      <div style={{ fontSize: 10, color: theme.textMuted }}>{cs.filter(s => s.enabled).length} enabled · {cs.filter(s => !s.enabled).length} disabled</div>
                    </div>
                  ); })}
                </div>
                <div style={{ background: theme.bgPanel, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14, marginBottom: 12 }}>
                  <div style={{ fontSize: 11, fontWeight: 700, color: theme.textPrimary, marginBottom: 10 }}>Label Distribution</div>
                  {Object.entries(groupedByLabel).sort(([, a], [, b]) => b.length - a.length).map(([label, group]) => {
                    const pct = Math.round((group.length / samples.length) * 100) || 0;
                    const tr = group.filter(s => s.category === 'training').length;
                    const te = group.filter(s => s.category === 'testing').length;
                    return (
                      <div key={label} style={{ marginBottom: 8 }}>
                        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, marginBottom: 2, gap: 8 }}>
                          <span style={{ color: theme.textSecondary, flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{label}</span>
                          <span style={{ color: CATEGORY_COLORS.training, flexShrink: 0 }}>{tr}tr</span>
                          <span style={{ color: CATEGORY_COLORS.testing, flexShrink: 0 }}>{te}te</span>
                          <span style={{ color: theme.textMuted, flexShrink: 0 }}>({pct}%)</span>
                        </div>
                        <div style={{ height: 5, background: theme.bgCard, borderRadius: 3, overflow: 'hidden', display: 'flex' }}>
                          <div style={{ width: `${(tr / samples.length) * 100}%`, height: '100%', background: CATEGORY_COLORS.training }} />
                          <div style={{ width: `${(te / samples.length) * 100}%`, height: '100%', background: CATEGORY_COLORS.testing }} />
                        </div>
                      </div>
                    );
                  })}
                </div>
                <div style={{ background: theme.bgPanel, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14 }}>
                  <div style={{ fontSize: 11, fontWeight: 700, color: theme.textPrimary, marginBottom: 8 }}>Channels ({sensors.length})</div>
                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: 5 }}>
                    {sensors.map((s, i) => (
                      <span key={s} style={{ background: SENSOR_COLORS[i % SENSOR_COLORS.length] + '18', border: `1px solid ${SENSOR_COLORS[i % SENSOR_COLORS.length]}55`, color: SENSOR_COLORS[i % SENSOR_COLORS.length], borderRadius: 4, padding: '2px 8px', fontSize: 10, fontFamily: 'inherit' }}>{s}</span>
                    ))}
                  </div>
                </div>
              </div>
            )}
            {/* ── WINDOWS ── */}
            {tab === 'windows' && (
              <WindowedCountTab samples={samples} theme={theme} />
            )}

            {/* ── PREDICT ── */}
            {tab === 'predict' && (
              <PredictTab samples={samples} sensors={sensors} activeSample={activeSample} theme={theme} />
            )}
          </div>
        </div>
      </div>
    </div>
    </ThemeContext.Provider>
  );
}
