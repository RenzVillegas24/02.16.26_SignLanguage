/*
 * @file display.h
 * @brief AMOLED display, touch controller, and LVGL driver initialization
 */
#pragma once

#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "lvgl.h"
#include "config.h"

// Exposed for direct GFX access (brightness control etc.)
extern Arduino_GFX *gfx;
extern std::unique_ptr<Arduino_IIC> touch_controller;

void display_init();
void display_set_brightness(uint8_t level);
void display_off();
void display_on();
