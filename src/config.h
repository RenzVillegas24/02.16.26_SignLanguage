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
#define MUX_SIG         1       // MUX analog output pin (unused when ADS1115 is present)

// ADS1115 16-bit external ADC (reads MUX output via A0)
#define ADS1115_ADDR    0x48    // ADDR pin → GND

// Multiplexer channel assignments
#define MUX_CH_FLEX_THUMB   0
#define MUX_CH_FLEX_INDEX   1
#define MUX_CH_FLEX_MIDDLE  2
#define MUX_CH_FLEX_RING    6
#define MUX_CH_FLEX_PINKY   7
#define MUX_CH_HALL_THUMB   8
#define MUX_CH_HALL_INDEX   9
#define MUX_CH_HALL_MIDDLE  10
#define MUX_CH_HALL_RING    11
#define MUX_CH_HALL_PINKY   12

/*
Hall-effect sensors on TOP of each finger (No longer used)
#define MUX_CH_HALL_TOP_THUMB   3
#define MUX_CH_HALL_TOP_INDEX   4
#define MUX_CH_HALL_TOP_MIDDLE  5
#define MUX_CH_HALL_TOP_RING    13
#define MUX_CH_HALL_TOP_PINKY   14
*/

#define NUM_FLEX_SENSORS      5
#define NUM_HALL_SENSORS      5
/*
Hall-effect sensors on TOP of each finger (No Longer Used)
#define NUM_HALL_SIDE_SENSORS 5
*/
#define NUM_SENSOR_CHANNELS   10


// ─────────────────────────────────────────────
//  I2S Audio (MAX98357A)
// ─────────────────────────────────────────────
#define I2S_BCLK        40
#define I2S_LRCK        41
#define I2S_DOUT        42

// ─────────────────────────────────────────────
//  Power & Battery
// ─────────────────────────────────────────────
#define PIN_POWER_BTN   2                           // Momentary button → GND (sleep/wake)
#define PIN_BAT_ADC     BATTERY_VOLTAGE_ADC_DATA    // Battery voltage ADC (internal)

// Battery voltage conversion (voltage divider on board)
#define BAT_ADC_FACTOR  2.0f    // Voltage divider ratio
#define BAT_FULL_V      4.144f
#define BAT_EMPTY_V     3.3f
// Battery internal resistance estimate (mΩ).
// Used to compensate the terminal-voltage rise while charging:
//   OCV ≈ VBAT_measured − (I_charge × R_internal)
// Increase if battery still reads too high when first plugged in;
// decrease if the percentage drops too much under heavy load.
#define BAT_INT_R_MOHM  150

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
    MODE_PREDICT_LOCAL,
    MODE_PREDICT_WEB,
    MODE_SETTINGS,
    MODE_TEST
};

// ─────────────────────────────────────────────
//  Sensor Data Structure
// ─────────────────────────────────────────────
struct SensorData {
    uint16_t flex[NUM_FLEX_SENSORS];         // Raw ADC (0-32767 via ADS1115, 0-4095 fallback)
    uint16_t hall[NUM_HALL_SENSORS];         // Raw ADC (side of finger)
    float    accel_x, accel_y, accel_z;      // m/s²
    float    gyro_x,  gyro_y,  gyro_z;      // rad/s
    float    pitch, roll;                    // Degrees
};

// ─────────────────────────────────────────────
//  Timing Constants
// ─────────────────────────────────────────────
#define SENSOR_READ_INTERVAL_MS     50
#define DISPLAY_UPDATE_INTERVAL_MS  100
#define BATTERY_READ_INTERVAL_MS    2000
#define TRAIN_SERIAL_INTERVAL_MS    50
#define POWER_BTN_DEBOUNCE_MS       300
#define AUTO_SLEEP_TIMEOUT_MS       60000   // 60 seconds
#define MUX_SETTLE_US               100     // Microseconds

// ─────────────────────────────────────────────
//  Edge Impulse Configuration
// ─────────────────────────────────────────────
#define EI_SERIAL_BAUD          115200
#define EI_NUM_FEATURES         18  // 5 flex + 5 hall + 3 accel + 3 gyro + pitch + roll
