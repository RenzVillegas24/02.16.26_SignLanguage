// ─── Column extraction ─────────────────────────────────────────────────────
export function getCol(values, colIdx) {
  return values.map(v => (Array.isArray(v) ? v[colIdx] : Number(v)));
}

export function colStats(col) {
  if (!col.length) return { mean: 0, std: 0, min: 0, max: 0 };
  const mean = col.reduce((a, b) => a + b, 0) / col.length;
  const std = Math.sqrt(col.reduce((a, b) => a + (b - mean) ** 2, 0) / col.length);
  return { mean, std, min: Math.min(...col), max: Math.max(...col) };
}

export function movAvg(arr, w) {
  return arr.map((_, i) => {
    let s = 0, c = 0;
    for (let j = Math.max(0, i - w); j <= Math.min(arr.length - 1, i + w); j++) {
      s += arr[j]; c++;
    }
    return s / c;
  });
}

// ─── Combine multiple channel signals into a composite ─────────────────────
// Takes a list of colIdx values and merges them into one signal
// by computing the RMS (root mean square) across channels at each timestep.
export function combineChannels(values, colIdxs) {
  if (colIdxs.length === 1) return getCol(values, colIdxs[0]);
  return values.map(v => {
    const row = Array.isArray(v) ? v : [v];
    const sum = colIdxs.reduce((s, ci) => s + (row[ci] ?? 0) ** 2, 0);
    return Math.sqrt(sum / colIdxs.length);
  });
}

// ─── Auto-pick best channels: highest std dev (most variation) ─────────────
export function pickBestChannels(values, sensors, n = 3) {
  return sensors
    .map((name, ci) => {
      const col = getCol(values, ci);
      const { std } = colStats(col);
      return { name, ci, std };
    })
    .sort((a, b) => b.std - a.std)
    .slice(0, n)
    .map(x => x.name);
}

// ─── Algorithms (single-channel) ──────────────────────────────────────────

export function algoEnergy(col, windowSize, threshPct) {
  const energy = col.map((_, i) => {
    let e = 0, c = 0;
    for (let j = Math.max(0, i - windowSize); j <= Math.min(col.length - 1, i + windowSize); j++) {
      e += col[j] ** 2; c++;
    }
    return e / c;
  });
  const { min, max } = colStats(energy);
  const thresh = min + (max - min) * threshPct;
  const valleys = [];
  for (let i = windowSize; i < energy.length - windowSize; i++) {
    if (energy[i] < thresh) {
      let ok = true;
      for (let j = i - 3; j <= i + 3; j++) {
        if (j >= 0 && j < energy.length && energy[j] < energy[i]) { ok = false; break; }
      }
      if (ok && (valleys.length === 0 || i - valleys[valleys.length - 1] > windowSize)) {
        valleys.push(i);
      }
    }
  }
  return valleys;
}

export function algoThreshold(col, stdMult, minGap) {
  const { mean, std } = colStats(col);
  const thresh = mean + std * stdMult;
  const cuts = [];
  let inHigh = false, start = 0;
  for (let i = 0; i < col.length; i++) {
    if (!inHigh && col[i] > thresh) { inHigh = true; start = i; }
    else if (inHigh && col[i] <= thresh) {
      const mid = Math.floor((start + i) / 2);
      if (cuts.length === 0 || mid - cuts[cuts.length - 1] > minGap) cuts.push(mid);
      inHigh = false;
    }
  }
  return cuts;
}

export function algoDerivative(col, sensitivity, minGap) {
  const sm = movAvg(col, 3);
  const deriv = sm.map((v, i) => (i === 0 ? 0 : Math.abs(v - sm[i - 1])));
  const maxD = Math.max(...deriv) || 1;
  const thresh = maxD * sensitivity;
  const cuts = [];
  for (let i = 5; i < deriv.length - 5; i++) {
    if (deriv[i] > thresh && (cuts.length === 0 || i - cuts[cuts.length - 1] > minGap)) {
      cuts.push(i);
    }
  }
  return cuts;
}

export function algoZeroCross(col, minGap) {
  const { mean } = colStats(col);
  const cuts = [];
  let above = col[0] > mean, lastCut = 0;
  for (let i = 1; i < col.length; i++) {
    const now = col[i] > mean;
    if (now !== above && i - lastCut > minGap) { cuts.push(i); lastCut = i; above = now; }
  }
  return cuts;
}

export function algoPeak2Peak(col, windowSize) {
  const segs = Math.floor(col.length / windowSize);
  const cuts = [];
  for (let s = 1; s < segs; s++) {
    const prev = col.slice((s - 1) * windowSize, s * windowSize);
    const curr = col.slice(s * windowSize, (s + 1) * windowSize);
    const ppPrev = Math.max(...prev) - Math.min(...prev);
    const ppCurr = Math.max(...curr) - Math.min(...curr);
    if (Math.abs(ppPrev - ppCurr) > (ppPrev + ppCurr) * 0.4) cuts.push(s * windowSize);
  }
  return cuts;
}

export function algoVariance(col, windowSize, sensitivity) {
  const variances = col.map((_, i) => {
    const seg = col.slice(Math.max(0, i - windowSize), i + windowSize + 1);
    const m = seg.reduce((a, b) => a + b, 0) / seg.length;
    return seg.reduce((a, b) => a + (b - m) ** 2, 0) / seg.length;
  });
  const { min, max } = colStats(variances);
  const thresh = min + (max - min) * sensitivity;
  const cuts = [];
  let wasBig = variances[0] > thresh;
  for (let i = 1; i < variances.length; i++) {
    const big = variances[i] > thresh;
    if (big !== wasBig && (cuts.length === 0 || i - cuts[cuts.length - 1] > windowSize)) {
      cuts.push(i); wasBig = big;
    }
  }
  return cuts;
}

// ─── Main entry: supports multiple reference channels (merged via RMS) ─────
export function runAutoDetect(values, sensors, cfg) {
  const {
    algorithm,
    refSensors = [],   // array of sensor names (multi-channel)
    sensor,            // legacy single sensor name (fallback)
    windowSize = 30, threshold = 0.3,
    stdMult = 1.5, sensitivity = 0.3,
    minGap = 50, numParts = 4,
  } = cfg;

  // Resolve channel indices (support both multi and legacy single)
  const sensorNames = refSensors.length > 0 ? refSensors : (sensor ? [sensor] : []);
  const colIdxs = sensorNames
    .map(n => sensors.indexOf(n))
    .filter(ci => ci >= 0);

  if (colIdxs.length === 0 || !values.length) return [];

  // Merge selected channels into one composite signal
  const col = combineChannels(values, colIdxs);

  let cuts = [];
  switch (algorithm) {
    case 'energy':        cuts = algoEnergy(col, windowSize, threshold); break;
    case 'threshold':     cuts = algoThreshold(col, stdMult, minGap); break;
    case 'derivative':    cuts = algoDerivative(col, sensitivity, minGap); break;
    case 'zero_crossing': cuts = algoZeroCross(col, minGap); break;
    case 'peak2peak':     cuts = algoPeak2Peak(col, windowSize); break;
    case 'variance':      cuts = algoVariance(col, windowSize, sensitivity); break;
    case 'equal': {
      const sz = Math.floor(values.length / numParts);
      for (let i = 1; i < numParts; i++) cuts.push(i * sz);
      break;
    }
    default: break;
  }
  return cuts.filter(c => c > 0 && c < values.length).sort((a, b) => a - b);
}

// ─── Batch split: auto-detect and split multiple samples at once ───────────
// Returns array of { originalId, parts: [values[]] }
export function batchAutoSplit(samples, sensors, cfg) {
  return samples.map(sample => {
    const cuts = runAutoDetect(sample.values, sensors, cfg);
    const all = [0, ...cuts, sample.values.length].sort((a, b) => a - b);
    const parts = all.slice(0, -1).map((c, i) => sample.values.slice(c, all[i + 1]));
    return { sample, parts, cuts };
  });
}

// ═══════════════════════════════════════════════════════════════════════════
// ─── Pattern-Aware Intelligent Split ─────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════
//
// Algorithm:
// 1. Build a "template" from reference samples of the same label:
//    - Normalize each reference to zero-mean unit-variance
//    - Align them to peak activity, then average → canonical template
// 2. Slide the template over the target sample using normalized cross-correlation
// 3. Pick peaks in the correlation curve → each peak = one gesture occurrence
// 4. Estimate segment boundaries as peak ± half template length
// 5. Merge overlapping segments, filter by min/max duration
// 6. Return cut points with confidence scores
//
// Jitter is applied to each cut at execution time (in doSplit) based on the
// Random Shift settings, so this function returns clean cuts.
// ─────────────────────────────────────────────────────────────────────────

/**
 * Normalize a 1-D array to zero mean, unit variance.
 */
function znorm(arr) {
  const n = arr.length;
  if (!n) return arr;
  const mean = arr.reduce((a, b) => a + b, 0) / n;
  const std  = Math.sqrt(arr.reduce((a, b) => a + (b - mean) ** 2, 0) / n) || 1;
  return arr.map(v => (v - mean) / std);
}

/**
 * Compute a composite "activity signal" from values+sensors:
 * RMS across all selected channel derivatives (rate-of-change emphasises gesture transitions).
 */
function activitySignal(values, sensors, channelIdxs) {
  if (!channelIdxs.length || !values.length) return new Array(values.length).fill(0);
  const cols = channelIdxs.map(ci => getCol(values, ci).map((v, i, a) => i > 0 ? Math.abs(v - a[i - 1]) : 0));
  return values.map((_, t) => {
    let sum = 0;
    for (const col of cols) sum += col[t] ** 2;
    return Math.sqrt(sum / cols.length);
  });
}

/**
 * Build a template signal from multiple reference samples.
 * Steps:
 *   a. Compute activity signal per sample
 *   b. Find the global activity peak in each sample
 *   c. Trim each to [peak - halfLen, peak + halfLen]
 *   d. Average the trimmed, z-normed signals → template
 *
 * @param {number[][][]} refValues  - array of values arrays
 * @param {string[]}     sensors
 * @param {number[]}     chIdxs
 * @param {number}       templateLen  - desired template length (pts)
 * @returns {number[]} template signal of length templateLen
 */
function buildTemplate(refValues, sensors, chIdxs, templateLen) {
  const half = Math.floor(templateLen / 2);
  const aligned = [];

  for (const vals of refValues) {
    if (vals.length < templateLen) continue;
    const act = activitySignal(vals, sensors, chIdxs);
    // Find peak
    let peakIdx = 0, peakVal = -Infinity;
    for (let i = half; i < act.length - half; i++) {
      if (act[i] > peakVal) { peakVal = act[i]; peakIdx = i; }
    }
    const start = Math.max(0, peakIdx - half);
    const end   = Math.min(vals.length, start + templateLen);
    const slice = act.slice(start, end);
    if (slice.length === templateLen) aligned.push(znorm(slice));
  }

  if (!aligned.length) return new Array(templateLen).fill(0);

  // Average
  const tmpl = new Array(templateLen).fill(0);
  for (const a of aligned) for (let i = 0; i < templateLen; i++) tmpl[i] += a[i];
  return tmpl.map(v => v / aligned.length);
}

/**
 * Normalized cross-correlation between template T and signal S at every lag.
 * Returns correlation array (same length as S, padded with zeros at edges).
 */
function ncc(signal, template) {
  const N = signal.length;
  const M = template.length;
  const tMean = template.reduce((a, b) => a + b, 0) / M;
  const tStd  = Math.sqrt(template.reduce((a, b) => a + (b - tMean) ** 2, 0) / M) || 1;
  const tNorm = template.map(v => v - tMean);
  const corr  = new Array(N).fill(0);

  for (let lag = 0; lag <= N - M; lag++) {
    const window = signal.slice(lag, lag + M);
    const wMean  = window.reduce((a, b) => a + b, 0) / M;
    const wStd   = Math.sqrt(window.reduce((a, b) => a + (b - wMean) ** 2, 0) / M) || 1;
    let dot = 0;
    for (let i = 0; i < M; i++) dot += tNorm[i] * (window[i] - wMean);
    corr[lag + Math.floor(M / 2)] = dot / (M * tStd * wStd);
  }
  return corr;
}

/**
 * Simple non-maximum suppression peak picker.
 * Returns indices of local maxima above threshold with minimum spacing.
 */
function pickPeaks(signal, threshold, minSpacing) {
  const peaks = [];
  for (let i = 1; i < signal.length - 1; i++) {
    if (signal[i] < threshold) continue;
    if (signal[i] <= signal[i - 1] || signal[i] <= signal[i + 1]) continue;
    if (peaks.length && i - peaks[peaks.length - 1].idx < minSpacing) {
      // Keep higher peak
      if (signal[i] > peaks[peaks.length - 1].score) {
        peaks[peaks.length - 1] = { idx: i, score: signal[i] };
      }
    } else {
      peaks.push({ idx: i, score: signal[i] });
    }
  }
  return peaks;
}

/**
 * Main pattern-aware split function.
 *
 * @param {number[][]}   targetValues   - the sample to split
 * @param {string[]}     sensors
 * @param {number[][][]} refValues      - reference samples (same label, ideally clean individual gestures)
 * @param {object}       cfg
 *   templateLen   : length of template in pts (default: median of ref lengths)
 *   threshold     : NCC threshold to count as a match (0–1, default 0.35)
 *   minGapFrac    : min gap between segments as fraction of templateLen (default 0.3)
 *   chIdxs        : channel indices to use (default: all)
 *   maxSegments   : max number of segments to return
 * @returns { cuts, peaks, corr, templateLen, confidence }
 */
export function patternSplit(targetValues, sensors, refValues, cfg = {}) {
  const N = targetValues.length;
  if (!N || !refValues.length) return { cuts: [], peaks: [], corr: [], templateLen: 0, confidence: 0 };

  // Determine template length from median reference length
  const refLens = refValues.map(r => r.length).sort((a, b) => a - b);
  const medianLen = refLens[Math.floor(refLens.length / 2)];
  const templateLen = cfg.templateLen || Math.max(10, Math.min(medianLen, Math.floor(N / 2)));

  // Choose channels
  const allChIdxs = sensors.map((_, i) => i);
  const chIdxs = cfg.chIdxs?.length ? cfg.chIdxs : pickBestChannels(targetValues, sensors, 4).map(n => sensors.indexOf(n));

  // Build template
  const template = buildTemplate(refValues, sensors, chIdxs, templateLen);

  // Compute target activity signal
  const targetAct = znorm(activitySignal(targetValues, sensors, chIdxs));

  // NCC
  const corr = ncc(targetAct, template);

  // Smooth the correlation
  const smoothCorr = movAvg(corr, Math.floor(templateLen * 0.1));

  // Dynamic threshold: if cfg.threshold is given, use it; else use mean + 0.5*std
  const corrStats = colStats(smoothCorr);
  const autoThresh = corrStats.mean + (corrStats.std * 0.7);
  const threshold  = cfg.threshold ?? Math.max(0.15, Math.min(0.6, autoThresh));

  // Min spacing between peaks = fraction of template length
  const minSpacing = Math.round(templateLen * (cfg.minGapFrac ?? 0.5));

  // Pick peaks
  const peaks = pickPeaks(smoothCorr, threshold, minSpacing);

  // Convert peaks to cut points (between consecutive peaks)
  // Cut point = midpoint between consecutive peak centers
  const cuts = [];
  const half = Math.floor(templateLen / 2);
  
  if (peaks.length < 2) {
    // Only one pattern found — try to split into 2 around the peak
    if (peaks.length === 1) {
      const p = peaks[0];
      const segStart = Math.max(0, p.idx - half);
      const segEnd   = Math.min(N, p.idx + half);
      if (segStart > 10) cuts.push(segStart);
      if (segEnd < N - 10) cuts.push(segEnd);
    }
  } else {
    // Between each consecutive pair of peaks, place a cut at the correlation valley
    for (let i = 0; i < peaks.length - 1; i++) {
      const a = peaks[i].idx;
      const b = peaks[i + 1].idx;
      // Find the valley (minimum correlation) between these two peaks
      let valleyIdx = a + 1, valleyVal = smoothCorr[a + 1] ?? Infinity;
      for (let j = a + 1; j < b; j++) {
        if ((smoothCorr[j] ?? 0) < valleyVal) {
          valleyVal = smoothCorr[j];
          valleyIdx = j;
        }
      }
      cuts.push(Math.max(1, Math.min(N - 1, valleyIdx)));
    }

    // Leading cut before first peak if there's meaningful space
    const firstSeg = peaks[0].idx - half;
    if (firstSeg > Math.floor(templateLen * 0.3)) cuts.unshift(Math.max(1, firstSeg));

    // Trailing cut after last peak
    const lastSeg = peaks[peaks.length - 1].idx + half;
    if (lastSeg < N - Math.floor(templateLen * 0.3)) cuts.push(Math.min(N - 1, lastSeg));
  }

  const uniqueCuts = [...new Set(cuts)].sort((a, b) => a - b).filter(c => c > 0 && c < N);
  const confidence = peaks.length ? peaks.reduce((a, p) => a + p.score, 0) / peaks.length : 0;

  return {
    cuts: uniqueCuts,
    peaks,
    corr: smoothCorr,
    templateLen,
    confidence: Math.min(1, Math.max(0, confidence)),
    threshold,
  };
}

/**
 * Batch pattern split — applies patternSplit to multiple targets.
 * Uses ALL reference samples (excluding the target itself).
 */
export function batchPatternSplit(targets, sensors, allSamples, cfg = {}) {
  return targets.map(target => {
    const refs = allSamples
      .filter(s => s.label === target.label && s.id !== target.id && s.values?.length > 0)
      .map(s => s.values);
    const result = patternSplit(target.values, sensors, refs, cfg);
    return { sample: target, ...result };
  });
}
