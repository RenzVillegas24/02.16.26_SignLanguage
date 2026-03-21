/**
 * Predicted Flat Detector
 *
 * Learns what "flat/rest" looks like from user-provided reference samples,
 * then scans a target sample to find flat regions and cuts around them —
 * so the output segments are the active (non-flat) portions only.
 *
 * Algorithm:
 * 1. From reference "flat" samples, compute per-channel stats (mean + std)
 *    in sliding windows. Build a profile of "what flat looks like".
 * 2. For the target sample, compute the same window stats.
 * 3. Score each window: how similar is it to the flat profile?
 *    Uses a normalised Mahalanobis-style distance: |window_std - flat_std_mean| / flat_std_std
 *    averaged across selected channels.
 * 4. Windows below a threshold = flat. Find contiguous flat regions.
 * 5. Place cut points at the edges of flat→active transitions.
 */

export const FLAT_DISCRIMINANT_CHANNELS = [
  // Ordered by discriminant power (from empirical analysis of rest vs active data)
  { key: 'gx',    label: 'Gyro X',   power: 'high'   },
  { key: 'gy',    label: 'Gyro Y',   power: 'high'   },
  { key: 'gz',    label: 'Gyro Z',   power: 'high'   },
  { key: 'ay',    label: 'Accel Y',  power: 'high'   },
  { key: 'ax',    label: 'Accel X',  power: 'high'   },
  { key: 'pitch', label: 'Pitch',    power: 'medium' },
  { key: 'roll',  label: 'Roll',     power: 'medium' },
  { key: 'az',    label: 'Accel Z',  power: 'medium' },
  { key: 'hall0', label: 'Hall 0',   power: 'low'    },
  { key: 'hall1', label: 'Hall 1',   power: 'low'    },
  { key: 'hall2', label: 'Hall 2',   power: 'low'    },
  { key: 'hall3', label: 'Hall 3',   power: 'low'    },
  { key: 'hall4', label: 'Hall 4',   power: 'low'    },
  { key: 'flex0', label: 'Flex 0',   power: 'low'    },
  { key: 'flex1', label: 'Flex 1',   power: 'low'    },
  { key: 'flex2', label: 'Flex 2',   power: 'low'    },
  { key: 'flex3', label: 'Flex 3',   power: 'low'    },
  { key: 'flex4', label: 'Flex 4',   power: 'low'    },
];

function colStd(col) {
  if (col.length < 2) return 0;
  const mean = col.reduce((a, b) => a + b, 0) / col.length;
  return Math.sqrt(col.reduce((a, b) => a + (b - mean) ** 2, 0) / col.length);
}

function colMean(col) {
  return col.reduce((a, b) => a + b, 0) / col.length;
}

function getCol(values, ci) {
  return values.map(v => (Array.isArray(v) ? v[ci] : Number(v)));
}

/**
 * Build a flat profile from multiple reference (flat/rest) samples.
 *
 * @param {Array[]} flatSamples - array of sample.values arrays
 * @param {string[]} sensors    - sensor name list
 * @param {string[]} useChannels - which channel names to use
 * @param {number}   windowSize
 * @returns {{ channelProfiles: Object }} - per-channel { stdMean, stdStd }
 */
export function buildFlatProfile(flatSamples, sensors, useChannels, windowSize = 20) {
  // Collect window-stds from ALL flat samples across ALL selected channels
  const perChannel = {}; // channel name → array of stds from flat windows

  useChannels.forEach(ch => {
    const ci = sensors.indexOf(ch);
    if (ci < 0) return;
    perChannel[ch] = [];
    flatSamples.forEach(vals => {
      for (let i = 0; i + windowSize <= vals.length; i += Math.max(1, Math.floor(windowSize / 2))) {
        const seg = vals.slice(i, i + windowSize);
        const col = getCol(seg, ci);
        perChannel[ch].push(colStd(col));
      }
    });
  });

  // Compute mean + std of those stds → defines what "flat std" looks like
  const channelProfiles = {};
  Object.entries(perChannel).forEach(([ch, stds]) => {
    if (!stds.length) return;
    const mean = colMean(stds);
    const std  = colStd(stds);
    channelProfiles[ch] = { stdMean: mean, stdStd: Math.max(std, mean * 0.05 + 0.01) };
  });

  return { channelProfiles, windowSize };
}

/**
 * Score each window in a target sample: 0 = definitely flat, 1+ = active.
 *
 * Scoring: for each channel, compute how many σ away the window's std is
 * from the flat profile's stdMean. Average across channels.
 * Windows with score < threshold → flat.
 */
export function scoreSample(values, sensors, profile, useChannels) {
  const { channelProfiles, windowSize } = profile;
  const N = values.length;
  const halfW = Math.max(1, Math.floor(windowSize / 2));

  const windowScores = [];
  for (let i = 0; i + windowSize <= N; i += halfW) {
    const seg = values.slice(i, i + windowSize);
    let totalScore = 0, count = 0;
    useChannels.forEach(ch => {
      const ci = sensors.indexOf(ch);
      if (ci < 0) return;
      const prof = channelProfiles[ch];
      if (!prof) return;
      const col = getCol(seg, ci);
      const winStd = colStd(col);
      // Normalised distance from flat profile
      const dist = Math.abs(winStd - prof.stdMean) / prof.stdStd;
      totalScore += dist;
      count++;
    });
    windowScores.push({
      start: i,
      end: Math.min(i + windowSize, N),
      center: i + Math.floor(windowSize / 2),
      score: count > 0 ? totalScore / count : 0,
    });
  }
  return windowScores;
}

/**
 * Find flat regions and generate cut points around active segments.
 *
 * @param {Object[]} windowScores  - from scoreSample
 * @param {number}   threshold     - score below = flat (default 1.5)
 * @param {number}   minFlatPts    - minimum contiguous flat points to be a real flat region
 * @param {number}   N             - total length of sample
 * @returns {number[]} cut points (sorted)
 */
export function flatRegionsToCuts(windowScores, threshold, minFlatPts, N) {
  if (!windowScores.length) return [];

  // Classify each window as flat/active
  const flat = windowScores.map(w => w.score < threshold);

  // Find contiguous flat runs
  const flatRegions = [];
  let inFlat = false, flatStart = 0;
  flat.forEach((isFlat, i) => {
    if (!inFlat && isFlat) { inFlat = true; flatStart = i; }
    else if (inFlat && !isFlat) {
      const startPt = windowScores[flatStart].start;
      const endPt   = windowScores[i - 1].end;
      if (endPt - startPt >= minFlatPts) flatRegions.push({ start: startPt, end: endPt });
      inFlat = false;
    }
  });
  if (inFlat) {
    const startPt = windowScores[flatStart].start;
    const endPt   = windowScores[windowScores.length - 1].end;
    if (endPt - startPt >= minFlatPts) flatRegions.push({ start: startPt, end: endPt });
  }

  // Convert flat regions to cut points at the edges
  const cuts = new Set();
  flatRegions.forEach(r => {
    if (r.start > 0) cuts.add(r.start);
    if (r.end < N)   cuts.add(r.end);
  });

  return Array.from(cuts).sort((a, b) => a - b);
}

/**
 * Full pipeline: given flat reference samples + target, return:
 * - cut points
 * - window scores (for visualization)
 * - flat regions
 */
export function runFlatDetector(targetValues, sensors, flatSamples, useChannels, cfg = {}) {
  const {
    windowSize   = 20,
    threshold    = 1.5,
    minFlatPts   = 15,
  } = cfg;

  if (!flatSamples || !flatSamples.length || !useChannels.length) return { cuts: [], scores: [], flatRegions: [] };

  const profile      = buildFlatProfile(flatSamples, sensors, useChannels, windowSize);
  const windowScores = scoreSample(targetValues, sensors, profile, useChannels);
  const cuts         = flatRegionsToCuts(windowScores, threshold, minFlatPts, targetValues.length);

  // Collect flat regions for visualization
  const flat = windowScores.map(w => w.score < threshold);
  const flatRegions = [];
  let inFlat = false, flatStart = 0;
  flat.forEach((isFlat, i) => {
    if (!inFlat && isFlat) { inFlat = true; flatStart = i; }
    else if (inFlat && !isFlat) {
      const s = windowScores[flatStart].start, e = windowScores[i - 1].end;
      if (e - s >= minFlatPts) flatRegions.push({ start: s, end: e });
      inFlat = false;
    }
  });
  if (inFlat) {
    const s = windowScores[flatStart].start;
    const e = windowScores[windowScores.length - 1].end;
    if (e - s >= minFlatPts) flatRegions.push({ start: s, end: e });
  }

  return { cuts, scores: windowScores, flatRegions, profile };
}
