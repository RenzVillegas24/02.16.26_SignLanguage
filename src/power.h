/*
 * @file power.h
 * @brief Power management — button, battery, SY6970 charger, sleep
 */
#pragma once
#include <Arduino.h>

void  power_init();
void  power_update();              // call each loop iteration
bool  power_button_pressed();      // true on short press (debounced)
bool  power_button_long_press();   // true on ≥2 s hold
float power_battery_voltage();
int   power_battery_percent();
void  power_deep_sleep();
void  power_reset_idle_timer();

// ── SY6970 charger status ──────────────────────────────────────────
bool  power_is_charging();         // true when Pre-charge or Fast Charging
bool  power_usb_connected();       // true when any input source detected
const char *power_charging_status_str();  // human-readable status
int   power_battery_voltage_mv();  // battery voltage from SY6970 (mV)
int   power_input_voltage_mv();    // USB/input voltage from SY6970 (mV)
int   power_charging_current_ma(); // charging current (mA)
