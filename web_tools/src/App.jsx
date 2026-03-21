import { useState, useCallback, useRef, useMemo, useEffect } from 'react';
import JSZip from 'jszip';
import {
  Save, FolderOpen, Package, Upload, FileJson,
  Scissors, GitMerge, Fingerprint, Download,
  List, LayoutGrid, Zap, ChevronRight,
  CheckSquare, Square, Trash2, Filter,
} from 'lucide-react';
import { uid, syncUid, parseEIJson, parseLabelsFile, buildEIJson, downloadJSON, simpleHash, formatMs } from './utils/parse';
import { SENSOR_COLORS, CATEGORY_COLORS } from './utils/colors';
import { pickBestChannels } from './utils/algorithms';
import DualRange from './components/DualRange';
import SplitModal from './components/SplitModal';
import LabelsModal from './components/LabelsModal';
import WaveformViewer from './components/WaveformViewer';
import SampleCard from './components/SampleCard';
import SplitHighlightsViewer from './components/SplitHighlightsViewer';
import LoadingOverlay from './components/LoadingOverlay';

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
  const [splitHighlightsByBase, setSplitHighlightsByBase] = useState({});
  const [splitHighlightSensors, setSplitHighlightSensors] = useState([]);
  const [visibleSampleTypes, setVisibleSampleTypes] = useState(new Set(['unmodified', 'segment']));

  const fileRef = useRef();
  const zipRef = useRef();
  const projectRef = useRef();
  const labelsManifestRef = useRef([]);
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

  const filteredSamples = useMemo(() => samples.filter(s => {
    if (!visibleSampleTypes.has(getSampleType(s))) return false;
    if (filterLabel !== 'all' && s.label !== filterLabel) return false;
    if (filterStatus === 'enabled' && !s.enabled) return false;
    if (filterStatus === 'disabled' && s.enabled) return false;
    if (filterCategory !== 'all' && s.category !== filterCategory) return false;
    const range = maxDur - minDur || 1;
    const pct = ((s.duration_ms - minDur) / range) * 100;
    if (pct < timeRange[0] || pct > timeRange[1]) return false;
    return true;
  }), [samples, visibleSampleTypes, getSampleType, filterLabel, filterStatus, filterCategory, timeRange, minDur, maxDur]);

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
      setLoading({ label: 'Importing samples', sub: `${newSamples.length} samples found`, count: newSamples.length, total: newSamples.length });
      setSamples(p => [...p, ...newSamples]);
      if (newSamples.length > 0) setActiveId(newSamples[0].id);
      showToast(`Loaded ${newSamples.length} samples${manifest.length ? ` · ${manifest.length} manifest entries` : ''}`, 'success');
    } catch (err) {
      showToast(`ZIP error: ${err.message}`, 'error');
    }
    setLoading(null);
  }, [showToast]);

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
    processFiles(Array.from(e.dataTransfer.files));
  }, [processFiles]);

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

    const zip = new JSZip();
    const usedPaths = new Set();
    const normalize = (name) => {
      const base = String(name || 'sample.json').replace(/[\\/:*?"<>|]+/g, '_').replace(/\s+/g, '_');
      return base.toLowerCase().endsWith('.json') ? base : `${base}.json`;
    };

    const manifestFiles = toExport.map((s, i) => {
      const category = s.category === 'testing' ? 'testing' : 'training';
      const baseName = normalize(s.filename || `${s.label || 'sample'}_${s.id || i + 1}.json`);
      let finalName = baseName;
      let k = 2;
      while (usedPaths.has(`${category}/${finalName}`)) {
        finalName = baseName.replace(/\.json$/i, `_${k}.json`);
        k++;
      }
      const relPath = `${category}/${finalName}`;
      usedPaths.add(relPath);

      zip.file(relPath, JSON.stringify(buildEIJson(s), null, 2));

      return {
        path: relPath,
        category,
        name: s.sampleName || finalName.replace(/\.json$/i, ''),
        label: s.label,
        enabled: s.enabled !== false,
        length: s.values.length,
      };
    });

    const labelsObj = {
      version: 1,
      files: manifestFiles.map(f => ({
        path: f.path,
        name: f.name,
        category: f.category,
        label: { type: 'label', label: f.label },
        enabled: f.enabled,
        length: f.length,
      })),
    };

    zip.file('info.labels', JSON.stringify(labelsObj, null, 2));

    const blob = await zip.generateAsync({ type: 'blob' });
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const url = URL.createObjectURL(blob);
    const a = Object.assign(document.createElement('a'), {
      href: url,
      download: `ei_export_${stamp}.zip`,
    });
    a.click();
    URL.revokeObjectURL(url);

    showToast(`Exported ZIP with ${toExport.length} sample${toExport.length !== 1 ? 's' : ''} + info.labels`, 'success');
  };

  const exportSplitSamplesZip = async () => {
    const splitSamples = samples.filter(s => s.splitBaseId && !s.fromLabels && s.values.length > 0);
    if (!splitSamples.length) {
      showToast('No split-generated samples available to export', 'error');
      return;
    }

    const zip = new JSZip();
    const used = new Set();
    const normalize = (name) => {
      const base = String(name || 'sample.json')
        .replace(/[\\/:*?"<>|]+/g, '_')
        .replace(/\s+/g, '_');
      return base.toLowerCase().endsWith('.json') ? base : `${base}.json`;
    };

    splitSamples.forEach((s, i) => {
      const baseName = normalize(s.filename || `${s.label || 'split'}_${i + 1}.json`);
      let finalName = baseName;
      let k = 2;
      while (used.has(finalName)) {
        finalName = baseName.replace(/\.json$/i, `_${k}.json`);
        k++;
      }
      used.add(finalName);
      zip.file(finalName, JSON.stringify(buildEIJson(s), null, 2));
    });

    const blob = await zip.generateAsync({ type: 'blob' });
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const url = URL.createObjectURL(blob);
    const a = Object.assign(document.createElement('a'), {
      href: url,
      download: `split_samples_${stamp}.zip`,
    });
    a.click();
    URL.revokeObjectURL(url);

    showToast(`Exported ${splitSamples.length} split sample${splitSamples.length !== 1 ? 's' : ''} to ZIP`, 'success');
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
      Object.assign(document.createElement('a'), { href: url, download: `ei_studio_project_${stamp}.eisproj.zip` }).click();
      URL.revokeObjectURL(url);
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
    background: disabled ? '#0d1625' : bg, color: disabled ? '#1e293b' : fg,
    border: 'none', borderRadius: 5, padding: '5px 11px',
    cursor: disabled ? 'not-allowed' : 'pointer', fontSize: 11,
    fontFamily: 'inherit', whiteSpace: 'nowrap', fontWeight: 600, opacity: disabled ? 0.5 : 1,
  });

  // Top bar icon button
  const TopBtn = ({ icon, label, onClick, disabled, color = '#94a3b8', bg = '#111827', border = '#1e293b', title }) => (
    <button onClick={onClick} disabled={disabled} title={title}
      style={{
        display: 'flex', alignItems: 'center', gap: 5,
        background: disabled ? '#0a0f1a' : bg,
        border: `1px solid ${disabled ? '#1a2030' : border}`,
        color: disabled ? '#1e293b' : color,
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

  // Sidebar action button
  const SideBtn = ({ icon, label, onClick, disabled, color = '#94a3b8', bg = '#111827', fullWidth }) => (
    <button onClick={onClick} disabled={disabled}
      style={{
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 4,
        ...(fullWidth ? { width: '100%' } : { flex: 1 }),
        background: disabled ? '#0a0f1a' : bg,
        border: `1px solid ${disabled ? '#1a2030' : '#1e293b'}`,
        color: disabled ? '#1e293b' : color,
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

  // Inline action icon+label button (used in waveform bar)
  const IcoBtn = ({ icon, label, onClick, color = '#94a3b8', bg = '#111827', border = '#1e293b' }) => (
    <button onClick={onClick}
      style={{
        display: 'flex', alignItems: 'center', gap: 5,
        background: bg, border: `1px solid ${border}`,
        color, borderRadius: 6, padding: '5px 10px',
        cursor: 'pointer', fontSize: 11, fontFamily: 'inherit', fontWeight: 600,
      }}
    >
      {icon}
      <span>{label}</span>
    </button>
  );

  return (
    <div style={{ fontFamily: "'JetBrains Mono', monospace", background: '#060d1a', minHeight: '100vh', color: '#f1f5f9' }}>

      {/* TOP BAR */}
      <div style={{
        background: '#020810',
        borderBottom: '1px solid #141f35',
        padding: '0 14px',
        display: 'flex', alignItems: 'center', gap: 8,
        height: 50, flexShrink: 0,
        boxShadow: '0 1px 0 #141f35',
      }}>
        {/* Logo */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginRight: 6 }}>
          <Zap size={18} color="#38bdf8" strokeWidth={2.5} />
          <span style={{ fontSize: 15, fontWeight: 900, letterSpacing: -0.5, color: '#f1f5f9' }}>
            EI<span style={{ color: '#38bdf8' }}>Studio</span>
          </span>
        </div>
        <div style={{ width: 1, height: 24, background: '#1e293b', marginRight: 2 }} />

        {/* File actions */}
        <TopBtn icon={<Save size={13} />} label="Save" onClick={saveProject} disabled={!samples.length}
          color="#a78bfa" bg="#1a103a" border="#4c1d95" title="Save project to .eisproj.zip" />
        <TopBtn icon={<FolderOpen size={13} />} label="Open" onClick={() => projectRef.current?.click()}
          color="#d1d5db" bg="#1a2030" border="#374151" title="Open saved project" />
        <div style={{ width: 1, height: 24, background: '#1e293b' }} />
        <TopBtn icon={<Package size={13} />} label="Import ZIP" onClick={() => zipRef.current?.click()}
          color="#34d399" bg="#0a2018" border="#065f46" title="Import EdgeImpulse dataset ZIP" />
        <TopBtn
          icon={<Download size={13} />} label="Export ZIP"
          onClick={exportSplitSamplesZip}
          disabled={!samples.some(s => s.splitBaseId && !s.fromLabels && s.values.length > 0)}
          color="#93c5fd" bg="#0d1e30" border="#1e3a5f"
          title="Export split samples to ZIP"
        />
        <TopBtn icon={<FileJson size={13} />} label="Files" onClick={() => fileRef.current?.click()}
          color="#60a5fa" bg="#0d1e30" border="#1e3a5f" title="Open individual .json/.labels files" />

        <div style={{ flex: 1 }} />

        {/* Stats */}
        <div style={{ display: 'flex', gap: 12, fontSize: 10, color: '#334155', fontFamily: 'monospace' }}>
          <span>{samples.length} <span style={{ color: '#1e3a5f' }}>samples</span></span>
          <span>{sensors.length} <span style={{ color: '#1e3a5f' }}>channels</span></span>
        </div>

        {/* Hidden file inputs */}
        <input ref={projectRef} type="file" accept=".eisproj,.eisproj.json,.json,.zip,.eisproj.zip"
          style={{ display: 'none' }} onChange={e => { if (e.target.files[0]) openProject(e.target.files[0]); e.target.value = ''; }} />
        <input ref={zipRef} type="file" accept=".zip" style={{ display: 'none' }}
          onChange={e => { if (e.target.files[0]) handleZip(e.target.files[0]); e.target.value = ''; }} />
        <input ref={fileRef} type="file" accept=".json,.labels" multiple style={{ display: 'none' }}
          onChange={e => { processFiles(Array.from(e.target.files)); e.target.value = ''; }} />
      </div>

      {/* LOADING OVERLAY */}
      <LoadingOverlay loading={loading} />

      {/* TOAST */}
      {toast && (
        <div style={{
          position: 'fixed', top: 60, right: 16, zIndex: 400,
          background: toast.type === 'success' ? '#041e10' : toast.type === 'error' ? '#1f0a0a' : '#0a1628',
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
        <div style={{ width: sidebar.width, borderRight: '1px solid #1e293b', display: 'flex', flexDirection: 'column', background: '#020810', flexShrink: 0, position: 'relative' }}>

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
              <div style={{ fontSize: 8, color: '#1e3a5f', marginTop: 1 }}>
                .zip · info.labels + testing/ + training/
              </div>
            </div>
          </div>

          {/* Filters */}
          <div style={{ padding: '0 8px 6px', borderBottom: '1px solid #1e293b', marginBottom: 5 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 5, fontSize: 9, color: '#1e3a5f', marginBottom: 5, letterSpacing: 1 }}>
              <Filter size={10} color="#1e3a5f" /> FILTERS
            </div>
            <select value={filterLabel} onChange={e => setFilterLabel(e.target.value)}
              style={{ width: '100%', background: '#080f1e', color: '#94a3b8', border: '1px solid #1e293b', borderRadius: 5, padding: '4px 6px', fontSize: 10, marginBottom: 4, fontFamily: 'inherit' }}>
              {allLabels.map(l => <option key={l} value={l}>{l}{l !== 'all' ? ` (${(groupedByLabel[l] || []).length})` : ` (${samples.length})`}</option>)}
            </select>
            <div style={{ display: 'flex', gap: 3, marginBottom: 4 }}>
              {['all', 'training', 'testing'].map(v => (
                <button key={v} onClick={() => setFilterCategory(v)} style={{ flex: 1, background: filterCategory === v ? (v === 'testing' ? '#451a0333' : v === 'training' ? '#0d204066' : '#1e293b') : '#080f1e', border: `1px solid ${filterCategory === v ? (v === 'testing' ? CATEGORY_COLORS.testing : v === 'training' ? CATEGORY_COLORS.training : '#64748b') : '#1e293b'}`, color: filterCategory === v ? (v === 'testing' ? CATEGORY_COLORS.testing : v === 'training' ? CATEGORY_COLORS.training : '#94a3b8') : '#334155', borderRadius: 4, padding: '3px 0', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>
                  {v === 'all' ? 'All' : v === 'training' ? 'Train' : 'Test'}
                </button>
              ))}
            </div>
            <div style={{ display: 'flex', gap: 3, marginBottom: 5 }}>
              {['all', 'enabled', 'disabled'].map(v => (
                <button key={v} onClick={() => setFilterStatus(v)} style={{ flex: 1, background: filterStatus === v ? '#0d2040' : '#080f1e', border: `1px solid ${filterStatus === v ? '#2563eb' : '#1e293b'}`, color: filterStatus === v ? '#60a5fa' : '#334155', borderRadius: 4, padding: '3px 0', fontSize: 9, cursor: 'pointer', fontFamily: 'inherit' }}>{v}</button>
              ))}
            </div>
            <div style={{ fontSize: 9, color: '#334155', marginBottom: 3 }}>View Samples</div>
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
                      background: on ? '#0d2040' : '#080f1e',
                      border: `1px solid ${on ? '#3b82f6' : '#1e293b'}`,
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
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 9, color: '#334155', marginBottom: 2 }}>
                  <span>Duration</span>
                  <span style={{ color: '#475569' }}>{formatMs(minDur + (maxDur - minDur) * timeRange[0] / 100)} – {formatMs(minDur + (maxDur - minDur) * timeRange[1] / 100)}</span>
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
                      background: '#080f1e',
                      color: '#94a3b8',
                      border: '1px solid #1e293b',
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
                      background: '#080f1e',
                      color: '#94a3b8',
                      border: '1px solid #1e293b',
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
            {selectedIds.size >= 2 && (
              <div style={{ marginBottom: 4 }}>
                <SideBtn
                  icon={<Scissors size={11} />}
                  label={`Batch Split (${selectedIds.size})`}
                  onClick={openBatchSplit}
                  color="#fbbf24" bg="#1a1008" fullWidth
                />
              </div>
            )}
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <span style={{ fontSize: 9, color: '#1e3a5f', fontFamily: 'monospace' }}>
                {filteredSamples.length}/{samples.length} · {selectedIds.size} sel
              </span>
              <div style={{ display: 'flex', gap: 2 }}>
                <button onClick={() => setSidebarViewMode('list')} title="List view"
                  style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 24, height: 24, background: sidebarViewMode === 'list' ? '#1e293b' : 'transparent', border: `1px solid ${sidebarViewMode === 'list' ? '#3b82f6' : '#1e293b'}`, color: sidebarViewMode === 'list' ? '#60a5fa' : '#334155', borderRadius: 4, cursor: 'pointer' }}>
                  <List size={12} />
                </button>
                <button onClick={() => setSidebarViewMode('grid')} title="Grid view"
                  style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', width: 24, height: 24, background: sidebarViewMode === 'grid' ? '#1e293b' : 'transparent', border: `1px solid ${sidebarViewMode === 'grid' ? '#3b82f6' : '#1e293b'}`, color: sidebarViewMode === 'grid' ? '#60a5fa' : '#334155', borderRadius: 4, cursor: 'pointer' }}>
                  <LayoutGrid size={12} />
                </button>
              </div>
            </div>
          </div>

          {/* Sample list / grid */}
          <div style={{ flex: 1, overflowY: 'auto', padding: sidebarViewMode === 'grid' ? '0 6px 10px' : '0 8px 10px' }}>
            {filteredSamples.length === 0 && (
              <div style={{ color: '#1e293b', fontSize: 10, textAlign: 'center', paddingTop: 24, lineHeight: 1.9 }}>
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
            <div style={{ position: 'absolute', top: '50%', right: 1, transform: 'translateY(-50%)', width: 3, height: 40, background: '#1e293b', borderRadius: 2, transition: 'background 0.15s' }}
              onMouseEnter={e => e.currentTarget.style.background = '#3b82f6'}
              onMouseLeave={e => e.currentTarget.style.background = '#1e293b'} />
          </div>
        </div>

        {/* ── MAIN PANEL ── */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden', minWidth: 0 }}>
          <div style={{ background: '#020810', borderBottom: '1px solid #1e293b', display: 'flex', padding: '0 16px', flexShrink: 0 }}>
            {[['waveform', '📈 Waveform'], ['groups', '🗂 Groups'], ['stats', '📊 Stats']].map(([t, l]) => (
              <button key={t} onClick={() => setTab(t)} style={{ background: 'none', border: 'none', borderBottom: `2px solid ${tab === t ? '#38bdf8' : 'transparent'}`, color: tab === t ? '#38bdf8' : '#334155', padding: '11px 16px', cursor: 'pointer', fontSize: 11, fontFamily: 'inherit', fontWeight: tab === t ? 700 : 400 }}>{l}</button>
            ))}
          </div>

          <div style={{ flex: 1, overflowY: 'auto', padding: 16 }}>

            {/* ── WAVEFORM ── */}
            {tab === 'waveform' && (
              <div>
                {activeSample && !activeSample.fromLabels && activeSample.values.length > 0 && (
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 12, padding: '8px 12px', background: '#080f1e', border: `1px solid ${activeSample.splitBaseId ? '#1e3a5f' : '#1e3a5f'}`, borderRadius: 8, flexWrap: 'wrap' }}>
                    <span style={{ fontSize: 11, color: '#38bdf8', fontWeight: 700, flex: 1, minWidth: 0 }}>
                      <span style={{ color: '#94a3b8', fontWeight: 400 }}>Active: </span>
                      <span style={{ color: '#f1f5f9' }}>{activeSample.label}</span>
                      {activeSample.sampleName && <span style={{ color: '#475569', fontWeight: 400 }}> · {activeSample.sampleName}</span>}
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
                    <div style={{ marginTop: 8, background: '#080f1e', border: '1px solid #1e293b', borderRadius: 8, padding: 10 }}>
                      <div style={{ fontSize: 10, color: '#64748b', marginBottom: 6 }}>
                        Split data from base sample <span style={{ color: '#f1f5f9' }}>{activeBaseSample.label}</span>
                      </div>
                      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))', gap: 6 }}>
                        {(activeSplitMeta.segments || []).map((seg, i) => (
                          <div
                            key={i}
                            onClick={() => seg.sampleId && setActiveId(seg.sampleId)}
                            style={{
                              background: seg.sampleId === activeSegmentSampleId ? '#10213a' : '#050c1a',
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
                            <div style={{ fontSize: 9, color: '#64748b', lineHeight: 1.5 }}>
                              <div>Label: <span style={{ color: '#94a3b8' }}>{seg.label || activeBaseSample.label}</span></div>
                              <div>Range: <span style={{ color: '#94a3b8' }}>{seg.start} → {seg.end}</span></div>
                              <div>Length: <span style={{ color: '#94a3b8' }}>{seg.length} pts ({formatMs(seg.length * activeBaseSample.interval_ms)})</span></div>
                              <div>File: <span style={{ color: '#475569' }}>{seg.filename}</span></div>
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
                      <div key={s.id} style={{ background: s.id === activeId ? '#0a1e38' : '#080f1e', border: `1px solid ${s.id === activeId ? '#38bdf8' : '#1e293b'}`, borderRadius: 8, padding: 12 }}>
                        <div style={{ fontSize: 12, fontWeight: 700, color: '#38bdf8', marginBottom: 3 }}>{s.label}</div>
                        {s.sampleName && <div style={{ fontSize: 9, color: '#334155', marginBottom: 3 }}>{s.sampleName}</div>}
                        <div style={{ fontSize: 10, color: '#334155', lineHeight: 1.7 }}>
                          <div>Points: <span style={{ color: '#64748b' }}>{s.values.length}</span></div>
                          <div>Duration: <span style={{ color: '#64748b' }}>{formatMs(s.duration_ms)}</span></div>
                          <div>Interval: <span style={{ color: '#64748b' }}>{s.interval_ms.toFixed(2)}ms</span></div>
                          <div>Category: <span style={{ color: s.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training }}>{s.category}</span></div>
                          <div>Status: <span style={{ color: s.enabled ? '#34d399' : '#f87171' }}>{s.enabled ? 'enabled' : 'disabled'}</span></div>
                        </div>
                      </div>
                    ))}
                  </div>
                )}
                {!viewSamples.length && <div style={{ color: '#1e293b', textAlign: 'center', padding: 60, fontSize: 13 }}>← Click a sample to view its waveform</div>}
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
                    <div key={label} style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 10, marginBottom: 10, overflow: 'hidden' }}>
                      <div style={{ padding: '10px 14px', display: 'flex', alignItems: 'center', gap: 8, borderBottom: '1px solid #1e293b', flexWrap: 'wrap' }}>
                        <span style={{ fontWeight: 700, fontSize: 13, color: '#f1f5f9', flex: 1 }}>{label}</span>
                        <span style={{ background: '#0d2040', color: '#60a5fa', fontSize: 9, borderRadius: 3, padding: '2px 6px' }}>{group.length} files</span>
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
                            style={{ background: s.id === activeId ? '#0a1e38' : '#050c1a', borderTop: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? '#1e293b' : '#450a0a'}`, borderRight: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? '#1e293b' : '#450a0a'}`, borderBottom: `1px solid ${s.id === activeId ? '#38bdf8' : s.enabled ? '#1e293b' : '#450a0a'}`, borderLeft: `2px solid ${s.category === 'testing' ? CATEGORY_COLORS.testing + '88' : CATEGORY_COLORS.training + '88'}`, borderRadius: 5, padding: '5px 9px', cursor: 'pointer', fontSize: 9 }}>
                            <div style={{ color: '#475569' }}>{s.values.length || '—'}pt</div>
                            <div style={{ color: '#334155' }}>{formatMs(s.duration_ms)}</div>
                            <div style={{ color: s.category === 'testing' ? CATEGORY_COLORS.testing : CATEGORY_COLORS.training, fontSize: 8 }}>{s.category === 'testing' ? 'TEST' : 'TRAIN'}</div>
                          </div>
                        ))}
                      </div>
                    </div>
                  );
                })}
                {!Object.keys(groupedByLabel).length && <div style={{ color: '#1e293b', textAlign: 'center', padding: 60 }}>Import a ZIP to see groups</div>}
              </div>
            )}

            {/* ── STATS ── */}
            {tab === 'stats' && (
              <div>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 8, marginBottom: 16 }}>
                  {[['Samples', samples.length, '#38bdf8'], ['Labels', Object.keys(groupedByLabel).length, '#34d399'], ['Total Points', samples.reduce((a, s) => a + s.values.length, 0).toLocaleString(), '#fbbf24'], ['Total Duration', formatMs(samples.reduce((a, s) => a + s.duration_ms, 0)), '#a78bfa']].map(([k, v, c]) => (
                    <div key={k} style={{ background: '#080f1e', border: `1px solid ${c}22`, borderRadius: 10, padding: 14, textAlign: 'center' }}>
                      <div style={{ fontSize: 20, fontWeight: 800, color: c }}>{v}</div>
                      <div style={{ fontSize: 9, color: '#334155', marginTop: 2, letterSpacing: 0.5 }}>{k}</div>
                    </div>
                  ))}
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, marginBottom: 16 }}>
                  {['training', 'testing'].map(cat => { const cs = samples.filter(s => s.category === cat); return (
                    <div key={cat} style={{ background: '#080f1e', border: `1px solid ${CATEGORY_COLORS[cat]}33`, borderRadius: 10, padding: 14 }}>
                      <div style={{ fontSize: 12, fontWeight: 700, color: CATEGORY_COLORS[cat], marginBottom: 8, textTransform: 'uppercase', letterSpacing: 1 }}>{cat}</div>
                      <div style={{ fontSize: 20, fontWeight: 800, color: '#f1f5f9', marginBottom: 4 }}>{cs.length}</div>
                      <div style={{ fontSize: 10, color: '#475569' }}>{cs.filter(s => s.enabled).length} enabled · {cs.filter(s => !s.enabled).length} disabled</div>
                    </div>
                  ); })}
                </div>
                <div style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 10, padding: 14, marginBottom: 12 }}>
                  <div style={{ fontSize: 11, fontWeight: 700, color: '#f1f5f9', marginBottom: 10 }}>Label Distribution</div>
                  {Object.entries(groupedByLabel).sort(([, a], [, b]) => b.length - a.length).map(([label, group]) => {
                    const pct = Math.round((group.length / samples.length) * 100) || 0;
                    const tr = group.filter(s => s.category === 'training').length;
                    const te = group.filter(s => s.category === 'testing').length;
                    return (
                      <div key={label} style={{ marginBottom: 8 }}>
                        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, marginBottom: 2, gap: 8 }}>
                          <span style={{ color: '#94a3b8', flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{label}</span>
                          <span style={{ color: CATEGORY_COLORS.training, flexShrink: 0 }}>{tr}tr</span>
                          <span style={{ color: CATEGORY_COLORS.testing, flexShrink: 0 }}>{te}te</span>
                          <span style={{ color: '#475569', flexShrink: 0 }}>({pct}%)</span>
                        </div>
                        <div style={{ height: 5, background: '#050c1a', borderRadius: 3, overflow: 'hidden', display: 'flex' }}>
                          <div style={{ width: `${(tr / samples.length) * 100}%`, height: '100%', background: CATEGORY_COLORS.training }} />
                          <div style={{ width: `${(te / samples.length) * 100}%`, height: '100%', background: CATEGORY_COLORS.testing }} />
                        </div>
                      </div>
                    );
                  })}
                </div>
                <div style={{ background: '#080f1e', border: '1px solid #1e293b', borderRadius: 10, padding: 14 }}>
                  <div style={{ fontSize: 11, fontWeight: 700, color: '#f1f5f9', marginBottom: 8 }}>Channels ({sensors.length})</div>
                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: 5 }}>
                    {sensors.map((s, i) => (
                      <span key={s} style={{ background: SENSOR_COLORS[i % SENSOR_COLORS.length] + '18', border: `1px solid ${SENSOR_COLORS[i % SENSOR_COLORS.length]}55`, color: SENSOR_COLORS[i % SENSOR_COLORS.length], borderRadius: 4, padding: '2px 8px', fontSize: 10, fontFamily: 'inherit' }}>{s}</span>
                    ))}
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
