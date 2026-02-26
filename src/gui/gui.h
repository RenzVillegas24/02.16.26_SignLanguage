/*
 * @file gui/gui.h
 * @brief LVGL GUI — public API for screen management
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include "sensor_module/sensor_module.h"

// Forward declaration
struct SystemInfoData;

void gui_init();                       // Create all screens, show splash → menu
void gui_update(const SensorData &d);  // Feed latest sensor data into active screen
void gui_set_mode(AppMode mode);       // Switch to a mode screen
void gui_set_gesture(const char *text);// Update recognized gesture label
void gui_set_battery(int pct);         // Update battery indicator
void gui_set_cpu_usage(int pct);       // Update CPU usage indicator (0-100)
void gui_show_web_qr(const char *url); // Set QR code content for WEB screen
void gui_web_set_connected(bool connected); // Toggle WiFi QR ↔ webpage QR
void gui_set_train_status(const char *msg);

// Settings
void gui_set_volume(uint8_t vol);         // 0-100
void gui_set_brightness(uint8_t brt);     // 0-255
uint8_t gui_get_volume();
uint8_t gui_get_brightness();

// Local-mode flag getters
bool gui_local_show_sensors();
bool gui_local_show_words();
bool gui_local_use_speech();

// About / system info — update live data on the about panel
void gui_update_about(const SystemInfoData &info);

// Tests — update live data on test screen (with processed sensor data)
void gui_test_update(const SensorData &d, const ProcessedSensorData &pd);

// Calibration UI control
void gui_update_calibration_progress(int pct);
bool gui_is_calibrating();

// Mode callback registration
void gui_register_mode_callback(void (*cb)(AppMode));

// Test action callbacks (set by main.cpp)
void gui_register_test_speaker_cb(void (*cb)());
void gui_register_test_oled_cb(void (*cb)());
void gui_register_brightness_cb(void (*cb)(uint8_t));
void gui_register_volume_cb(void (*cb)(uint8_t));
