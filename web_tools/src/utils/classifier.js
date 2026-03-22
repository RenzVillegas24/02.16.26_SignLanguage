/**
 * Lightweight KNN + feature extraction classifier for gesture prediction.
 * Works entirely in the browser — no ML library needed.
 *
 * Features extracted per sample (per-channel):
 *   mean, std, min, max, range, RMS, zero-cross-rate
 *
 * Prediction returns:
 *   { label, confidence, topK: [{label, score}] }
 */

function getCol(values, ci) {
  return values.map(v => Array.isArray(v) ? v[ci] : Number(v));
}

function extractFeatures(values, sensors) {
  const features = [];
  for (let ci = 0; ci < sensors.length; ci++) {
    const col = getCol(values, ci);
    if (!col.length) { features.push(0, 0, 0, 0, 0, 0, 0); continue; }
    const n = col.length;
    const mean = col.reduce((a, b) => a + b, 0) / n;
    const variance = col.reduce((a, v) => a + (v - mean) ** 2, 0) / n;
    const std = Math.sqrt(variance);
    const mn = Math.min(...col);
    const mx = Math.max(...col);
    const rms = Math.sqrt(col.reduce((a, v) => a + v * v, 0) / n);
    let zc = 0;
    for (let i = 1; i < col.length; i++) {
      if ((col[i] >= mean) !== (col[i - 1] >= mean)) zc++;
    }
    features.push(mean, std, mn, mx, mx - mn, rms, zc / n);
  }
  return features;
}

function euclidean(a, b) {
  let sum = 0;
  for (let i = 0; i < a.length; i++) sum += (a[i] - b[i]) ** 2;
  return Math.sqrt(sum);
}

// Normalise features to [0,1] range across the training set
function normalise(trainFeatures, testVec) {
  const n = trainFeatures[0]?.length || 0;
  const mins = new Array(n).fill(Infinity);
  const maxs = new Array(n).fill(-Infinity);
  trainFeatures.forEach(f => f.forEach((v, i) => {
    if (v < mins[i]) mins[i] = v;
    if (v > maxs[i]) maxs[i] = v;
  }));
  const norm = f => f.map((v, i) => {
    const span = maxs[i] - mins[i];
    return span < 1e-9 ? 0 : (v - mins[i]) / span;
  });
  return {
    normTrain: trainFeatures.map(norm),
    normTest: norm(testVec),
  };
}

/**
 * Build a classifier model from labelled samples.
 * @param {Array} samples — array of { label, values, sensors }
 * @returns model object
 */
export function buildModel(samples) {
  const valid = samples.filter(s => s.values?.length && s.sensors?.length && s.label);
  if (valid.length < 2) return null;
  const sensors = valid[0].sensors;
  const entries = valid.map(s => ({
    label: s.label,
    features: extractFeatures(s.values, sensors),
  }));
  return { entries, sensors, labels: [...new Set(entries.map(e => e.label))].sort() };
}

/**
 * Predict the label of a sample using KNN.
 * @param {number[][]} values
 * @param {object}     model — from buildModel
 * @param {number}     k
 * @returns { label, confidence, topK }
 */
export function predict(values, model, k = 5) {
  if (!model || !model.entries.length) return null;
  const testVec = extractFeatures(values, model.sensors);
  const { normTrain, normTest } = normalise(model.entries.map(e => e.features), testVec);

  const distances = model.entries.map((e, i) => ({
    label: e.label,
    dist: euclidean(normTrain[i], normTest),
  })).sort((a, b) => a.dist - b.dist);

  const knn = distances.slice(0, Math.min(k, distances.length));
  const votes = {};
  knn.forEach(d => { votes[d.label] = (votes[d.label] || 0) + 1; });
  const topK = Object.entries(votes)
    .map(([label, count]) => ({ label, score: count / knn.length }))
    .sort((a, b) => b.score - a.score);
  const best = topK[0];
  return { label: best.label, confidence: best.score, topK };
}

/**
 * Cross-validate: leave-one-out accuracy per label + confusion matrix.
 * For large datasets, uses stratified sampling (up to maxPerClass samples).
 */
export function crossValidate(model, maxPerClass = 30) {
  if (!model || model.entries.length < 4) return null;
  const { entries, labels } = model;
  const k = Math.min(5, Math.floor(entries.length / 2));

  // Stratified sample
  const byLabel = {};
  labels.forEach(l => { byLabel[l] = []; });
  entries.forEach((e, i) => { if (byLabel[e.label]) byLabel[e.label].push(i); });
  const testIdxs = [];
  labels.forEach(l => {
    const idxs = byLabel[l];
    // shuffle
    for (let i = idxs.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [idxs[i], idxs[j]] = [idxs[j], idxs[i]];
    }
    testIdxs.push(...idxs.slice(0, maxPerClass));
  });

  // Confusion matrix: confMatrix[actual][predicted] = count
  const confMatrix = {};
  labels.forEach(l => { confMatrix[l] = {}; labels.forEach(p => { confMatrix[l][p] = 0; }); });
  let correct = 0;

  testIdxs.forEach(testIdx => {
    const testEntry = entries[testIdx];
    // Train on all others
    const trainFeatures = entries.filter((_, i) => i !== testIdx).map(e => e.features);
    const trainLabels   = entries.filter((_, i) => i !== testIdx).map(e => e.label);
    const { normTrain, normTest } = normalise(trainFeatures, entries[testIdx].features);
    const distances = trainFeatures.map((_, i) => ({
      label: trainLabels[i],
      dist: euclidean(normTrain[i], normTest),
    })).sort((a, b) => a.dist - b.dist).slice(0, k);
    const votes = {};
    distances.forEach(d => { votes[d.label] = (votes[d.label] || 0) + 1; });
    const predicted = Object.entries(votes).sort((a, b) => b[1] - a[1])[0][0];
    confMatrix[testEntry.label][predicted]++;
    if (predicted === testEntry.label) correct++;
  });

  const accuracy = correct / testIdxs.length;
  const perLabel = {};
  labels.forEach(l => {
    const row = confMatrix[l];
    const total = Object.values(row).reduce((a, b) => a + b, 0);
    perLabel[l] = total ? row[l] / total : 0;
  });

  return { accuracy, confMatrix, perLabel, labels, tested: testIdxs.length };
}
