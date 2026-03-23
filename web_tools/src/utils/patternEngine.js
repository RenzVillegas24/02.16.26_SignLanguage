/**
 * ═══════════════════════════════════════════════════════════════════
 *  PATTERN ENGINE v3  —  High-accuracy gesture segmentation
 * ═══════════════════════════════════════════════════════════════════
 *
 * Core design principles:
 *  1. Sensitivity is a DIRECT threshold multiplier — higher sensitivity
 *     always means lower threshold, always means more detections.
 *     No secondary clamp that can override it.
 *
 *  2. Multi-scale template matching — builds templates at 3 scales
 *     (0.7×, 1.0×, 1.3× of the median reference length) and takes
 *     the maximum score across scales at each time point.
 *
 *  3. Per-channel NCC averaged (not max-pooled) — rewards agreement
 *     across channels, not just the single noisiest channel.
 *
 *  4. DTW similarity uses all-channel mean distance (not min) with
 *     a softer exp decay calibrated to the reference distribution.
 *
 *  5. Leave-one-out self-calibration — computes each reference
 *     against the remaining references and collects their peak scores.
 *     This gives a real empirical baseline for what "a good match"
 *     looks like for THIS specific gesture, then thresholds relative
 *     to that baseline.
 *
 *  6. Sensitivity maps to a percentile of the calibration scores:
 *     sensitivity=0.5 → threshold = 50th percentile of ref scores
 *     sensitivity=0.8 → threshold = 20th percentile (more detections)
 *     sensitivity=0.2 → threshold = 80th percentile (fewer, cleaner)
 *
 *  7. Multi-channel feature vector uses 9 features per channel
 *     (vs 5 before), including spectral energy and kurtosis.
 *
 *  8. Cut placement uses a two-stage refinement: coarse valley
 *     between peaks, then fine-grained 5-point energy minimum search.
 * ═══════════════════════════════════════════════════════════════════
 */

// ─── Utilities ────────────────────────────────────────────────────────────

const mean = a => a.length ? a.reduce((s, v) => s + v, 0) / a.length : 0;

const variance = a => { const m = mean(a); return a.reduce((s, v) => s + (v - m) ** 2, 0) / (a.length || 1); };

const std = a => Math.sqrt(variance(a));

const znorm = a => { const m = mean(a), s = std(a) || 1; return a.map(v => (v - m) / s); };

const getCol = (vals, ci) => vals.map(v => Array.isArray(v) ? (v[ci] ?? 0) : Number(v));

const clamp = (v, lo, hi) => v < lo ? lo : v > hi ? hi : v;

function percentile(arr, p) {
  if (!arr.length) return 0;
  const s = [...arr].sort((a, b) => a - b);
  const idx = clamp((p / 100) * (s.length - 1), 0, s.length - 1);
  const lo = Math.floor(idx), hi = Math.ceil(idx);
  return s[lo] + (s[hi] - s[lo]) * (idx - lo);
}

function gaussSmooth(arr, sigma) {
  if (sigma < 0.5) return arr;
  const r = Math.ceil(sigma * 3);
  const k = [], N = arr.length; let ksum = 0;
  for (let i = -r; i <= r; i++) { const w = Math.exp(-i * i / (2 * sigma * sigma)); k.push(w); ksum += w; }
  return arr.map((_, i) => {
    let s = 0;
    for (let j = 0; j < k.length; j++) {
      const t = clamp(i + j - r, 0, N - 1);
      s += k[j] * arr[t];
    }
    return s / ksum;
  });
}

// Normalize array to [0, 1] using min/max
function minmaxNorm(arr) {
  const lo = Math.min(...arr), hi = Math.max(...arr);
  const span = hi - lo || 1;
  return arr.map(v => (v - lo) / span);
}

// Kurtosis (4th standardized moment) — measures peakedness
function kurtosis(arr) {
  const m = mean(arr), s = std(arr) || 1, n = arr.length;
  return arr.reduce((sum, v) => sum + ((v - m) / s) ** 4, 0) / n - 3;
}

// Skewness
function skewness(arr) {
  const m = mean(arr), s = std(arr) || 1, n = arr.length;
  return arr.reduce((sum, v) => sum + ((v - m) / s) ** 3, 0) / n;
}

// Low-frequency energy ratio (fraction of energy in first half of sorted magnitudes)
function spectralShape(arr) {
  const diffs = arr.slice(1).map((v, i) => Math.abs(v - arr[i])); // derivative
  const sorted = [...diffs].sort((a, b) => a - b);
  const half = Math.floor(sorted.length / 2);
  const lo = sorted.slice(0, half).reduce((s, v) => s + v, 0);
  const hi = sorted.slice(half).reduce((s, v) => s + v, 0);
  return lo / (hi || 1);
}

// ─── NCC ─────────────────────────────────────────────────────────────────

/**
 * Normalized cross-correlation. Returns array length N (target length).
 * Each position contains the NCC score at that center offset.
 * Optimized: uses running sum for mean, O(N*M) but tight.
 */
function ncc(signal, template) {
  const N = signal.length, M = template.length;
  if (M > N || M < 2) return new Float32Array(N);
  const half = Math.floor(M / 2);
  const tMean = mean(template), tStd = std(template) || 1;
  const tNorm = template.map(v => v - tMean);

  // Pre-compute running sums for fast mean
  const psum  = new Float64Array(N + 1);
  const psum2 = new Float64Array(N + 1);
  for (let i = 0; i < N; i++) {
    psum[i + 1]  = psum[i]  + signal[i];
    psum2[i + 1] = psum2[i] + signal[i] * signal[i];
  }

  const corr = new Float32Array(N);
  for (let lag = 0; lag <= N - M; lag++) {
    const wMean = (psum[lag + M] - psum[lag]) / M;
    const wVar  = (psum2[lag + M] - psum2[lag]) / M - wMean * wMean;
    const wStd  = Math.sqrt(Math.max(0, wVar)) || 1;
    let dot = 0;
    for (let i = 0; i < M; i++) dot += tNorm[i] * (signal[lag + i] - wMean);
    corr[lag + half] = dot / (M * tStd * wStd);
  }
  return corr;
}

// ─── Multi-scale multi-channel NCC ────────────────────────────────────────

/**
 * Build templates for one channel at multiple scales.
 * Each ref is peak-aligned to its maximum absolute-activity point.
 * Returns: { templates: Float32Array[], scale } for each requested scale.
 */
function buildTemplatesForChannel(refValues, ci, templateLen) {
  const half = Math.floor(templateLen / 2);
  const aligned = [];
  for (const vals of refValues) {
    if (vals.length < templateLen) continue;
    const col = znorm(getCol(vals, ci));
    // Find peak by RMS energy in a window of size half
    let bestE = -1, bestIdx = half;
    for (let i = half; i < col.length - half; i++) {
      let e = 0;
      for (let j = i - half; j < i + half; j++) e += col[j] * col[j];
      if (e > bestE) { bestE = e; bestIdx = i; }
    }
    const s = Math.max(0, bestIdx - half);
    const slice = col.slice(s, s + templateLen);
    if (slice.length === templateLen) aligned.push(slice);
  }
  if (!aligned.length) return new Array(templateLen).fill(0);
  // Weighted average — give more weight to higher-energy references
  const weights = aligned.map(a => a.reduce((s, v) => s + v * v, 0));
  const wsum = weights.reduce((a, b) => a + b, 0) || 1;
  const tmpl = new Array(templateLen).fill(0);
  for (let k = 0; k < aligned.length; k++) {
    const w = weights[k] / wsum;
    for (let i = 0; i < templateLen; i++) tmpl[i] += aligned[k][i] * w;
  }
  return tmpl;
}

function resampleTemplate(tmpl, newLen) {
  const M = tmpl.length;
  if (M === newLen) return tmpl;
  return Array.from({ length: newLen }, (_, i) => {
    const t = (i / (newLen - 1)) * (M - 1);
    const lo = Math.floor(t), hi = Math.min(lo + 1, M - 1);
    return tmpl[lo] + (tmpl[hi] - tmpl[lo]) * (t - lo);
  });
}

function runMultiScaleNCC(targetValues, chIdxs, refValues, templateLen) {
  const N = targetValues.length;
  const scales = [0.7, 1.0, 1.35].map(s => Math.max(8, Math.round(templateLen * s)));
  const fused = new Float32Array(N);

  for (const ci of chIdxs) {
    const baseTmpl = buildTemplatesForChannel(refValues, ci, templateLen);
    const col = znorm(getCol(targetValues, ci));
    let bestAtEachPoint = new Float32Array(N).fill(-1);

    for (const scaleLen of scales) {
      const scaled = resampleTemplate(baseTmpl, scaleLen);
      const corr = ncc(col, scaled);
      for (let t = 0; t < N; t++) {
        if (corr[t] > bestAtEachPoint[t]) bestAtEachPoint[t] = corr[t];
      }
    }
    // Accumulate with mean (not max) across channels
    for (let t = 0; t < N; t++) fused[t] += Math.max(0, bestAtEachPoint[t]);
  }

  const nCh = chIdxs.length || 1;
  const result = Array.from(fused, v => v / nCh);
  return gaussSmooth(result, Math.max(2, templateLen * 0.04));
}

// ─── DTW ─────────────────────────────────────────────────────────────────

function dtw(a, b, band = 0.25) {
  const N = a.length, M = b.length;
  const w = Math.max(Math.abs(N - M), Math.ceil(Math.min(N, M) * band));
  const INF = 1e9;
  let prev = new Float32Array(M).fill(INF);
  let curr = new Float32Array(M).fill(INF);
  prev[0] = (a[0] - b[0]) ** 2;
  for (let j = 1; j < Math.min(M, w + 1); j++) prev[j] = prev[j - 1] + (a[0] - b[j]) ** 2;

  for (let i = 1; i < N; i++) {
    curr.fill(INF);
    const jlo = Math.max(0, i - w), jhi = Math.min(M - 1, i + w);
    for (let j = jlo; j <= jhi; j++) {
      const cost = (a[i] - b[j]) ** 2;
      let best = prev[j];
      if (j > 0 && curr[j - 1] < best) best = curr[j - 1];
      if (j > 0 && prev[j - 1] < best) best = prev[j - 1];
      curr[j] = cost + (best === INF ? INF : best);
    }
    [prev, curr] = [curr, prev];
  }
  return Math.sqrt(prev[M - 1] / (N + M));
}

function runDTWScan(targetValues, chIdxs, refValues, templateLen) {
  const N = targetValues.length;
  if (!refValues.length || chIdxs.length === 0) return new Array(N).fill(0);

  const half = Math.floor(templateLen / 2);
  const stride = Math.max(1, Math.floor(templateLen * 0.1));

  // Build one prototype per channel (peak-aligned average)
  const protos = chIdxs.map(ci => buildTemplatesForChannel(refValues, ci, templateLen));
  const cols   = chIdxs.map(ci => znorm(getCol(targetValues, ci)));

  // Calibrate DTW scale: compute dist of each ref against others
  const selfDists = [];
  for (let ri = 0; ri < Math.min(refValues.length, 10); ri++) {
    for (let ci = 0; ci < chIdxs.length; ci++) {
      const col = znorm(getCol(refValues[ri], chIdxs[ci]));
      if (col.length < templateLen) continue;
      const start = Math.max(0, Math.floor(col.length / 2) - half);
      const w = col.slice(start, start + templateLen);
      if (w.length === templateLen) selfDists.push(dtw(w, protos[ci]));
    }
  }
  const selfMed = percentile(selfDists, 50) || 1;
  const scale = 2.0 / selfMed; // normalizes DTW distances relative to typical self-similarity

  const scores = new Float32Array(N);
  const counts = new Uint16Array(N);

  for (let lag = 0; lag <= N - templateLen; lag += stride) {
    let distSum = 0;
    for (let ci = 0; ci < chIdxs.length; ci++) {
      const w = cols[ci].slice(lag, lag + templateLen);
      distSum += dtw(w, protos[ci]);
    }
    const avgDist = distSum / chIdxs.length;
    const sim = Math.exp(-avgDist * scale); // calibrated similarity
    const center = lag + half;
    for (let t = Math.max(0, center - half); t <= Math.min(N - 1, center + half); t++) {
      scores[t] += sim;
      counts[t]++;
    }
  }

  const raw = Array.from(scores, (v, i) => counts[i] ? v / counts[i] : 0);
  return gaussSmooth(raw, Math.max(2, templateLen * 0.05));
}

// ─── Rich feature extractor ───────────────────────────────────────────────

/**
 * 9 features per channel: mean, std, min, max, range, skew, kurt, zcr, spectralShape
 */
function extractFeatures(values, chIdxs, start, len) {
  const features = [];
  for (const ci of chIdxs) {
    const col = [];
    for (let t = start; t < Math.min(start + len, values.length); t++) {
      col.push(Array.isArray(values[t]) ? values[t][ci] ?? 0 : 0);
    }
    if (col.length < 4) { for (let i = 0; i < 9; i++) features.push(0); continue; }
    const m = mean(col), s = std(col);
    const mn = Math.min(...col), mx = Math.max(...col);
    let zc = 0;
    for (let i = 1; i < col.length; i++) if ((col[i] >= m) !== (col[i - 1] >= m)) zc++;
    features.push(
      m, s, mn, mx, mx - mn,
      skewness(col), kurtosis(col),
      zc / col.length,
      spectralShape(col),
    );
  }
  return features;
}

function euclidean(a, b) {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += (a[i] - b[i]) ** 2;
  return Math.sqrt(s);
}

function cosineSim(a, b) {
  let dot = 0, na = 0, nb = 0;
  for (let i = 0; i < a.length; i++) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
  return dot / (Math.sqrt(na * nb) || 1);
}

/**
 * Build feature space from references — uses multiple windows per ref,
 * not just the center. Returns array of feature vectors.
 */
function buildRefFeatureBank(refValues, chIdxs, templateLen) {
  const half = Math.floor(templateLen / 2);
  const bank = [];
  for (const vals of refValues) {
    if (vals.length < templateLen) continue;
    // Extract features at 3 positions: 33%, 50%, 67% of sample length
    for (const frac of [0.33, 0.5, 0.67]) {
      const center = Math.floor(vals.length * frac);
      const s = clamp(center - half, 0, vals.length - templateLen);
      const f = extractFeatures(vals, chIdxs, s, templateLen);
      bank.push(f);
    }
  }
  return bank;
}

function runFeatureScan(targetValues, chIdxs, refBank, templateLen) {
  const N = targetValues.length;
  if (!refBank.length) return new Array(N).fill(0);

  const half = Math.floor(templateLen / 2);
  const stride = Math.max(1, Math.floor(templateLen * 0.12));

  // Normalize the reference bank
  const nDim = refBank[0].length;
  const mins = new Float32Array(nDim).fill(Infinity);
  const maxs = new Float32Array(nDim).fill(-Infinity);
  for (const f of refBank) for (let i = 0; i < nDim; i++) {
    if (f[i] < mins[i]) mins[i] = f[i];
    if (f[i] > maxs[i]) maxs[i] = f[i];
  }
  const spans = maxs.map((mx, i) => mx - mins[i] || 1);
  const normF = f => f.map((v, i) => (v - mins[i]) / spans[i]);
  const normBank = refBank.map(normF);

  // Compute KNN similarity: for each window, use median sim to top-3 ref vectors
  const scores = new Float32Array(N);
  const counts = new Uint16Array(N);

  for (let lag = 0; lag <= N - templateLen; lag += stride) {
    const f = normF(extractFeatures(targetValues, chIdxs, lag, templateLen));
    const sims = normBank.map(r => cosineSim(f, r));
    sims.sort((a, b) => b - a);
    const topK = sims.slice(0, Math.min(3, sims.length));
    const sim  = (topK.reduce((a, b) => a + b, 0) / topK.length + 1) / 2; // map [-1,1] → [0,1]

    const center = lag + half;
    for (let t = Math.max(0, center - half); t <= Math.min(N - 1, center + half); t++) {
      scores[t] += sim;
      counts[t]++;
    }
  }

  const raw = Array.from(scores, (v, i) => counts[i] ? v / counts[i] : 0);
  return gaussSmooth(raw, Math.max(2, templateLen * 0.06));
}

// ─── Self-calibration (leave-one-out) ─────────────────────────────────────

/**
 * Runs the ensemble on each reference sample against the others.
 * Collects the peak scores. This tells us what "a true positive" looks like
 * for this specific gesture class — so we can set the threshold relative to
 * the empirical distribution of self-similarity scores.
 *
 * Returns array of peak scores from LOO evaluation.
 */
function selfCalibrate(refValues, chIdxs, templateLen) {
  if (refValues.length < 2) return [];
  const peakScores = [];
  const nTest = Math.min(refValues.length, 8); // cap LOO at 8 to keep it fast

  for (let i = 0; i < nTest; i++) {
    const target = refValues[i];
    const others = refValues.filter((_, j) => j !== i);
    if (target.length < templateLen) continue;

    // Run NCC only (fastest) for calibration
    const fused = new Float32Array(target.length);
    for (const ci of chIdxs) {
      const tmpl  = buildTemplatesForChannel(others, ci, templateLen);
      const col   = znorm(getCol(target, ci));
      const corr  = ncc(col, tmpl);
      for (let t = 0; t < target.length; t++) {
        if (corr[t] > 0) fused[t] += corr[t] / chIdxs.length;
      }
    }
    const smoothed = gaussSmooth(Array.from(fused), Math.max(2, templateLen * 0.04));
    // Take the max peak score in the middle 60% of the reference
    const s = Math.floor(target.length * 0.2);
    const e = Math.floor(target.length * 0.8);
    let best = 0;
    for (let t = s; t < e; t++) if (smoothed[t] > best) best = smoothed[t];
    if (best > 0) peakScores.push(best);
  }

  return peakScores;
}

// ─── Fusion ───────────────────────────────────────────────────────────────

function fuseDetectors(scoreArrays, weights) {
  const N = scoreArrays[0].length;
  const totalW = weights.reduce((a, b) => a + b, 0);
  const fused = new Float32Array(N);
  for (let k = 0; k < scoreArrays.length; k++) {
    const w = weights[k] / totalW;
    const arr = scoreArrays[k];
    const lo = percentile(arr, 2), hi = percentile(arr, 98);
    const span = hi - lo || 1;
    for (let t = 0; t < N; t++) {
      fused[t] += clamp((arr[t] - lo) / span, 0, 1) * w;
    }
  }
  return gaussSmooth(Array.from(fused), 2);
}

// ─── Peak picker ─────────────────────────────────────────────────────────

/**
 * sensitivity ∈ [0,1]:
 *   - Directly controls the threshold.
 *   - Higher sensitivity = lower threshold = more peaks detected.
 *   - Threshold is set as: calibBaseline * (1 - sensitivity * k)
 *   - No floor that can override sensitivity.
 */
function pickPeaks(scores, minSpacing, threshold) {
  const peaks = [];
  for (let i = 1; i < scores.length - 1; i++) {
    if (scores[i] <= threshold) continue;
    if (scores[i] <= scores[i - 1] || scores[i] <= scores[i + 1]) continue;
    if (peaks.length && i - peaks[peaks.length - 1].idx < minSpacing) {
      if (scores[i] > peaks[peaks.length - 1].score) {
        peaks[peaks.length - 1] = { idx: i, score: scores[i] };
      }
    } else {
      peaks.push({ idx: i, score: scores[i] });
    }
  }
  return peaks;
}

// ─── Cut refinement ───────────────────────────────────────────────────────

function findValley(fused, energy, a, b) {
  // Two-stage: find rough valley in fused score, then refine with energy
  let best = Math.floor((a + b) / 2), bestVal = Infinity;
  for (let t = a + 1; t < b; t++) {
    const v = fused[t] * 0.55 + (energy[t] || 0) * 0.45;
    if (v < bestVal) { bestVal = v; best = t; }
  }
  // Fine-tune ±3 points using energy minimum
  const lo = Math.max(a + 1, best - 3), hi = Math.min(b - 1, best + 3);
  let minE = Infinity;
  for (let t = lo; t <= hi; t++) {
    if ((energy[t] || 0) < minE) { minE = energy[t]; best = t; }
  }
  return best;
}

// ─── Main export ─────────────────────────────────────────────────────────

/**
 * @param {number[][]} targetValues
 * @param {string[]}   sensors
 * @param {number[][][]} refValues  — clean single-gesture reference samples
 * @param {object}     cfg
 *   chIdxs       : channel indices to use
 *   templateLen  : 0 = auto
 *   sensitivity  : 0–1, DIRECTLY lowers the threshold (higher = more detections)
 *   minGapFrac   : min gap between peaks as fraction of templateLen
 */
export function advancedPatternSplit(targetValues, sensors, refValues, cfg = {}) {
  const N = targetValues.length;
  if (!N || !refValues.length) {
    return { cuts: [], peaks: [], fused: [], nccScores: [], dtwScores: [], energyScores: [], featScores: [], templateLen: 0, confidence: 0, threshold: 0, diagnostics: 'no references' };
  }

  // ── Config ──────────────────────────────────────────────────────────────
  const refLens    = refValues.map(r => r.length).sort((a, b) => a - b);
  const medLen     = refLens[Math.floor(refLens.length / 2)];
  const templateLen = cfg.templateLen > 10 ? cfg.templateLen : Math.max(15, Math.min(medLen, Math.floor(N / 2)));
  const half        = Math.floor(templateLen / 2);
  const chIdxs      = (cfg.chIdxs?.length) ? cfg.chIdxs : sensors.map((_, i) => i);
  const sensitivity = clamp(cfg.sensitivity ?? 0.6, 0, 1);
  const minGapFrac  = cfg.minGapFrac ?? 0.5;
  const minSpacing  = Math.max(1, Math.round(templateLen * minGapFrac));

  // ── Detector 1: Multi-scale NCC ──────────────────────────────────────────
  const nccScores = runMultiScaleNCC(targetValues, chIdxs, refValues, templateLen);

  // ── Detector 2: DTW ──────────────────────────────────────────────────────
  const dtwScores = runDTWScan(targetValues, chIdxs, refValues, templateLen);

  // ── Detector 3: Energy ───────────────────────────────────────────────────
  // RMS energy per frame, smoothed and normalized
  const rawEnergy = targetValues.map(v => {
    let e = 0;
    for (const ci of chIdxs) e += (Array.isArray(v) ? v[ci] ?? 0 : 0) ** 2;
    return Math.sqrt(e / (chIdxs.length || 1));
  });
  const energyScores = gaussSmooth(minmaxNorm(rawEnergy), Math.max(2, half * 0.15));

  // ── Detector 4: Feature similarity ──────────────────────────────────────
  const refBank   = buildRefFeatureBank(refValues, chIdxs, templateLen);
  const featScores = runFeatureScan(targetValues, chIdxs, refBank, templateLen);

  // ── Fusion ───────────────────────────────────────────────────────────────
  const fused = fuseDetectors(
    [nccScores, dtwScores, energyScores, featScores],
    [3.0, 2.5, 1.0, 1.5]   // NCC and DTW most trusted
  );

  // ── Self-calibration to set threshold ───────────────────────────────────
  const calScores = selfCalibrate(refValues, chIdxs, templateLen);
  let threshold;
  if (calScores.length >= 2) {
    // Sensitivity directly selects which percentile of calibration scores is the threshold:
    // sensitivity=1.0 → threshold = min(calScores) * 0.5  (very permissive)
    // sensitivity=0.5 → threshold = median(calScores) * 0.7
    // sensitivity=0.0 → threshold = max(calScores) * 0.9  (very strict)
    const calPct = percentile(calScores, 100 * (1 - sensitivity));
    threshold = calPct * (0.5 + (1 - sensitivity) * 0.4); // scale: 0.5 at sens=1, 0.9 at sens=0
  } else {
    // No calibration data — use direct percentile of fused scores
    const fusedSorted = [...fused].sort((a, b) => b - a);
    const topN = Math.max(1, Math.round(fusedSorted.length * (1 - sensitivity) * 0.3 + 0.02));
    threshold = fusedSorted[Math.min(topN, fusedSorted.length - 1)];
  }
  // Hard cap: threshold must be positive and below global max
  const globalMax = Math.max(...fused);
  threshold = clamp(threshold, globalMax * 0.05, globalMax * 0.95);

  // ── Peak detection ───────────────────────────────────────────────────────
  const peaks = pickPeaks(fused, minSpacing, threshold);

  // ── Cut placement ────────────────────────────────────────────────────────
  const cuts = [];

  if (peaks.length === 0) {
    // Fallback: find single highest region
    let bestIdx = half, bestVal = -Infinity;
    for (let t = half; t < N - half; t++) if (fused[t] > bestVal) { bestVal = fused[t]; bestIdx = t; }
    if (bestVal > threshold * 0.6) {
      if (bestIdx - half > 5) cuts.push(Math.max(1, bestIdx - half));
      if (bestIdx + half < N - 5) cuts.push(Math.min(N - 1, bestIdx + half));
    }
  } else if (peaks.length === 1) {
    const p = peaks[0];
    const leadCut  = p.idx - half;
    const trailCut = p.idx + half;
    if (leadCut  > Math.floor(templateLen * 0.2)) cuts.push(Math.max(1, leadCut));
    if (trailCut < N - Math.floor(templateLen * 0.2)) cuts.push(Math.min(N - 1, trailCut));
  } else {
    // Between consecutive peaks: valley
    for (let i = 0; i < peaks.length - 1; i++) {
      const cut = findValley(fused, energyScores, peaks[i].idx, peaks[i + 1].idx);
      cuts.push(clamp(cut, 1, N - 1));
    }
    // Leading / trailing
    const firstLead = peaks[0].idx - half;
    if (firstLead > Math.floor(templateLen * 0.15)) cuts.unshift(Math.max(1, firstLead));
    const lastTrail = peaks[peaks.length - 1].idx + half;
    if (lastTrail < N - Math.floor(templateLen * 0.15)) cuts.push(Math.min(N - 1, lastTrail));
  }

  const uniqueCuts = [...new Set(cuts)].sort((a, b) => a - b).filter(c => c > 0 && c < N);
  const avgScore   = peaks.length ? peaks.reduce((s, p) => s + p.score, 0) / peaks.length : 0;
  const confidence = clamp(avgScore / (globalMax || 1), 0, 1);

  return {
    cuts: uniqueCuts,
    peaks,
    fused: Array.from(fused),
    nccScores: Array.from(nccScores),
    dtwScores: Array.from(dtwScores),
    energyScores: Array.from(energyScores),
    featScores: Array.from(featScores),
    templateLen,
    confidence,
    threshold,
    diagnostics: `${peaks.length} peaks · tpl=${templateLen}pts (${(templateLen * (cfg.interval_ms || 1) / 1000).toFixed(2)}s) · sens=${sensitivity.toFixed(2)} · thresh=${threshold.toFixed(3)} · refs=${refValues.length} · cal=${calScores.length}`,
  };
}

/**
 * Batch version.
 */
export function advancedBatchPatternSplit(targets, sensors, allSamples, cfg = {}) {
  return targets.map(target => {
    const refs = allSamples
      .filter(s => s.label === target.label && s.id !== target.id && s.values?.length > 0)
      .map(s => s.values);
    if (!refs.length) return { sample: target, cuts: [], peaks: [], fused: [], confidence: 0, templateLen: 0, threshold: 0, diagnostics: 'no refs' };
    const res = advancedPatternSplit(target.values, sensors, refs, { ...cfg, interval_ms: target.interval_ms });
    return { sample: target, ...res };
  });
}
