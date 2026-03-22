/**
 * Shared mutable store — persists all split settings within the session.
 * Mutated in-place by SplitModal and FlatDetectorPanel on every change.
 */
export const lastAlgoStore = {
  // Which tab was last active
  mode: 'auto',

  // Auto-detect params
  algo: 'energy',
  windowSize: 30,
  windowIncreaseStride: 10,
  threshold: 0.25,
  stdMult: 1.5,
  sensitivity: 0.25,
  minGap: null,
  numParts: 4,

  // Equal parts
  equalParts: 4,

  // Random shift — single [lo, hi] range; each cut gets value sampled from this range
  shiftEnabled: false,
  shiftLo: -10,     // minimum shift (can be negative)
  shiftHi: 10,      // maximum shift
  shiftUnit: 'pts',

  // Padding — single [lo, hi] range
  // Fixed mode: padLo is used as constant pad
  // Random mode: pad per segment sampled from [padLo, padHi]
  padEnabled: false,
  padLo: 3,
  padHi: 10,
  padUnit: 'pts',
  padRandom: false,

  // Target duration — crops each kept segment to a random duration from [durLo, durHi]
  durEnabled: false,
  durLo: 50,
  durHi: 100,
  durUnit: 'pts',
  durAlign: 'center', // 'start' | 'center' | 'end' | 'random'

  // Predicted Flat
  flatWindowSize: 20,
  flatThreshold: 1.5,
  flatMinFlatPts: 15,
  flatSelectedIds: [],
  flatSelectedCh: null,
  flatAutoDisable: false,
};
