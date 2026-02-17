/*
 * @file system_info.h
 * @brief ESP32-S3 system property helpers
 *
 * Provides a snapshot of RAM, PSRAM, Flash, CPU, chip and LVGL stats
 * that the GUI "About" screen can display.
 */
#pragma once

#include <Arduino.h>

// ── Snapshot struct ─────────────────────────────────────────────────
struct SystemInfoData {
    // Internal SRAM
    uint32_t ram_total;          // bytes
    uint32_t ram_free;
    uint32_t ram_used;
    uint8_t  ram_pct;            // 0-100

    // PSRAM (SPI RAM)
    uint32_t psram_total;        // bytes
    uint32_t psram_free;
    uint32_t psram_used;
    uint8_t  psram_pct;

    // Flash
    uint32_t flash_size;         // bytes
    uint32_t flash_speed;        // Hz

    // CPU
    uint32_t cpu_freq_mhz;       // MHz
    uint8_t  cpu_cores;
    uint8_t  cpu_usage_pct;      // 0-100  (fed externally)
    float    cpu_temp_c;         // internal temperature sensor (°C)

    // LVGL rendering
    uint16_t lvgl_fps;           // frames per second  (fed externally)

    // Chip
    const char *chip_model;      // e.g. "ESP32-S3"
    uint8_t  chip_revision;
    const char *sdk_version;

    // Uptime
    uint32_t uptime_sec;
};

// ── API ─────────────────────────────────────────────────────────────

/// Call once in setup() after Serial.begin()
void sysinfo_init();

/// Call periodically (e.g. every 1 s) to refresh the snapshot.
/// @param cpu_pct   CPU busy percentage computed by the caller
/// @param lvgl_fps  LVGL frames-per-second computed by the caller
void sysinfo_update(uint8_t cpu_pct, uint16_t lvgl_fps);

/// Return a const reference to the latest snapshot.
const SystemInfoData &sysinfo_get();

/// Print a one-shot summary to Serial (useful at boot).
void sysinfo_print_summary();
