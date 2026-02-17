/*
 * @file power.h
 * @brief Power management — button, battery, sleep
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
