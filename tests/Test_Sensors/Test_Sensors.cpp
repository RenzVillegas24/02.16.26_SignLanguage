/*
 * @file Test_Sensors.cpp
 * @brief Comprehensive sensor test — utilises sensor_module for calibration
 *        and processing, sensors.h for hardware reading, and
 *        test_sensors_module for formatted output.
 *
 * Reads all 21 sensor channels:
 *   5 Flex, 5 Hall (side), 5 Hall (top), MPU6050 (accel + gyro)
 */
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "sensors.h"
#include "sensor_module/sensor_module.h"
#include "test_sensors_module.h"

// ── Calibration progress callback (serial) ──
static void calib_progress(int pct) {
    Serial.printf("\rCalibrating: %3d%%", pct);
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   Test_Sensors — All Sensor Channels   ║");
    Serial.println("║   5 Flex + 5 Hall Side + 5 Hall Top    ║");
    Serial.println("║              + MPU6050                  ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    Serial.println("Multiplexer pin configuration:");
    Serial.printf("  MUX_S0  = %d\n", MUX_S0);
    Serial.printf("  MUX_S1  = %d\n", MUX_S1);
    Serial.printf("  MUX_S2  = %d\n", MUX_S2);
    Serial.printf("  MUX_S3  = %d\n", MUX_S3);
    Serial.printf("  MUX_SIG = %d  (ADC input)\n", MUX_SIG);
    Serial.println();

    // 1. Initialise hardware (mux + MPU6050)
    sensors_init();
    Serial.printf("[TEST] MPU6050: %s\n",
                  sensors_mpu_available() ? "OK" : "NOT DETECTED");

    // 2. Initialise sensor processing module
    sensor_module_init();

    // 3. Run calibration (flat hand, no magnets, ~5 s)
    sensor_module_calibrate(calib_progress);
    Serial.println();

    Serial.println("[TEST] Starting continuous sensor readings...\n");
}

void loop() {
    // 1. Read all raw sensor channels
    SensorData raw = {};
    sensors_read(raw);

    // 2. Process through sensor module (EMA, calibration, percentages)
    ProcessedSensorData pd = {};
    test_sensors_read_once(raw, pd);

    // 3. Print formatted sections (using test module formatters)
    char buf[512];

    test_sensors_format_flex(pd, buf, sizeof(buf));
    Serial.println(buf);

    test_sensors_format_hall(pd, buf, sizeof(buf));
    Serial.println(buf);

    test_sensors_format_hall_top(pd, buf, sizeof(buf));
    Serial.println(buf);

    test_sensors_format_mpu(pd, buf, sizeof(buf));
    Serial.println(buf);

    Serial.println();
    delay(250);
}
