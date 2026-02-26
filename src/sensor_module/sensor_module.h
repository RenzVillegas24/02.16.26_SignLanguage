/*
 * @file sensor_module/sensor_module.h
 * @brief Sensor data processing module — calibration, smoothing, percentages,
 *        and future Edge Impulse sign-language prediction.
 *
 * Handles:
 *   • Flex sensor calibration + EMA smoothing + percentage (-100..+100)
 *   • Hall-effect (side) calibration + percentage (-100..+100)
 *   • Hall-effect (top) calibration + percentage (-100..+100)
 *   • MPU6050 pitch/roll (pass-through from SensorData)
 *   • Sign-language prediction placeholder (Edge Impulse – to be implemented)
 *
 * Usage:
 *   1. sensor_module_init()          — call once in setup()
 *   2. sensor_module_calibrate(...)  — blocking calibration (flat hand, no magnets)
 *   3. sensor_module_process(raw, out) — call each loop after sensors_read()
 *   4. sensor_module_predict(out)    — (future) returns predicted gesture label
 */
#pragma once

#include <Arduino.h>
#include "config.h"

// ─────────────────────────────────────────────
//  Processed sensor data (percentages + raw)
// ─────────────────────────────────────────────
struct ProcessedSensorData {
    // Flex: -100 (full down) → 0 (flat) → +100 (full up)
    int8_t   flex_pct[NUM_FLEX_SENSORS];
    uint16_t flex_raw[NUM_FLEX_SENSORS];

    // Hall side: -100 (back/away) → 0 (neutral) → +100 (front/close)
    int8_t   hall_pct[NUM_HALL_SENSORS];
    uint16_t hall_raw[NUM_HALL_SENSORS];

    // Hall top: -100 (back) → 0 (neutral) → +100 (front)
    int8_t   hall_top_pct[NUM_HALL_TOP_SENSORS];
    uint16_t hall_top_raw[NUM_HALL_TOP_SENSORS];

    // IMU (passed through)
    float accel_x, accel_y, accel_z;
    float gyro_x,  gyro_y,  gyro_z;
    float pitch, roll;

    // Prediction (future Edge Impulse)
    char  predicted_label[32];
    float prediction_confidence;
};

// ─────────────────────────────────────────────
//  Calibration progress callback (0-100 %)
// ─────────────────────────────────────────────
typedef void (*SensorCalibProgressCb)(int percent);

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

/// Initialise internal state (call once)
void sensor_module_init();

/// Blocking calibration (flat hand, no magnets).
/// Samples for ~5 s, sets baselines and deadzones.
/// Optional progress callback for GUI / serial feedback.
void sensor_module_calibrate(SensorCalibProgressCb progress_cb = nullptr);

/// Is calibration done?
bool sensor_module_is_calibrated();

/// Process a raw SensorData frame into calibrated percentages.
void sensor_module_process(const SensorData &raw, ProcessedSensorData &out);

/// Print a formatted stats block to Serial.
void sensor_module_print_serial(const ProcessedSensorData &pd);

/// Build a compact text summary suitable for an OLED label (LVGL).
/// Writes into `buf` (up to `buf_len` chars).
void sensor_module_format_oled(const ProcessedSensorData &pd,
                               char *buf, size_t buf_len);

/// (Future) Run Edge Impulse inference on ProcessedSensorData.
/// Returns the predicted label (also stored in pd.predicted_label).
const char *sensor_module_predict(ProcessedSensorData &pd);

// ─────────────────────────────────────────────
//  Calibration info — for GUI display
// ─────────────────────────────────────────────
struct FlexCalibInfo {
    uint16_t flat_value;
    uint16_t upward_range;
    uint16_t downward_range;
    uint16_t noise_deadzone;
};

struct HallCalibInfo {
    uint16_t normal;
    uint16_t front_range;
    uint16_t back_range;
};

/// Copy current calibration data into caller arrays.
void sensor_module_get_flex_cal(FlexCalibInfo out[NUM_FLEX_SENSORS]);
void sensor_module_get_hall_cal(HallCalibInfo out[NUM_HALL_SENSORS]);
void sensor_module_get_hall_top_cal(HallCalibInfo out[NUM_HALL_TOP_SENSORS]);

/// NVS persistence — save/load calibration data.
void sensor_module_save_calibration();
bool sensor_module_load_calibration();

