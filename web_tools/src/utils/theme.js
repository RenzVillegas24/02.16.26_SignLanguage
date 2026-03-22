// ─── Theme tokens ──────────────────────────────────────────────────────────
// All colors used in the app go through these tokens.
// Switching themes just swaps the object passed to ThemeProvider.

export const DARK = {
  name: 'dark',
  // Backgrounds
  bgBase:    '#060d1a',
  bgPanel:   '#080f1e',
  bgCard:    '#050c1a',
  bgInput:   '#060d1a',
  bgHover:   '#0a1628',
  bgActive:  '#091d38',
  bgSelected:'#071428',
  bgSidebar: '#020810',
  bgTopbar:  '#020810',
  bgSplit:   '#0a1628',
  // Borders
  border:    '#1e293b',
  borderMid: '#1a2540',
  borderHi:  '#1e3a5f',
  // Text
  textPrimary:   '#f1f5f9',
  textSecondary: '#94a3b8',
  textMuted:     '#475569',
  textDim:       '#334155',
  textFaint:     '#1e3a5f',
  // Accent
  accent:    '#38bdf8',
  accentAlt: '#60a5fa',
  // Canvas
  canvasBg:  '#060d1a',
  canvasGrid:'#0f1e33',
  // Scrollbar
  scrollTrack:'#0a1628',
  scrollThumb:'#1e3a5f',
};

export const LIGHT = {
  name: 'light',
  bgBase:    '#f1f5f9',
  bgPanel:   '#ffffff',
  bgCard:    '#f8fafc',
  bgInput:   '#ffffff',
  bgHover:   '#e2e8f0',
  bgActive:  '#dbeafe',
  bgSelected:'#eff6ff',
  bgSidebar: '#f8fafc',
  bgTopbar:  '#ffffff',
  bgSplit:   '#ffffff',
  border:    '#e2e8f0',
  borderMid: '#cbd5e1',
  borderHi:  '#93c5fd',
  textPrimary:   '#0f172a',
  textSecondary: '#374151',
  textMuted:     '#6b7280',
  textDim:       '#9ca3af',
  textFaint:     '#cbd5e1',
  accent:    '#2563eb',
  accentAlt: '#3b82f6',
  canvasBg:  '#f8fafc',
  canvasGrid:'#e2e8f0',
  scrollTrack:'#f1f5f9',
  scrollThumb:'#cbd5e1',
};
