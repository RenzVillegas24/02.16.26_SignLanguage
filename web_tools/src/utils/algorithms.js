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
