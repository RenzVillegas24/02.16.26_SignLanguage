/*
 * @file system_info.cpp
 * @brief ESP32-S3 system property helpers — implementation
 */
#include "system_info.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_idf_version.h>

static SystemInfoData s_info = {};

// ════════════════════════════════════════════════════════════════════
//  Internal helpers
// ════════════════════════════════════════════════════════════════════
static const char *chip_model_str() {
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    switch (ci.model) {
#ifdef CHIP_ESP32S3
        case CHIP_ESP32S3: return "ESP32-S3";
#endif
#ifdef CHIP_ESP32S2
        case CHIP_ESP32S2: return "ESP32-S2";
#endif
        case CHIP_ESP32:   return "ESP32";
        default:           return "ESP32-S3";      // fallback
    }
}

// ════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════
void sysinfo_init() {
    memset(&s_info, 0, sizeof(s_info));

    // ── Flash ──────────────────────────────────────────────────────
    uint32_t flash_sz = 0;
    if (esp_flash_get_size(NULL, &flash_sz) == ESP_OK)
        s_info.flash_size = flash_sz;
    else
        s_info.flash_size = ESP.getFlashChipSize();

    s_info.flash_speed = ESP.getFlashChipSpeed();

    // ── CPU ────────────────────────────────────────────────────────
    s_info.cpu_freq_mhz = ESP.getCpuFreqMHz();

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    s_info.cpu_cores    = ci.cores;
    s_info.chip_revision = ci.revision;
    s_info.chip_model   = chip_model_str();
    s_info.sdk_version  = esp_get_idf_version();

    // ── First snapshot ─────────────────────────────────────────────
    sysinfo_update(0, 0);
}

void sysinfo_update(uint8_t cpu_pct, uint16_t lvgl_fps) {
    // ── Internal SRAM ──────────────────────────────────────────────
    s_info.ram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s_info.ram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_info.ram_used  = s_info.ram_total - s_info.ram_free;
    s_info.ram_pct   = s_info.ram_total ? (uint8_t)((uint64_t)s_info.ram_used * 100 / s_info.ram_total) : 0;

    // ── PSRAM ──────────────────────────────────────────────────────
    s_info.psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s_info.psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s_info.psram_used  = s_info.psram_total - s_info.psram_free;
    s_info.psram_pct   = s_info.psram_total ? (uint8_t)((uint64_t)s_info.psram_used * 100 / s_info.psram_total) : 0;

    // ── CPU / LVGL (externally provided) ───────────────────────────
    s_info.cpu_freq_mhz  = ESP.getCpuFreqMHz();
    s_info.cpu_usage_pct = cpu_pct;
    s_info.lvgl_fps      = lvgl_fps;
    s_info.cpu_temp_c    = temperatureRead();

    // ── Uptime ─────────────────────────────────────────────────────
    s_info.uptime_sec = (uint32_t)(millis() / 1000);
}

const SystemInfoData &sysinfo_get() {
    return s_info;
}

void sysinfo_print_summary() {
    Serial.println("═══════ System Info ═══════");
    Serial.printf("  Chip   : %s rev %d  (%d core%s)\n",
                  s_info.chip_model, s_info.chip_revision,
                  s_info.cpu_cores, s_info.cpu_cores > 1 ? "s" : "");
    Serial.printf("  CPU    : %lu MHz\n", (unsigned long)s_info.cpu_freq_mhz);
    Serial.printf("  SDK    : %s\n", s_info.sdk_version);
    Serial.printf("  Flash  : %lu KB @ %lu MHz\n",
                  (unsigned long)(s_info.flash_size / 1024),
                  (unsigned long)(s_info.flash_speed / 1000000));
    Serial.printf("  RAM    : %lu / %lu KB (%d%%)\n",
                  (unsigned long)(s_info.ram_used / 1024),
                  (unsigned long)(s_info.ram_total / 1024),
                  s_info.ram_pct);
    Serial.printf("  PSRAM  : %lu / %lu KB (%d%%)\n",
                  (unsigned long)(s_info.psram_used / 1024),
                  (unsigned long)(s_info.psram_total / 1024),
                  s_info.psram_pct);
    Serial.println("═══════════════════════════");
}
