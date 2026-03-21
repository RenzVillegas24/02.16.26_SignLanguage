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
  threshold: 0.25,
  stdMult: 1.5,
  sensitivity: 0.25,
  minGap: null,      // null → use per-sample default
  numParts: 4,

  // Equal parts
  equalParts: 4,

  // Shift
  shiftEnabled: false,
  shiftMin: -10,
  shiftMax: 10,
  shiftUnit: 'pts',

  // Padding
  padEnabled: false,
  padMin: 5,
  padMax: 5,
  padUnit: 'pts',
  padRandom: false,

  // Predicted Flat
  flatWindowSize: 20,
  flatThreshold: 1.5,
  flatMinFlatPts: 15,
  flatSelectedIds: [],    // array of sample IDs (serializable)
  flatSelectedCh: null,   // null = use auto default
};
