/*
 * @file power.h
 * @brief Power management — button, battery, SY6970 charger, light/deep sleep
 */
#pragma once
#include <Arduino.h>

// ── Full SY6970 snapshot (for battery test screen, etc.) ───────────
struct PowerInfo {
    // Measured values
    int   battery_mv;
    int   system_mv;
    int   input_mv;
    int   charge_ma;
    float ntc_pct;             // NTC voltage percentage (0.0–100.0)
    // Status strings
    char  charge_status[32];
    char  bus_status[32];
    char  bus_connection[32];
    char  input_source[32];
    char  input_usb[32];
    char  sys_voltage_status[32];
    char  thermal_reg_status[32];
    // Fault strings
    char  charging_fault[32];
    char  battery_fault[32];
    char  ntc_fault[32];
    // Derived
    bool  is_charging;
    bool  usb_connected;
    int   battery_pct;         // 0–100
    // Internal ADC (PIN_BAT_ADC voltage divider)
    int   adc_raw;             // Raw 12-bit ADC reading (0–4095)
    int   adc_mv;              // Compensated voltage after divider (mV)
};

void  power_init();
void  power_update();              // call each loop iteration
bool  power_button_pressed();      // true on short press (debounced)
bool  power_button_long_press();   // true on ≥2 s hold
float power_battery_voltage();
int   power_battery_percent();
void  power_reset_idle_timer();
uint32_t power_idle_elapsed_ms();    // ms since last user interaction

// ── Sleep / power control ──────────────────────────────────────────
void  power_light_sleep();           // light sleep — resumes on button press
void  power_deep_sleep();            // deep sleep (shutdown) — resets on button press
void  power_restart();               // software restart (esp_restart)
bool  power_is_deep_sleep_wake();    // true if this boot was a deep-sleep wakeup

// ── SY6970 charger status ──────────────────────────────────────────
bool  power_is_charging();           // true when Pre-charge or Fast Charging
bool  power_usb_connected();         // true when any input source detected
bool  power_usb_state_changed();     // true once per USB plug/unplug edge (clears on read)
const char *power_charging_status_str();  // human-readable status
int   power_battery_voltage_mv();    // battery voltage from SY6970 (mV)
int   power_system_voltage_mv();     // system voltage from SY6970 (mV)
int   power_input_voltage_mv();      // USB/input voltage from SY6970 (mV)
int   power_charging_current_ma();   // charging current (mA)

/// Thread-safe snapshot of all SY6970 values/statuses (for battery test)
PowerInfo power_get_info();
