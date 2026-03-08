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
void gui_set_predict_confidence(float conf); // Update prediction confidence %
void gui_set_predict_status(const char *text); // Update predict status text
void gui_set_battery(int pct);         // Update battery indicator
void gui_set_charging(bool charging);  // Show/hide charging bolt indicator
void gui_show_charge_popup(bool charging, int pct);  // Full-screen charging overlay (5 s auto-dismiss)
void gui_hide_charge_popup();                         // Manually dismiss charging popup
bool gui_charge_popup_visible();
void gui_set_cpu_usage(int pct);       // Update CPU usage indicator (0-100)
void gui_show_web_qr(const char *url); // Set QR code content for WEB screen
void gui_web_set_connected(bool connected); // Toggle WiFi QR ↔ webpage QR
void gui_set_train_status(const char *msg);

// Train — update live sensor display (bars + IMU + counter)
void gui_train_update(const SensorData &d, const ProcessedSensorData &pd, uint32_t sample_count);

// Predict Local — update live sensor display (same style as train, percentage bars)
void gui_local_sensor_update(const ProcessedSensorData &pd);

// Settings
void gui_set_volume(uint8_t vol);         // 0-100
void gui_set_brightness(uint8_t brt);     // 0-255
uint8_t gui_get_volume();
uint8_t gui_get_brightness();
uint8_t gui_get_sleep_min();              // auto-sleep minutes (1-30)
bool    gui_get_lock_screen_on();         // true → lock screen instead of sleep in active modes

// Local-mode flag getters
bool gui_local_show_sensors();
bool gui_local_show_words();
bool gui_local_use_speech();
const char *gui_local_voice_dir();   // returns "boy" or "girl"

// About / system info — update live data on the about panel
void gui_update_about(const SystemInfoData &info);

// Tests — update live data on test screen (with processed sensor data)
void gui_test_update(const SensorData &d, const ProcessedSensorData &pd);

// Calibration UI control
void gui_update_calibration_progress(int pct);
bool gui_is_calibrating();

// Power menu dialog (overlay on lv_layer_top)
enum PowerAction { PWR_NONE = 0, PWR_SLEEP, PWR_SHUTDOWN, PWR_RESTART, PWR_CANCEL };
void gui_show_power_menu();
void gui_hide_power_menu();
bool gui_power_menu_visible();
void gui_register_power_cb(void (*cb)(PowerAction));

// Mode callback registration
void gui_register_mode_callback(void (*cb)(AppMode));

// Test action callbacks (set by main.cpp)
void gui_register_test_speaker_cb(void (*cb)());
void gui_register_test_oled_cb(void (*cb)());
void gui_register_brightness_cb(void (*cb)(uint8_t));
void gui_register_volume_cb(void (*cb)(uint8_t));

// ── Auto-sleep warning dialog ──────────────────────────────────────
void gui_show_sleep_warning(int seconds_left);   // show/update countdown
void gui_hide_sleep_warning();                    // dismiss warning
bool gui_sleep_warning_visible();

// ── Lock screen (always-on for Train / Predict modes) ──────────────
void gui_show_lock_screen(AppMode mode);          // enter lock screen
void gui_hide_lock_screen();                      // exit lock screen
bool gui_lock_screen_visible();
void gui_lock_update_gesture(const char *text);   // update prediction text
void gui_lock_update_battery(int pct);            // update battery %
