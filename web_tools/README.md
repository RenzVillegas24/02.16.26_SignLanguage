# ⚡ EI Studio — EdgeImpulse Data Manager

A React app for managing EdgeImpulse exported sensor datasets.

## Setup

```bash
npm install
npm start
```

## How to Import Your Dataset

EdgeImpulse exports your dataset as a ZIP. The expected structure inside is:

```
info.labels          ← dataset manifest (label + category per file)
testing/
  kumusta.json.6k24gu8r.ingestion-....json
  ...
training/
  takbo.json.6k28e9u2.ingestion-....json
  ...
```

**To import:** Click **📦 Import ZIP** (or drag-and-drop the .zip onto the sidebar).

The app will:
1. Parse `info.labels` to get label names, categories (training/testing), and enabled status
2. Read every `.json` file in `testing/` and `training/`
3. Cross-reference each file with the manifest to assign the correct label and category

You can also drag individual `.json` sample files or a `.labels` file directly.

## Features

- **Waveform viewer** — click any sample to view it; check multiple to overlay them
- **Smart Split** — 7 auto-detection algorithms with interactive drag handles
- **Merge** — combine multiple checked samples into one
- **DeDup** — remove exact duplicate samples by signal hash
- **Export** — download samples as EdgeImpulse-compatible `.json`
- **Filters** — by label, category (training/testing), enabled/disabled, duration range
- **Groups view** — per-label training/testing breakdown with merge & export per label
- **Stats** — distribution charts, category summary, channel list

## Sensor channels supported

flex0–4, hall0–4, ax, ay, az, gx, gy, gz, pitch, roll (18 channels)
