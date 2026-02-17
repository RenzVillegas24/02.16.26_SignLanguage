/*
 * @file config.h
 * @brief Pin definitions and configuration for Sign Language Translator Glove v4.0
 * @platform LILYGO T-Display S3 AMOLED 1.64
 */
#pragma once

#include <Arduino.h>
#include "pin_config.h"

// ─────────────────────────────────────────────
//  Multiplexer (CD74HC4067)
// ─────────────────────────────────────────────
#define MUX_S0          21
#define MUX_S1          47
#define MUX_S2          48
#define MUX_S3          45
#define MUX_SIG         1       // ADC1_CH0

// Multiplexer channel assignments
#define MUX_CH_FLEX_THUMB   0
#define MUX_CH_FLEX_INDEX   1
#define MUX_CH_FLEX_MIDDLE  2
#define MUX_CH_FLEX_RING    3
#define MUX_CH_FLEX_PINKY   4
#define MUX_CH_HALL_THUMB   5
#define MUX_CH_HALL_INDEX   6
#define MUX_CH_HALL_MIDDLE  7
#define MUX_CH_HALL_RING    8
#define MUX_CH_HALL_PINKY   9

#define NUM_FLEX_SENSORS    5
#define NUM_HALL_SENSORS    5
#define NUM_SENSOR_CHANNELS 10

// ─────────────────────────────────────────────
//  I2S Audio (MAX98357A)
// ─────────────────────────────────────────────
#define I2S_BCLK        40
#define I2S_LRCK        41
#define I2S_DOUT        42

// ─────────────────────────────────────────────
//  Power & Battery
// ─────────────────────────────────────────────
#define PIN_POWER_BTN   38                          // Momentary button → GND (sleep/wake)
#define PIN_BAT_ADC     BATTERY_VOLTAGE_ADC_DATA    // Battery voltage ADC (internal)

// Battery voltage conversion (voltage divider on board)
#define BAT_ADC_FACTOR  2.0f    // Voltage divider ratio
#define BAT_FULL_V      4.2f
#define BAT_EMPTY_V     3.3f

// ─────────────────────────────────────────────
//  MPU6050 IMU (shares I2C bus with FT3168)
// ─────────────────────────────────────────────
#define MPU6050_ADDR    0x68    // AD0 = LOW

// ─────────────────────────────────────────────
//  WiFi AP Configuration (for WEB mode)
// ─────────────────────────────────────────────
#define WIFI_AP_SSID    "SignGlove"
#define WIFI_AP_PASS    "signlang123"
#define WEB_SERVER_PORT 80

// ─────────────────────────────────────────────
//  Application Modes
// ─────────────────────────────────────────────
enum AppMode {
    MODE_MENU = 0,
    MODE_TRAIN,
    MODE_PREDICT_WORDS,
    MODE_PREDICT_SPEECH,
    MODE_PREDICT_BOTH,
    MODE_PREDICT_WEB
};

// ─────────────────────────────────────────────
//  Sensor Data Structure
// ─────────────────────────────────────────────
struct SensorData {
    uint16_t flex[NUM_FLEX_SENSORS];     // Raw ADC 0-4095
    uint16_t hall[NUM_HALL_SENSORS];     // Raw ADC 0-4095
    float    accel_x, accel_y, accel_z;  // m/s²
    float    gyro_x,  gyro_y,  gyro_z;  // rad/s
    float    pitch, roll;                // Degrees
};

// ─────────────────────────────────────────────
//  Timing Constants
// ─────────────────────────────────────────────
#define SENSOR_READ_INTERVAL_MS     50
#define DISPLAY_UPDATE_INTERVAL_MS  100
#define BATTERY_READ_INTERVAL_MS    5000
#define TRAIN_SERIAL_INTERVAL_MS    50
#define POWER_BTN_DEBOUNCE_MS       300
#define AUTO_SLEEP_TIMEOUT_MS       60000   // 60 seconds
#define MUX_SETTLE_US               100     // Microseconds

// ─────────────────────────────────────────────
//  Edge Impulse Configuration
// ─────────────────────────────────────────────
#define EI_SERIAL_BAUD          115200
#define EI_NUM_FEATURES         16  // 5 flex + 5 hall + 3 accel + 3 gyro
