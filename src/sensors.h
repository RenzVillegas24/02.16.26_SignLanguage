/*
 * @file sensors.h
 * @brief Multiplexer-based sensor reading + MPU6050 IMU
 */
#pragma once

#include <Arduino.h>
#include "config.h"

void    sensors_init();
void    sensors_read(SensorData &data);
bool    sensors_mpu_available();
void    sensors_shutdown();         // put MPU6050 to sleep + isolate mux GPIOs
