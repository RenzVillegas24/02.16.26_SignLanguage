/*
 * @file test_sensors_module.h
 * @brief Callable sensor test functions for the SignGlove environment.
 *        Wraps calibration + live-reading logic from Test_Sensors.cpp
 *        so it can be invoked from the GUI test screen.
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include "sensor_module/sensor_module.h"

/// Run a one-shot sensor test: reads all channels once,
/// prints stats to Serial, and fills `pd` for OLED display.
void test_sensors_read_once(const SensorData &raw, ProcessedSensorData &pd);

/// Build a formatted OLED string for the Flex test screen.
/// Writes into `buf`.
void test_sensors_format_flex(const ProcessedSensorData &pd,
                              char *buf, size_t len);

/// Build a formatted OLED string for the Hall (side) test screen.
void test_sensors_format_hall(const ProcessedSensorData &pd,
                              char *buf, size_t len);

/// Build a formatted OLED string for the MPU6050 test screen.
void test_sensors_format_mpu(const ProcessedSensorData &pd,
                             char *buf, size_t len);

