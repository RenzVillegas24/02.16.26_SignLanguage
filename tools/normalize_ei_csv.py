#!/usr/bin/env python3
"""
normalize_ei_csv.py — Normalize raw SignGlove CSV data for Edge Impulse upload.

Replicates the EXACT normalization performed on-device in sensor_module.cpp:
  - Flex:  (raw - flat_value) / range → clamped to [-1.0, 1.0]
  - Hall:  (raw - normal) / range    → clamped to [-1.0, 1.0]
  - IMU:   Left in physical units (m/s², °/s, degrees)

Usage:
  python normalize_ei_csv.py --input raw_data/ --output normalized/ --cal calibration.json

  Input:  Directory of raw CSV files organized by label (raw_data/hello/001.csv)
  Output: Directory with normalized CSV files matching EI upload format
  Cal:    JSON file exported from SignGlove via 'cal_dump' serial command

The CSV format expected from the SignGlove Web Serial app is:
  DATA,timestamp,flex0,flex1,flex2,flex3,flex4,hall0,hall1,hall2,hall3,hall4,
  accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,pitch,roll
"""

import argparse
import csv
import json
import os
import sys
import numpy as np
from pathlib import Path


def load_calibration(cal_path: str) -> dict:
    """Load calibration JSON exported from SignGlove."""
    with open(cal_path, 'r') as f:
        data = json.load(f)
    cal = data.get('calibration', data)
    
    flex_cal = []
    for fc in cal['flex']:
        flex_cal.append({
            'flat': fc['flat'],
            'up_range': fc['up_range'],
            'down_range': fc['down_range'],
            'deadzone': fc['deadzone'],
        })
    
    hall_cal = []
    for hc in cal['hall']:
        hall_cal.append({
            'normal': hc['normal'],
            'front_range': hc['front_range'],
            'back_range': hc['back_range'],
        })
    
    return {'flex': flex_cal, 'hall': hall_cal}


def normalize_flex(raw: float, cal: dict) -> float:
    """Replicate calc_flex_pct from sensor_module.cpp, output as [-1.0, 1.0]."""
    diff = raw - cal['flat']
    dz = cal['deadzone']
    
    if abs(diff) <= dz:
        return 0.0
    
    if diff > 0:
        rng = cal['up_range']
        if rng <= 0:
            rng = 1
        pct = (diff * 100) / rng
        return max(0.0, min(100.0, pct)) / 100.0
    else:
        rng = cal['down_range']
        if rng <= 0:
            rng = 1
        pct = (diff * 100) / rng
        return max(-100.0, min(0.0, pct)) / 100.0


def normalize_hall(raw: float, cal: dict) -> float:
    """Replicate calc_hall_side_pct from sensor_module.cpp, output as [-1.0, 1.0]."""
    diff = raw - cal['normal']
    
    if diff > 0:
        if cal['front_range'] == 0:
            return 0.0
        pct = (diff * 100) / cal['front_range']
        return max(0.0, min(100.0, pct)) / 100.0
    elif diff < 0:
        if cal['back_range'] == 0:
            return 0.0
        pct = (diff * 100) / cal['back_range']
        return max(-100.0, min(0.0, pct)) / 100.0
    return 0.0


def normalize_csv(input_path: str, output_path: str, cal: dict, frequency: int = 30):
    """Normalize a single raw CSV file and write EI-compatible output."""
    rows = []
    
    with open(input_path, 'r') as f:
        reader = csv.reader(f)
        for row in reader:
            # Skip empty rows and header rows
            if not row or row[0].startswith('#'):
                continue
            # Handle DATA prefix from streaming format
            if row[0] == 'DATA':
                row = row[1:]  # remove DATA prefix
            # Skip timestamp if present (first column is timestamp)
            if len(row) >= 19:
                # timestamp,flex0..4,hall0..4,ax,ay,az,gx,gy,gz,pitch,roll
                ts = row[0]
                values = [float(v) for v in row[1:19]]
            elif len(row) == 18:
                values = [float(v) for v in row[0:18]]
            else:
                continue
            
            # Normalize flex (indices 0-4)
            norm_flex = []
            for i in range(5):
                norm_flex.append(normalize_flex(values[i], cal['flex'][i]))
            
            # Normalize hall (indices 5-9)
            norm_hall = []
            for i in range(5):
                norm_hall.append(normalize_hall(values[5 + i], cal['hall'][i]))
            
            # IMU stays as-is (indices 10-17)
            imu = values[10:18]
            
            rows.append(norm_flex + norm_hall + imu)
    
    if not rows:
        print(f"  WARNING: No data rows in {input_path}")
        return 0
    
    # Write EI-compatible CSV
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    header = [
        'flex0', 'flex1', 'flex2', 'flex3', 'flex4',
        'hall0', 'hall1', 'hall2', 'hall3', 'hall4',
        'accel_x', 'accel_y', 'accel_z',
        'gyro_x', 'gyro_y', 'gyro_z',
        'pitch', 'roll'
    ]
    
    with open(output_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for row in rows:
            writer.writerow([f'{v:.4f}' for v in row])
    
    return len(rows)


def main():
    parser = argparse.ArgumentParser(
        description='Normalize raw SignGlove CSV data for Edge Impulse upload')
    parser.add_argument('--input', '-i', required=True,
                        help='Input directory with raw CSV files (organized by label subdirs)')
    parser.add_argument('--output', '-o', required=True,
                        help='Output directory for normalized CSV files')
    parser.add_argument('--cal', '-c', required=True,
                        help='Calibration JSON file (from cal_dump serial command)')
    parser.add_argument('--frequency', '-f', type=int, default=30,
                        help='Sampling frequency in Hz (default: 30)')
    args = parser.parse_args()
    
    # Load calibration
    print(f"Loading calibration from {args.cal}")
    cal = load_calibration(args.cal)
    
    print(f"\nCalibration summary:")
    for i, fc in enumerate(cal['flex']):
        print(f"  Flex {i}: flat={fc['flat']}, up={fc['up_range']}, "
              f"down={fc['down_range']}, dz={fc['deadzone']}")
    for i, hc in enumerate(cal['hall']):
        print(f"  Hall {i}: normal={hc['normal']}, front={hc['front_range']}, "
              f"back={hc['back_range']}")
    
    # Process all CSV files
    input_dir = Path(args.input)
    output_dir = Path(args.output)
    total_files = 0
    total_samples = 0
    
    for label_dir in sorted(input_dir.iterdir()):
        if not label_dir.is_dir():
            continue
        label = label_dir.name
        print(f"\nProcessing label: {label}")
        
        for csv_file in sorted(label_dir.glob('*.csv')):
            out_path = output_dir / label / csv_file.name
            n_rows = normalize_csv(str(csv_file), str(out_path), cal, args.frequency)
            if n_rows > 0:
                print(f"  {csv_file.name}: {n_rows} samples → {out_path}")
                total_files += 1
                total_samples += n_rows
    
    print(f"\n{'='*50}")
    print(f"Done! Normalized {total_files} files, {total_samples} total samples")
    print(f"Output directory: {output_dir}")
    print(f"\nNext steps:")
    print(f"  1. Upload normalized CSVs to Edge Impulse")
    print(f"  2. Set frequency to {args.frequency} Hz")
    print(f"  3. Create impulse:")
    print(f"     - Flatten DSP for flex0..4 + hall0..4 (10 axes)")
    print(f"     - Spectral Analysis DSP for accel + gyro (6 axes)")
    print(f"     - Flatten DSP for pitch + roll (2 axes)")


if __name__ == '__main__':
    main()
