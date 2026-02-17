/*
 * @file gui/gui_api.cpp
 * @brief Public GUI API + global state + NVS persistence
 */
#include "gui_internal.h"
#include "gui/gui.h"
#include "system_info.h"
#include <Preferences.h>

// ════════════════════════════════════════════════════════════════════
//  Screen objects
// ════════════════════════════════════════════════════════════════════
lv_obj_t *scr_splash      = nullptr;
lv_obj_t *scr_menu         = nullptr;
lv_obj_t *scr_predict      = nullptr;
lv_obj_t *scr_local        = nullptr;
lv_obj_t *scr_train        = nullptr;
lv_obj_t *scr_web          = nullptr;
lv_obj_t *scr_settings     = nullptr;
lv_obj_t *scr_test         = nullptr;
lv_obj_t *scr_test_detail  = nullptr;

// ════════════════════════════════════════════════════════════════════
//  Widget pointers
// ════════════════════════════════════════════════════════════════════
lv_obj_t *lbl_gesture      = nullptr;   // local screen gesture label

lv_obj_t *bar_flex[5]      = {};        // local screen sensor bars
lv_obj_t *bar_hall[5]      = {};
lv_obj_t *bars_container   = nullptr;   // hides/shows bars on local screen

lv_obj_t *slider_brightness= nullptr;
lv_obj_t *slider_volume    = nullptr;
lv_obj_t *slider_sleep     = nullptr;
lv_obj_t *lbl_brt_val      = nullptr;
lv_obj_t *lbl_vol_val      = nullptr;
lv_obj_t *lbl_slp_val      = nullptr;

lv_obj_t *sw_dark_mode     = nullptr;
lv_obj_t *dd_fps           = nullptr;

lv_obj_t *lbl_about        = nullptr;
lv_obj_t *lbl_train_stat   = nullptr;

lv_obj_t *qr_wifi           = nullptr;
lv_obj_t *qr_web            = nullptr;
lv_obj_t *lbl_web_stat      = nullptr;
bool      web_client_connected = false;

lv_obj_t *lbl_test_detail  = nullptr;
lv_obj_t *lbl_test_title   = nullptr;   // dynamic header title for test detail
lv_obj_t *test_vol_row     = nullptr;
lv_obj_t *slider_test_vol  = nullptr;
lv_obj_t *lbl_test_vol_val = nullptr;
lv_obj_t *test_brt_row     = nullptr;   // OLED test brightness row
lv_obj_t *slider_test_brt  = nullptr;
lv_obj_t *lbl_test_brt_val = nullptr;
lv_obj_t *btn_benchmark    = nullptr;   // OLED benchmark button

lv_obj_t *bat_labels[SI_COUNT] = {};
lv_obj_t *cpu_labels[SI_COUNT] = {};

// ════════════════════════════════════════════════════════════════════
//  Settings state
// ════════════════════════════════════════════════════════════════════
uint8_t  cfg_volume     = 80;
uint8_t  cfg_brightness = 200;
uint8_t  cfg_sleep_min  = 5;
bool     cfg_dark_mode  = true;
uint8_t  cfg_fps        = 30;

// Local screen toggle state
bool     cfg_local_sensors = false;
bool     cfg_local_words   = true;
bool     cfg_local_speech  = false;

float    bat_voltage_v  = 4.2f;
int      bat_pct_cache  = 100;
AppMode  cur_gui_mode   = MODE_MENU;
int      test_active    = -1;

// Test name table — used for dynamic header title in test detail
const char *test_names[] = {
    "OLED", "MPU6050", "Flex Sensor", "Hall Effect", "Battery", "Speaker"
};

// ════════════════════════════════════════════════════════════════════
//  External callbacks
// ════════════════════════════════════════════════════════════════════
void (*s_mode_cb)(AppMode)        = nullptr;
void (*s_test_speaker_cb)()       = nullptr;
void (*s_test_oled_cb)()          = nullptr;
void (*s_brightness_cb)(uint8_t)  = nullptr;
void (*s_volume_cb)(uint8_t)      = nullptr;

// ════════════════════════════════════════════════════════════════════
//  NVS persistence
// ════════════════════════════════════════════════════════════════════
void load_settings() {
    Preferences prefs;
    prefs.begin("gui_cfg", true);
    cfg_brightness = prefs.getUChar("brt", 200);
    cfg_volume     = prefs.getUChar("vol", 80);
    cfg_sleep_min  = prefs.getUChar("slp", 5);
    cfg_dark_mode  = prefs.getBool("dark", true);
    cfg_fps        = prefs.getUChar("fps", 30);
    prefs.end();
}

void save_settings() {
    Preferences prefs;
    prefs.begin("gui_cfg", false);
    prefs.putUChar("brt", cfg_brightness);
    prefs.putUChar("vol", cfg_volume);
    prefs.putUChar("slp", cfg_sleep_min);
    prefs.putBool("dark", cfg_dark_mode);
    prefs.putUChar("fps", cfg_fps);
    prefs.end();
}

// ════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════
void gui_init() {
    load_settings();

    // Point to the correct palette before building anything
    tc = cfg_dark_mode ? &TC_DARK : &TC_LIGHT;

    init_styles();

    // Apply saved FPS
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        uint32_t period = (cfg_fps == 60) ? 16 : 33;
        lv_timer_set_period(disp->refr_timer, period);
    }

    build_splash();
    build_menu();
    build_predict_menu();
    build_train();
    build_local();
    build_web();
    build_settings();
    build_test();
    build_test_detail();

    lv_scr_load(scr_splash);
    lv_timer_create(cb_splash_timer, 2000, NULL);
}

void gui_set_mode(AppMode mode) {
    cur_gui_mode = mode;
    switch (mode) {
        case MODE_MENU:           lv_scr_load_anim(scr_menu,        LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_TRAIN:          lv_scr_load_anim(scr_train,       LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_LOCAL:  lv_scr_load_anim(scr_local,       LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_WEB:    lv_scr_load_anim(scr_web,         LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_SETTINGS:       lv_scr_load_anim(scr_settings,    LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_TEST:           lv_scr_load_anim(scr_test,        LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
    }
}

void gui_update(const SensorData &d) {
    if (cur_gui_mode == MODE_PREDICT_LOCAL && cfg_local_sensors) {
        for (int i = 0; i < 5; i++) {
            lv_bar_set_value(bar_flex[i], d.flex[i], LV_ANIM_OFF);
            lv_bar_set_value(bar_hall[i], d.hall[i], LV_ANIM_OFF);
        }
    }
}

void gui_test_update(const SensorData &d) {
    if (cur_gui_mode != MODE_TEST || !lbl_test_detail) return;
    char buf[200];
    switch (test_active) {
    case 0:
        lv_label_set_text(lbl_test_detail,
            "OLED Test\n\n" LV_SYMBOL_OK " Display OK\nNo artifacts detected.");
        break;
    case 1:
        snprintf(buf, sizeof(buf),
            "MPU6050 Live Data\n\n"
            "Accel: %.1f  %.1f  %.1f m/s2\n"
            "Gyro:  %.1f  %.1f  %.1f d/s\n"
            "Pitch: %.1f  Roll: %.1f",
            d.accel_x, d.accel_y, d.accel_z,
            d.gyro_x, d.gyro_y, d.gyro_z,
            d.pitch, d.roll);
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 2:
        snprintf(buf, sizeof(buf),
            "Flex Sensors (raw ADC)\n\n"
            "Thumb:  %u\nIndex:  %u\nMiddle: %u\nRing:   %u\nPinky:  %u",
            d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4]);
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 3:
        snprintf(buf, sizeof(buf),
            "Hall Sensors (raw ADC)\n\n"
            "Thumb:  %u\nIndex:  %u\nMiddle: %u\nRing:   %u\nPinky:  %u",
            d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4]);
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 4: {
        char vbuf[120];
        snprintf(vbuf, sizeof(vbuf),
            "Battery Test\n\n"
            LV_SYMBOL_BATTERY_FULL " %d%%\n"
            "Voltage: %.2fV\n\n"
            "Status: %s",
            bat_pct_cache, bat_voltage_v,
            bat_pct_cache > 20 ? "OK" : "LOW");
        lv_label_set_text(lbl_test_detail, vbuf);
        break;
    }
    case 5:
        lv_label_set_text(lbl_test_detail,
            "Speaker Test\n\n"
            LV_SYMBOL_OK " Tone played\n"
            "Adjust volume below.");
        break;
    default:
        break;
    }
}

void gui_set_gesture(const char *text) {
    if (lbl_gesture) lv_label_set_text(lbl_gesture, text);
}

void gui_set_battery(int pct) {
    bat_pct_cache = pct;
    bat_voltage_v = BAT_EMPTY_V + (pct / 100.0f) * (BAT_FULL_V - BAT_EMPTY_V);

    char buf[32];
    const char *icon;

    if      (pct > 80) icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 50) icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 25) icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 10) icon = LV_SYMBOL_BATTERY_1;
    else               icon = LV_SYMBOL_BATTERY_EMPTY;

    snprintf(buf, sizeof(buf), "%s %d%%", icon, pct);
    for (int i = 0; i < SI_COUNT; i++) {
        if (bat_labels[i])
            lv_label_set_text(bat_labels[i], buf);
    }

    // Update battery test detail if active
    if (cur_gui_mode == MODE_TEST && test_active == 4 && lbl_test_detail) {
        char vbuf[120];
        snprintf(vbuf, sizeof(vbuf),
            "Battery Test\n\n"
            "%s %d%%\n"
            "Voltage: %.2fV\n\n"
            "Status: %s",
            icon, pct, bat_voltage_v,
            pct > 20 ? "OK" : "LOW");
        lv_label_set_text(lbl_test_detail, vbuf);
    }
}

void gui_show_web_qr(const char *url) {
    if (qr_web && url)
        lv_qrcode_update(qr_web, url, strlen(url));
}

void gui_web_set_connected(bool connected) {
    if (connected == web_client_connected) return;
    web_client_connected = connected;

    if (connected) {
        // Client connected — show webpage QR, hide WiFi QR
        if (qr_wifi) lv_obj_add_flag(qr_wifi, LV_OBJ_FLAG_HIDDEN);
        if (qr_web)  lv_obj_clear_flag(qr_web, LV_OBJ_FLAG_HIDDEN);
        if (lbl_web_stat)
            lv_label_set_text(lbl_web_stat,
                "Client connected!\n"
                "Scan to open webpage\n"
                "http://192.168.4.1");
    } else {
        // No clients — show WiFi QR, hide webpage QR
        if (qr_wifi) lv_obj_clear_flag(qr_wifi, LV_OBJ_FLAG_HIDDEN);
        if (qr_web)  lv_obj_add_flag(qr_web, LV_OBJ_FLAG_HIDDEN);
        if (lbl_web_stat)
            lv_label_set_text(lbl_web_stat,
                "Scan to connect to WiFi\n"
                "SSID: " WIFI_AP_SSID "\n"
                "Pass: " WIFI_AP_PASS);
    }
}

void gui_set_train_status(const char *msg) {
    if (lbl_train_stat) lv_label_set_text(lbl_train_stat, msg);
}

void gui_set_volume(uint8_t vol)      { cfg_volume = vol; }
void gui_set_brightness(uint8_t brt)  { cfg_brightness = brt; }
uint8_t gui_get_volume()              { return cfg_volume; }
uint8_t gui_get_brightness()          { return cfg_brightness; }

// Local flag getters
bool gui_local_show_sensors()         { return cfg_local_sensors; }
bool gui_local_show_words()           { return cfg_local_words; }
bool gui_local_use_speech()           { return cfg_local_speech; }

void gui_set_cpu_usage(int pct) {
    char buf[16];
    snprintf(buf, sizeof(buf), "CPU %d%%", pct);
    for (int i = 0; i < SI_COUNT; i++) {
        if (cpu_labels[i])
            lv_label_set_text(cpu_labels[i], buf);
    }
}

void gui_update_about(const SystemInfoData &info) {
    if (!lbl_about) return;
    char buf[512];
    uint32_t h = info.uptime_sec / 3600;
    uint32_t m = (info.uptime_sec % 3600) / 60;
    uint32_t s = info.uptime_sec % 60;

    snprintf(buf, sizeof(buf),
        "Signa v4.0\n"
        "%s rev %d  |  %d core%s\n"
        "SDK: %s\n"
        "\n"
        "CPU: %lu MHz  |  %d%%\n"
        "Temp: %.1f C\n"
        "LVGL: %d FPS\n"
        "\n"
        "RAM: %lu / %lu KB (%d%%)\n"
        "PSRAM: %lu / %lu KB (%d%%)\n"
        "Flash: %lu KB @ %lu MHz\n"
        "\n"
        LV_SYMBOL_BATTERY_FULL " %d%%  |  %.2fV\n"
        "\n"
        "Uptime: %02lu:%02lu:%02lu",
        info.chip_model, info.chip_revision,
        info.cpu_cores, info.cpu_cores > 1 ? "s" : "",
        info.sdk_version,
        (unsigned long)info.cpu_freq_mhz, info.cpu_usage_pct,
        info.cpu_temp_c,
        info.lvgl_fps,
        (unsigned long)(info.ram_used/1024), (unsigned long)(info.ram_total/1024), info.ram_pct,
        (unsigned long)(info.psram_used/1024), (unsigned long)(info.psram_total/1024), info.psram_pct,
        (unsigned long)(info.flash_size/1024), (unsigned long)(info.flash_speed/1000000),
        bat_pct_cache, bat_voltage_v,
        (unsigned long)h, (unsigned long)m, (unsigned long)s);

    lv_label_set_text(lbl_about, buf);
}

// ════════════════════════════════════════════════════════════════════
//  Callback registration
// ════════════════════════════════════════════════════════════════════
void gui_register_mode_callback(void (*cb)(AppMode))   { s_mode_cb = cb; }
void gui_register_test_speaker_cb(void (*cb)())        { s_test_speaker_cb = cb; }
void gui_register_test_oled_cb(void (*cb)())           { s_test_oled_cb = cb; }
void gui_register_brightness_cb(void (*cb)(uint8_t))   { s_brightness_cb = cb; }
void gui_register_volume_cb(void (*cb)(uint8_t))       { s_volume_cb = cb; }
