#pragma once
#include "sensors.h"
#include "sensor_module/sensor_module.h"

/**
 * Start the WiFi Soft-AP and HTTP server.
 * LittleFS is mounted inside this call and kept mounted until stop.
 */
void web_server_start();
void web_server_stop();

/**
 * Must be called every loop() iteration.
 * @param d          Raw sensor data
 * @param pd         Processed sensor data (calibrated percentages)
 * @param gesture    Null-terminated gesture label (or "---")
 * @param confidence Prediction confidence 0.0 – 1.0  (optional, default 0)
 */
void web_server_update(const SensorData &d, const ProcessedSensorData &pd,
					   const char *gesture, float confidence);
void web_server_update(const SensorData &d, const ProcessedSensorData &pd,
					   const char *gesture);  // compat overload

void web_server_update(const SensorData &d, const char *gesture, float confidence);
void web_server_update(const SensorData &d, const char *gesture);  // compat overload

bool   web_server_is_running();
int    web_server_num_clients();
String web_server_get_url();