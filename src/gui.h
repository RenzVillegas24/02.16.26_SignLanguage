/*
 * @file gui.h
 * @brief LVGL GUI — screen management for all application modes
 */
#pragma once

#include <Arduino.h>
#include "config.h"

void gui_init();                       // Create all screens, show splash → menu
void gui_update(const SensorData &d);  // Feed latest sensor data into active screen
void gui_set_mode(AppMode mode);       // Switch to a mode screen
void gui_set_gesture(const char *text);// Update recognized gesture label
void gui_set_battery(int pct);         // Update battery indicator
void gui_show_web_qr(const char *url); // Set QR code content for WEB screen
void gui_set_train_status(const char *msg);
