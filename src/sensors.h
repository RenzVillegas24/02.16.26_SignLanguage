/*
 * @file sensors.h
 * @brief Multiplexer-based sensor reading + ADS1115 ADC + MPU6050 IMU
 *
 * Sensor reading runs on a background FreeRTOS task (Core 0) with
 * vTaskDelayUntil() for precise 30 Hz pacing, decoupled from the LVGL
 * rendering loop on Core 1.
 *
 * An I2C mutex coordinates bus access between the sensor task and the
 * FT3168 touch controller.
 */
#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>
#include "config.h"

/// I2C bus mutex — shared between sensor task and touch controller.
/// Created inside sensors_init(), NULL before that (touch runs unguarded
/// during early boot when there is no sensor task to conflict with).
extern SemaphoreHandle_t i2c_mutex;

void     sensors_init();
void     sensors_read(SensorData &data);       ///< Non-blocking — copies latest data from background task
bool     sensors_mpu_available();
bool     sensors_ads_available();               ///< true if ADS1115 was detected
void     sensors_set_active(bool active);       ///< Pause/resume background reading (menu vs active modes)
uint16_t sensors_mux_read(uint8_t ch);          ///< Single mux+ADS1115 read (for calibration). Caller must NOT hold i2c_mutex.
void     sensors_shutdown();                    ///< Stop task, put MPU6050 to sleep + isolate mux GPIOs
void     sensors_set_rate_hz(int hz);             ///< Change sensor task rate (1-100 Hz, default 30)
