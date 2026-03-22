import { useState, useMemo, useCallback, useRef } from 'react';
import { buildModel, predict, crossValidate } from '../utils/classifier';
import { SENSOR_COLORS } from '../utils/colors';
import { formatMs } from '../utils/parse';
import { Target, Play, RefreshCw, Upload } from 'lucide-react';

// ─── Confusion matrix heatmap ─────────────────────────────────────────────
function ConfusionMatrix({ result, theme }) {
  if (!result) return null;
  const { confMatrix, labels, accuracy, perLabel } = result;

  const cellMax = Math.max(1, ...labels.flatMap(a => labels.map(p => confMatrix[a]?.[p] || 0)));

  return (
    <div>
      <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10, display: 'flex', alignItems: 'center', gap: 8 }}>
        Confusion Matrix
        <span style={{ fontWeight: 400, fontSize: 10, color: theme.textMuted }}>
          — overall accuracy: <b style={{ color: accuracy >= 0.8 ? '#34d399' : accuracy >= 0.6 ? '#fbbf24' : '#f87171' }}>{(accuracy * 100).toFixed(1)}%</b>
          <span style={{ color: theme.textDim }}> ({result.tested} tested, LOO-CV)</span>
        </span>
      </div>
      <div style={{ overflowX: 'auto' }}>
        <table style={{ borderCollapse: 'collapse', fontSize: 9, fontFamily: 'monospace' }}>
          <thead>
            <tr>
              <th style={{ padding: '4px 8px', color: theme.textDim, fontWeight: 400, textAlign: 'right', fontSize: 9 }}>actual ↓ pred →</th>
              {labels.map(p => (
                <th key={p} style={{ padding: '4px 6px', color: theme.textMuted, fontWeight: 700, maxWidth: 80, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {p}
                </th>
              ))}
              <th style={{ padding: '4px 6px', color: theme.textMuted, fontWeight: 700 }}>Acc</th>
            </tr>
          </thead>
          <tbody>
            {labels.map(actual => (
              <tr key={actual}>
                <td style={{ padding: '3px 8px', color: theme.textSecondary, fontWeight: 700, whiteSpace: 'nowrap', textAlign: 'right', maxWidth: 100, overflow: 'hidden', textOverflow: 'ellipsis' }}>
                  {actual}
                </td>
                {labels.map(pred => {
                  const v = confMatrix[actual]?.[pred] || 0;
                  const isDiag = actual === pred;
                  const intensity = cellMax > 0 ? v / cellMax : 0;
                  const bg = isDiag
                    ? `rgba(52,211,153,${0.15 + intensity * 0.5})`
                    : v > 0 ? `rgba(248,113,113,${0.1 + intensity * 0.45})` : 'transparent';
                  return (
                    <td key={pred} title={`Actual: ${actual}, Predicted: ${pred}, Count: ${v}`}
                      style={{
                        padding: '3px 6px', textAlign: 'center', background: bg,
                        color: v > 0 ? (isDiag ? '#34d399' : '#f87171') : theme.textDim,
                        fontWeight: isDiag && v > 0 ? 700 : 400,
                        border: `1px solid ${theme.border}`,
                        minWidth: 36,
                      }}>
                      {v || '·'}
                    </td>
                  );
                })}
                <td style={{ padding: '3px 8px', textAlign: 'center', fontWeight: 700,
                  color: perLabel[actual] >= 0.8 ? '#34d399' : perLabel[actual] >= 0.6 ? '#fbbf24' : '#f87171' }}>
                  {perLabel[actual] !== undefined ? `${(perLabel[actual] * 100).toFixed(0)}%` : '—'}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div style={{ display: 'flex', gap: 12, marginTop: 8, fontSize: 9, color: theme.textDim }}>
        <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <span style={{ width: 10, height: 10, background: 'rgba(52,211,153,0.5)', display: 'inline-block', borderRadius: 2 }} /> Correct
        </span>
        <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <span style={{ width: 10, height: 10, background: 'rgba(248,113,113,0.4)', display: 'inline-block', borderRadius: 2 }} /> Misclassified
        </span>
        <span>Darker = more samples</span>
      </div>
    </div>
  );
}

// ─── Live prediction panel ────────────────────────────────────────────────
function LivePredict({ model, samples, activeSample, theme }) {
  const [result, setResult] = useState(null);
  const [targetId, setTargetId] = useState(activeSample?.id || '');

  const testSample = useMemo(
    () => samples.find(s => s.id === (Number(targetId) || targetId)) || activeSample,
    [samples, targetId, activeSample]
  );

  const run = () => {
    if (!testSample?.values?.length || !model) return;
    const r = predict(testSample.values, model, 5);
    setResult({ ...r, actualLabel: testSample.label });
  };

  const isCorrect = result && result.label === result.actualLabel;

  return (
    <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14, marginBottom: 14 }}>
      <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10 }}>Live Prediction</div>
      <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 10, flexWrap: 'wrap' }}>
        <select value={targetId} onChange={e => { setTargetId(e.target.value); setResult(null); }}
          style={{ flex: 1, background: theme.bgInput, color: theme.textPrimary, border: `1px solid ${theme.border}`, borderRadius: 5, padding: '5px 8px', fontSize: 10, fontFamily: 'inherit' }}>
          {samples.filter(s => s.values?.length > 0).map(s => (
            <option key={s.id} value={s.id}>{s.label}{s.sampleName ? ` · ${s.sampleName}` : ''} ({s.values.length}pt)</option>
          ))}
        </select>
        <button onClick={run} style={{ display: 'flex', alignItems: 'center', gap: 6, background: '#1d4ed8', border: 'none', color: '#fff', borderRadius: 6, padding: '6px 14px', cursor: 'pointer', fontSize: 11, fontWeight: 700, fontFamily: 'inherit' }}>
          <Play size={12} /> Predict
        </button>
      </div>
      {result && (
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 8, padding: '8px 12px', background: isCorrect ? '#052e16' : '#1f0a0a', border: `1px solid ${isCorrect ? '#16a34a' : '#dc2626'}`, borderRadius: 8 }}>
            <div>
              <div style={{ fontSize: 9, color: theme.textDim, marginBottom: 2 }}>Predicted</div>
              <div style={{ fontSize: 16, fontWeight: 800, color: isCorrect ? '#34d399' : '#f87171' }}>{result.label}</div>
            </div>
            <div style={{ fontSize: 20, color: isCorrect ? '#34d399' : '#f87171' }}>{isCorrect ? '✓' : '✗'}</div>
            <div>
              <div style={{ fontSize: 9, color: theme.textDim, marginBottom: 2 }}>Actual</div>
              <div style={{ fontSize: 14, fontWeight: 700, color: theme.textSecondary }}>{result.actualLabel}</div>
            </div>
            <div style={{ marginLeft: 'auto' }}>
              <div style={{ fontSize: 9, color: theme.textDim, marginBottom: 2 }}>Confidence</div>
              <div style={{ fontSize: 16, fontWeight: 800, color: result.confidence >= 0.8 ? '#34d399' : result.confidence >= 0.5 ? '#fbbf24' : '#f87171' }}>
                {(result.confidence * 100).toFixed(0)}%
              </div>
            </div>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {result.topK.map((item, i) => (
              <div key={item.label} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <span style={{ fontSize: 9, color: theme.textMuted, minWidth: 14, textAlign: 'right' }}>#{i + 1}</span>
                <span style={{ fontSize: 10, color: theme.textSecondary, minWidth: 100, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{item.label}</span>
                <div style={{ flex: 1, height: 6, background: theme.border, borderRadius: 3, overflow: 'hidden' }}>
                  <div style={{ width: `${item.score * 100}%`, height: '100%', background: SENSOR_COLORS[i % SENSOR_COLORS.length], borderRadius: 3 }} />
                </div>
                <span style={{ fontSize: 9, color: theme.textMuted, minWidth: 32, textAlign: 'right' }}>{(item.score * 100).toFixed(0)}%</span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

// ─── Main Predict Tab ─────────────────────────────────────────────────────
export default function PredictTab({ samples, sensors, activeSample, theme }) {
  const [cvResult, setCvResult] = useState(null);
  const [running, setRunning] = useState(false);
  const [k, setK] = useState(5);
  const [useTrainOnly, setUseTrainOnly] = useState(false);

  const trainSamples = useMemo(() =>
    samples.filter(s => s.values?.length > 0 && (!useTrainOnly || s.category === 'training') && !s.splitBaseId),
    [samples, useTrainOnly]
  );

  const model = useMemo(() => buildModel(trainSamples), [trainSamples]);

  const labelGroups = useMemo(() => {
    const m = {};
    trainSamples.forEach(s => { m[s.label] = (m[s.label] || 0) + 1; });
    return m;
  }, [trainSamples]);

  const runCV = useCallback(() => {
    if (!model) return;
    setRunning(true);
    setTimeout(() => {
      const res = crossValidate(model, 30);
      setCvResult(res);
      setRunning(false);
    }, 0);
  }, [model]);

  const n = trainSamples.length;
  const nLabels = Object.keys(labelGroups).length;

  if (!n) {
    return (
      <div style={{ textAlign: 'center', padding: 60, color: theme.textDim }}>
        <Target size={32} color={theme.textDim} style={{ marginBottom: 12 }} />
        <div style={{ fontSize: 13 }}>No samples loaded — import a dataset first</div>
      </div>
    );
  }

  return (
    <div>
      {/* Model info */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 8, marginBottom: 14 }}>
        {[
          ['Samples', n, '#38bdf8'],
          ['Labels', nLabels, '#34d399'],
          ['Sensors', sensors.length, '#fbbf24'],
          ['Features/ch', 7, '#a78bfa'],
        ].map(([k, v, c]) => (
          <div key={k} style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 8, padding: 10, textAlign: 'center' }}>
            <div style={{ fontSize: 18, fontWeight: 800, color: c }}>{v}</div>
            <div style={{ fontSize: 9, color: theme.textDim, marginTop: 1 }}>{k}</div>
          </div>
        ))}
      </div>

      {/* Controls */}
      <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14, marginBottom: 14 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10 }}>KNN Classifier — Leave-One-Out Cross-Validation</div>
        <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', alignItems: 'center', marginBottom: 10 }}>
          <label style={{ fontSize: 10, color: theme.textMuted, display: 'flex', flexDirection: 'column', gap: 3 }}>
            Neighbours (K)
            <div style={{ display: 'flex', gap: 4 }}>
              {[3, 5, 7, 10].map(v => (
                <button key={v} onClick={() => setK(v)} style={{
                  background: k === v ? '#1d4ed8' : theme.bgInput,
                  border: `1px solid ${k === v ? '#3b82f6' : theme.border}`,
                  color: k === v ? '#fff' : theme.textMuted,
                  borderRadius: 4, padding: '3px 8px', cursor: 'pointer', fontSize: 10, fontFamily: 'inherit',
                }}>{v}</button>
              ))}
            </div>
          </label>
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer', fontSize: 10, color: theme.textMuted, userSelect: 'none' }}>
            <input type="checkbox" checked={useTrainOnly} onChange={e => setUseTrainOnly(e.target.checked)} style={{ accentColor: '#3b82f6' }} />
            Training set only
          </label>
          <button onClick={runCV} disabled={running || !model}
            style={{ display: 'flex', alignItems: 'center', gap: 6, background: model ? '#065f46' : theme.border, border: 'none', color: model ? '#34d399' : theme.textDim, borderRadius: 6, padding: '7px 16px', cursor: 'pointer', fontSize: 11, fontWeight: 700, fontFamily: 'inherit' }}>
            {running ? <RefreshCw size={13} style={{ animation: 'spin 0.8s linear infinite' }} /> : <Target size={13} />}
            {running ? 'Running…' : 'Run Cross-Validation'}
          </button>
        </div>
        <div style={{ fontSize: 9, color: theme.textDim, lineHeight: 1.6 }}>
          Features: mean, std, min, max, range, RMS, zero-crossing rate per channel · {sensors.length} channels × 7 = {sensors.length * 7} features total
        </div>
      </div>

      {/* Confusion matrix */}
      {cvResult && (
        <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14, marginBottom: 14 }}>
          <ConfusionMatrix result={cvResult} theme={theme} />
        </div>
      )}

      {/* Live prediction */}
      <LivePredict model={model} samples={trainSamples} activeSample={activeSample} theme={theme} />

      {/* Label distribution */}
      <div style={{ background: theme.bgCard, border: `1px solid ${theme.border}`, borderRadius: 10, padding: 14 }}>
        <div style={{ fontSize: 11, fontWeight: 700, color: theme.textSecondary, marginBottom: 10 }}>Training Samples per Label</div>
        {Object.entries(labelGroups).sort(([, a], [, b]) => b - a).map(([label, count], i) => (
          <div key={label} style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 5 }}>
            <span style={{ fontSize: 10, color: theme.textSecondary, minWidth: 120, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{label}</span>
            <div style={{ flex: 1, height: 8, background: theme.border, borderRadius: 4, overflow: 'hidden' }}>
              <div style={{ width: `${(count / Math.max(...Object.values(labelGroups))) * 100}%`, height: '100%', background: SENSOR_COLORS[i % SENSOR_COLORS.length], borderRadius: 4, transition: 'width 0.3s' }} />
            </div>
            <span style={{ fontSize: 10, color: theme.textMuted, minWidth: 28, textAlign: 'right' }}>{count}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
