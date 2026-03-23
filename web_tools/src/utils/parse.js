// ─── ID counter ──────────────────────────────────────────────────────────────
let _uid = 0;
export const uid = () => ++_uid;
// Call after loading persisted IDs to prevent collisions with existing numeric IDs.
// Pass the maximum numeric ID found among loaded samples so future uid() calls
// always produce values strictly higher than any existing ID.
export const syncUid = (maxId) => {
  const n = Math.max(_uid, Number(maxId) || 0);
  _uid = n;
};
export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

export function formatMs(ms) {
  if (!ms || isNaN(ms)) return '0ms';
  if (ms < 1000) return `${Number(ms).toFixed(0)}ms`;
  if (ms < 60000) return `${(ms / 1000).toFixed(2)}s`;
  return `${(ms / 60000).toFixed(2)}min`;
}

// ─── Simple hash for dedup ────────────────────────────────────────────────────
export function simpleHash(values) {
  if (!values || !values.length) return 0;
  let h = 0;
  const flat = Array.isArray(values[0])
    ? values.slice(0, 50).flatMap(v => v.slice(0, 4))
    : values.slice(0, 200);
  for (let i = 0; i < flat.length; i++) {
    h = (Math.imul(31, h) + ((flat[i] * 1000) | 0)) | 0;
  }
  return h;
}

// ─── Parse EdgeImpulse .json data file ───────────────────────────────────────
export function parseEIJson(text, filename) {
  try {
    const json = JSON.parse(text);
    const payload = json.payload || json;
    const sensors = (payload.sensors || []).map(s => s.name);
    const interval_ms = payload.interval_ms || 33.33;
    const values = payload.values || [];

    // Extract label from filename. EI uses several patterns:
    //
    // Pattern A (old ingestion style):
    //   "AJ_takbo_json_6k28e9u2_ingestion-55d75df7fb-9pq22.json"
    //    → label = "takbo"
    //
    // Pattern B (new path-style from .labels file):
    //   "takbo.json.6k24gu8r.ingestion-55d75df7fb-9pq22.s4.json"
    //    → label = "takbo"
    //
    // Pattern C (simple):
    //   "takbo.json"  →  label = "takbo"
    //
    let label = 'unknown';

    // Pattern A: username_LABEL_json_HASH_pod.json
    const mA = filename.match(/^[^_]+_(.+?)_json_/i);
    if (mA) {
      label = mA[1].replace(/_/g, ' ');
    } else {
      // Pattern B: LABEL.json.HASH.ingestion-...json  (or LABEL.json.HASH.pod.sN.json)
      const mB = filename.match(/^(.+?)\.json\./i);
      if (mB) {
        label = mB[1].replace(/_/g, ' ');
      } else {
        // Pattern C: LABEL.json
        const mC = filename.match(/^(.+?)\./);
        if (mC) label = mC[1].replace(/_/g, ' ');
      }
    }

    return {
      filename,
      label,
      sensors,
      interval_ms,
      values,
      raw: json,
      id: uid(),
      enabled: true,
      category: 'training',
      duration_ms: values.length * interval_ms,
      hash: simpleHash(values),
      fromLabels: false,
      sampleName: '',
    };
  } catch (e) {
    console.warn('Failed to parse EI JSON:', filename, e.message);
    return null;
  }
}

// ─── Rebuild EdgeImpulse JSON for export ─────────────────────────────────────
export function buildEIJson(sample) {
  const base = sample.raw || {};
  const basePayload = base.payload || {};
  const fallbackSensors = sample.sensors.map(n => ({ name: n, units: 'N/A' }));
  const sensors = Array.isArray(basePayload.sensors) && basePayload.sensors.length
    ? basePayload.sensors
    : fallbackSensors;

  return {
    ...base,
    payload: {
      ...basePayload,
      sensors,
      interval_ms: sample.interval_ms,
      values: sample.values,
    },
  };
}

export function downloadJSON(obj, filename) {
  const blob = new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = Object.assign(document.createElement('a'), { href: url, download: filename });
  document.body.appendChild(a); a.click(); document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ─── Parse EdgeImpulse .labels file ──────────────────────────────────────────
//
// Actual EI .labels format (confirmed from sample):
// {
//   "version": 1,
//   "files": [
//     {
//       "path": "testing/kumusta.json.6k24gu8r.ingestion-....json",
//       "name": "kumusta.samuel.s1",
//       "category": "testing",
//       "label": { "type": "label", "label": "kumusta" }   // <-- nested object!
//     },
//     ...
//   ]
// }
//
// Also supports older flat-string label, samples[], and text formats.
//
function extractLabel(labelField) {
  if (!labelField) return 'unknown';
  // Nested object: { type: "label", label: "foo" }
  if (typeof labelField === 'object' && labelField !== null) {
    return String(labelField.label || labelField.name || labelField.type || 'unknown');
  }
  // Plain string
  return String(labelField);
}

function extractFilename(f) {
  // Prefer path (has directory prefix like "testing/..."), else name, else filename
  const raw = f.path || f.filename || f.name || f.file || '';
  // Strip leading directory ("testing/" or "training/")
  return String(raw).replace(/^(testing|training)\//, '');
}

export function parseLabelsFile(text) {
  // ── Attempt JSON parse ───────────────────────────────────────────────────────
  try {
    const obj = JSON.parse(text);

    // Standard EI .labels: { version, files: [...] }
    const fileList = obj.files || obj.samples || obj.data || [];

    if (Array.isArray(fileList) && fileList.length > 0) {
      return fileList.map(f => {
        const label = extractLabel(f.label);
        const filename = extractFilename(f);
        const category =
          f.category === 'testing' ? 'testing' :
          f.category === 'training' ? 'training' :
          (f.split === 'test' || f.split === 'testing') ? 'testing' :
          'training';

        return {
          filename,
          // Also store full path for reference
          path: String(f.path || f.filename || ''),
          name: String(f.name || ''),
          label,
          category,
          enabled: f.enabled !== false,
          length: Number(f.length || f.sample_length || 0),
          id_remote: f.id || f.sample_id || null,
          created_at: f.created_at || null,
          frequency: f.frequency || null,
        };
      }).filter(f => f.filename || f.path);
    }

    // Fallback: flat object keyed by filename { "file.json": { label, category } }
    if (typeof obj === 'object' && !Array.isArray(obj)) {
      const entries = [];
      for (const [key, info] of Object.entries(obj)) {
        if (key === 'version') continue;
        if (typeof info === 'object' && info !== null) {
          entries.push({
            filename: String(key),
            path: String(key),
            name: String(key),
            label: extractLabel(info.label || info.name || 'unknown'),
            category: info.category === 'testing' ? 'testing' : 'training',
            enabled: info.enabled !== false,
            length: Number(info.length || 0),
          });
        }
      }
      if (entries.length) return entries;
    }
  } catch {
    // Not JSON — fall through to text formats
  }

  // ── Tab-separated: path\tlabel[\tcategory] ───────────────────────────────────
  const lines = text.trim().split('\n').filter(Boolean);
  const tsv = lines.map(line => {
    const parts = line.split('\t');
    if (parts.length >= 2) {
      const raw = parts[0].trim();
      return {
        filename: raw.replace(/^(testing|training)\//, ''),
        path: raw,
        name: raw,
        label: parts[1].trim(),
        category: parts[2]?.trim() === 'testing' ? 'testing' : 'training',
        enabled: true,
        length: 0,
      };
    }
    return null;
  }).filter(Boolean);
  if (tsv.length) return tsv;

  // ── CSV: path,label[,category] ───────────────────────────────────────────────
  const csv = lines.map(line => {
    const parts = line.split(',').map(s => s.trim().replace(/^"|"$/g, ''));
    if (parts.length >= 2) {
      const raw = parts[0];
      return {
        filename: raw.replace(/^(testing|training)\//, ''),
        path: raw,
        name: raw,
        label: parts[1],
        category: parts[2] === 'testing' ? 'testing' : 'training',
        enabled: true,
        length: 0,
      };
    }
    return null;
  }).filter(Boolean);

  return csv;
}
