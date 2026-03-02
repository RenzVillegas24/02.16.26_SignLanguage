/*
 * @file gui/gui_api.cpp
 * @brief Public GUI API + global state + NVS persistence
 *        Now includes cfg_accent, accent NVS, and emoji test_names[].
 */
#include "gui_internal.h"
#include "gui/gui.h"
#include "system_info.h"
#include "test_sensors_module.h"
#include "power.h"
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
lv_obj_t *scr_test_sensors = nullptr;
lv_obj_t *scr_test_detail  = nullptr;

// ════════════════════════════════════════════════════════════════════
//  Widget pointers
// ════════════════════════════════════════════════════════════════════
lv_obj_t *lbl_gesture      = nullptr;

lv_obj_t *bar_flex[5]      = {};
lv_obj_t *bar_hall[5]      = {};
lv_obj_t *bar_hall_top[5]  = {};
lv_obj_t *bars_container   = nullptr;

lv_obj_t *slider_brightness= nullptr;
lv_obj_t *slider_volume    = nullptr;
lv_obj_t *slider_sleep     = nullptr;
lv_obj_t *lbl_brt_val      = nullptr;
lv_obj_t *lbl_vol_val      = nullptr;
lv_obj_t *lbl_slp_val      = nullptr;

lv_obj_t *sw_dark_mode     = nullptr;
lv_obj_t *dd_fps           = nullptr;
lv_obj_t *dd_accent        = nullptr;

lv_obj_t *lbl_about        = nullptr;
lv_obj_t *lbl_train_stat   = nullptr;

lv_obj_t *qr_wifi           = nullptr;
lv_obj_t *qr_web            = nullptr;
lv_obj_t *lbl_web_stat      = nullptr;
bool      web_client_connected = false;

lv_obj_t *lbl_test_detail  = nullptr;
lv_obj_t *lbl_test_title   = nullptr;
lv_obj_t *test_vol_row     = nullptr;
lv_obj_t *slider_test_vol  = nullptr;
lv_obj_t *lbl_test_vol_val = nullptr;
lv_obj_t *test_brt_row     = nullptr;
lv_obj_t *slider_test_brt  = nullptr;
lv_obj_t *lbl_test_brt_val = nullptr;
lv_obj_t *btn_benchmark    = nullptr;

// Sensor test detail bars + labels
lv_obj_t *sensor_test_container = nullptr;
lv_obj_t *sensor_test_bars[5]   = {};
lv_obj_t *sensor_test_lbls[5]   = {};

// Calibration dialog widgets
lv_obj_t *calib_overlay    = nullptr;
lv_obj_t *calib_bar        = nullptr;
lv_obj_t *calib_lbl        = nullptr;
lv_obj_t *lbl_calib_info   = nullptr;
lv_obj_t *btn_calibrate    = nullptr;

// Speaker test panel widgets
lv_obj_t *spk_panel        = nullptr;
lv_obj_t *lbl_spk_step     = nullptr;
lv_obj_t *spk_prog_bar     = nullptr;
lv_obj_t *btn_spk_pause    = nullptr;
lv_obj_t *btn_spk_stop     = nullptr;

lv_obj_t *bat_label = nullptr;
lv_obj_t *charge_label = nullptr;
lv_obj_t *cpu_label = nullptr;
lv_obj_t *stat_bar  = nullptr;

// Power menu dialog widgets
lv_obj_t *power_overlay = nullptr;
lv_obj_t *power_dialog  = nullptr;

// Sleep warning dialog widgets
lv_obj_t *sleep_warn_overlay = nullptr;
lv_obj_t *sleep_warn_lbl     = nullptr;

// Lock screen widgets
lv_obj_t *lock_overlay  = nullptr;
lv_obj_t *lock_icon_lbl = nullptr;
lv_obj_t *lock_main_lbl = nullptr;
lv_obj_t *lock_bat_lbl  = nullptr;

// ════════════════════════════════════════════════════════════════════
//  Settings state
// ════════════════════════════════════════════════════════════════════
uint8_t  cfg_volume     = 80;
uint8_t  cfg_brightness = 200;
uint8_t  cfg_sleep_min  = 5;
bool     cfg_dark_mode  = true;
uint8_t  cfg_fps        = 30;
uint8_t  cfg_accent     = 0;        // accent colour index (0..NUM_ACCENTS-1)

bool     cfg_local_sensors = false;
bool     cfg_local_words   = true;
bool     cfg_local_speech  = false;
bool     cfg_back_gesture  = true;
bool     cfg_lock_screen_on = true;  // always-on lock screen for Train/Predict modes

float    bat_voltage_v  = 4.2f;
int      bat_pct_cache  = 100;
AppMode  cur_gui_mode   = MODE_MENU;
int      test_active    = -1;

// Test names (without emoji/symbol prefixes — headers will show plain text)
const char *test_names[] = {
    "OLED",
    "MPU6050",
    "Flex Sensor",
    "Hall Effect",
    "Hall Top",
    "Battery",
    "Speaker"
};

// ════════════════════════════════════════════════════════════════════
//  External callbacks
// ════════════════════════════════════════════════════════════════════
void (*s_mode_cb)(AppMode)        = nullptr;
void (*s_test_speaker_cb)()       = nullptr;
void (*s_test_oled_cb)()          = nullptr;
void (*s_brightness_cb)(uint8_t)  = nullptr;
void (*s_volume_cb)(uint8_t)      = nullptr;
void (*s_power_cb)(PowerAction)   = nullptr;

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
    cfg_accent     = prefs.getUChar("acnt", 0);
    if (cfg_accent >= NUM_ACCENTS) cfg_accent = 0;
    cfg_back_gesture = prefs.getBool("bgest", true);
    cfg_lock_screen_on = prefs.getBool("lck", true);
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
    prefs.putUChar("acnt", cfg_accent);
    prefs.putBool("bgest", cfg_back_gesture);
    prefs.putBool("lck",   cfg_lock_screen_on);
    prefs.end();
}

// ════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════
void gui_init() {
    load_settings();

    tc = cfg_dark_mode ? &TC_DARK : &TC_LIGHT;

    init_styles();

    // Apply saved FPS
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        uint32_t period = (cfg_fps == 60) ? 16 : 33;
        lv_timer_set_period(disp->refr_timer, period);
    }

    build_splash();
    build_status_bar();
    build_menu();
    build_predict_menu();
    build_train();
    build_local();
    build_web();
    build_settings();
    build_test();
    build_test_sensors();
    build_test_detail();
    build_power_menu();
    build_sleep_warning();
    build_lock_screen();

    lv_scr_load(scr_splash);
    if (stat_bar) lv_obj_add_flag(stat_bar, LV_OBJ_FLAG_HIDDEN);

    // Shorter splash when waking from deep sleep (user pressed button — respond fast)
    uint32_t splash_ms = power_is_deep_sleep_wake() ? 500 : 2000;
    lv_timer_create(cb_splash_timer, splash_ms, NULL);
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
            lv_bar_set_value(bar_hall_top[i], d.hall_top[i], LV_ANIM_OFF);
        }
    }
}

void gui_test_update(const SensorData &d, const ProcessedSensorData &pd) {
    // Only update the detail screen when it is actually visible
    if (cur_gui_mode != MODE_TEST) return;
    if (lv_scr_act() != scr_test_detail) return;
    if (!lbl_test_detail) return;
    char buf[320];
    switch (test_active) {
    case 0:
        lv_label_set_text(lbl_test_detail,
            "OLED Test\n\n" LV_SYMBOL_OK " Display OK\nNo artifacts detected.");
        break;
    case 1:
        test_sensors_format_mpu(pd, buf, sizeof(buf));
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 2:
        // Flex sensor test — update bars + labels with live data (including RAW)
        if (sensor_test_container && !lv_obj_has_flag(sensor_test_container, LV_OBJ_FLAG_HIDDEN)) {
            for (int i = 0; i < 5; i++) {
                lv_bar_set_value(sensor_test_bars[i], pd.flex_pct[i], LV_ANIM_OFF);
                lv_label_set_text_fmt(sensor_test_lbls[i], "Flex %d: %+d%% (R:%d)",
                                      i + 1, pd.flex_pct[i], pd.flex_raw[i]);
            }
        }
        break;
    case 3:
        // Hall effect side test — update bars + labels (including RAW)
        if (sensor_test_container && !lv_obj_has_flag(sensor_test_container, LV_OBJ_FLAG_HIDDEN)) {
            for (int i = 0; i < 5; i++) {
                lv_bar_set_value(sensor_test_bars[i], pd.hall_pct[i], LV_ANIM_OFF);
                lv_label_set_text_fmt(sensor_test_lbls[i], "Hall %d: %+d%% (R:%d)",
                                      i + 1, pd.hall_pct[i], pd.hall_raw[i]);
            }
        }
        break;
    case 4:
        // Hall effect top test — update bars + labels (including RAW)
        if (sensor_test_container && !lv_obj_has_flag(sensor_test_container, LV_OBJ_FLAG_HIDDEN)) {
            for (int i = 0; i < 5; i++) {
                lv_bar_set_value(sensor_test_bars[i], pd.hall_top_pct[i], LV_ANIM_OFF);
                lv_label_set_text_fmt(sensor_test_lbls[i], "HTop %d: %+d%% (R:%d)",
                                      i + 1, pd.hall_top_pct[i], pd.hall_top_raw[i]);
            }
        }
        break;
    case 5: {
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
    case 6:
        // Speaker test UI is updated via the poll timer in gui_callbacks.cpp
        // gui_test_update is called from the loop — just refresh the panel here
        refresh_spk_panel();
        break;
    default:
        break;
    }

    // Dump sensor data to Serial only for sensor/IMU tests
    if (test_active >= 1 && test_active <= 4) {
        static uint32_t last_serial = 0;
        if (millis() - last_serial >= 500) {
            last_serial = millis();
            sensor_module_print_serial(pd);
        }
    }
}

void gui_set_gesture(const char *text) {
    if (lbl_gesture) lv_label_set_text(lbl_gesture, text);
}

void gui_set_battery(int pct) {
    bat_pct_cache = pct;
    // Use the accurate SY6970 voltage when available
    float real_v  = power_battery_voltage();   // SY6970 mV → V (or ADC fallback)
    bat_voltage_v = (real_v > 0.1f) ? real_v
                  : BAT_EMPTY_V + (pct / 100.0f) * (BAT_FULL_V - BAT_EMPTY_V);

    char buf[32];
    const char *icon;

    if      (pct > 80) icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 50) icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 25) icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 10) icon = LV_SYMBOL_BATTERY_1;
    else               icon = LV_SYMBOL_BATTERY_EMPTY;

    snprintf(buf, sizeof(buf), "%s %d%%", icon, pct);
    if (bat_label)
        lv_label_set_text(bat_label, buf);

    if (cur_gui_mode == MODE_TEST && test_active == 5 && lbl_test_detail) {
        int input_mv  = power_input_voltage_mv();
        int charge_ma = power_charging_current_ma();
        char vbuf[200];
        snprintf(vbuf, sizeof(vbuf),
            "Battery Test\n\n"
            "%s %d%%\n"
            "Voltage: %.3f V\n"
            "Input:   %d mV\n"
            "Current: %d mA\n\n"
            "Charging: %s\n"
            "Status: %s",
            icon, pct,
            bat_voltage_v,
            input_mv,
            charge_ma,
            (power_is_charging() || power_usb_connected()) ? "Yes" : "No",
            power_charging_status_str());
        lv_label_set_text(lbl_test_detail, vbuf);
    }
}

void gui_set_charging(bool charging) {
    if (!charge_label) return;
    if (charging)
        lv_obj_clear_flag(charge_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(charge_label, LV_OBJ_FLAG_HIDDEN);
}

void gui_show_web_qr(const char *url) {
    if (qr_web && url)
        lv_qrcode_update(qr_web, url, strlen(url));
}

void gui_web_set_connected(bool connected) {
    if (connected == web_client_connected) return;
    web_client_connected = connected;

    if (connected) {
        if (qr_wifi) lv_obj_add_flag(qr_wifi, LV_OBJ_FLAG_HIDDEN);
        if (qr_web)  lv_obj_clear_flag(qr_web, LV_OBJ_FLAG_HIDDEN);
        if (lbl_web_stat)
            lv_label_set_text(lbl_web_stat,
                "Client connected!\n"
                "Scan to open webpage\n"
                "http://192.168.4.1");
    } else {
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
uint8_t gui_get_sleep_min()           { return cfg_sleep_min; }
bool    gui_get_lock_screen_on()      { return cfg_lock_screen_on; }

bool gui_local_show_sensors()         { return cfg_local_sensors; }
bool gui_local_show_words()           { return cfg_local_words; }
bool gui_local_use_speech()           { return cfg_local_speech; }

void gui_set_cpu_usage(int pct) {
    char buf[16];
    snprintf(buf, sizeof(buf), "CPU %d%%", pct);
    if (cpu_label)
        lv_label_set_text(cpu_label, buf);
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
void gui_register_power_cb(void (*cb)(PowerAction))    { s_power_cb = cb; }

// ════════════════════════════════════════════════════════════════════
//  Power Menu
// ════════════════════════════════════════════════════════════════════
void gui_show_power_menu() {
    if (power_overlay) lv_obj_clear_flag(power_overlay, LV_OBJ_FLAG_HIDDEN);
}

void gui_hide_power_menu() {
    if (power_overlay) lv_obj_add_flag(power_overlay, LV_OBJ_FLAG_HIDDEN);
}

bool gui_power_menu_visible() {
    return power_overlay && !lv_obj_has_flag(power_overlay, LV_OBJ_FLAG_HIDDEN);
}

// ════════════════════════════════════════════════════════════════════
//  Sleep Warning Dialog
// ════════════════════════════════════════════════════════════════════
void gui_show_sleep_warning(int seconds_left) {
    if (!sleep_warn_overlay) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Sleeping in %d s\nTouch to cancel", seconds_left);
    if (sleep_warn_lbl) lv_label_set_text(sleep_warn_lbl, buf);
    lv_obj_clear_flag(sleep_warn_overlay, LV_OBJ_FLAG_HIDDEN);
}

void gui_hide_sleep_warning() {
    if (sleep_warn_overlay) lv_obj_add_flag(sleep_warn_overlay, LV_OBJ_FLAG_HIDDEN);
}

bool gui_sleep_warning_visible() {
    return sleep_warn_overlay && !lv_obj_has_flag(sleep_warn_overlay, LV_OBJ_FLAG_HIDDEN);
}

// ════════════════════════════════════════════════════════════════════
//  Lock Screen (always-on for Train / Predict modes)
// ════════════════════════════════════════════════════════════════════
void gui_show_lock_screen(AppMode mode) {
    if (!lock_overlay) return;
    // Set text based on mode
    if (lock_main_lbl) {
        if (mode == MODE_TRAIN) {
            lv_label_set_text(lock_main_lbl, "Training");
        } else {
            lv_label_set_text(lock_main_lbl, "Predictions:\n---");
        }
    }
    // Hide status bar so lock screen is truly full-screen
    if (stat_bar) lv_obj_add_flag(stat_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
    // Move lock overlay to front of lv_layer_top
    lv_obj_move_foreground(lock_overlay);
}

void gui_hide_lock_screen() {
    if (lock_overlay) lv_obj_add_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
    // Restore status bar
    if (stat_bar) lv_obj_clear_flag(stat_bar, LV_OBJ_FLAG_HIDDEN);
}

bool gui_lock_screen_visible() {
    return lock_overlay && !lv_obj_has_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
}

void gui_lock_update_gesture(const char *text) {
    if (!lock_main_lbl || !gui_lock_screen_visible()) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Predictions:\n%s", text);
    lv_label_set_text(lock_main_lbl, buf);
}

void gui_lock_update_battery(int pct) {
    if (!lock_bat_lbl || !gui_lock_screen_visible()) return;
    const char *icon;
    if      (pct > 80) icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 50) icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 25) icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 10) icon = LV_SYMBOL_BATTERY_1;
    else               icon = LV_SYMBOL_BATTERY_EMPTY;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d%%", icon, pct);
    lv_label_set_text(lock_bat_lbl, buf);
}

// ════════════════════════════════════════════════════════════════════
//  Calibration UI
// ════════════════════════════════════════════════════════════════════
static bool s_calibrating = false;

void gui_update_calibration_progress(int pct) {
    update_calibration_progress(pct);
    if (pct >= 100) {
        s_calibrating = false;
        hide_calibration_dialog();
        refresh_calib_info_label();
    } else {
        s_calibrating = true;
    }
}

bool gui_is_calibrating() {
    return s_calibrating;
}

void refresh_calib_info_label() {
    if (!lbl_calib_info) return;

    if (!sensor_module_is_calibrated()) {
        lv_label_set_text(lbl_calib_info,
            LV_SYMBOL_WARNING " Not calibrated.\n"
            "Tap 'Calibrate' to begin.\n"
            "Keep hand flat, no magnets.");
        return;
    }

    // Fetch actual calibration data
    FlexCalibInfo fc[NUM_FLEX_SENSORS];
    HallCalibInfo hc[NUM_HALL_SENSORS];
    HallCalibInfo htc[NUM_HALL_TOP_SENSORS];
    sensor_module_get_flex_cal(fc);
    sensor_module_get_hall_cal(hc);
    sensor_module_get_hall_top_cal(htc);

    static const char *fn[] = {"Th","Ix","Mi","Ri","Pi"};
    char buf[480];
    int off = 0;

    off += snprintf(buf + off, sizeof(buf) - off,
                    LV_SYMBOL_OK " Calibrated & saved!\n\n");

    off += snprintf(buf + off, sizeof(buf) - off, "Flex (flat / +up / -dn):\n");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        " %s: %d / +%d / -%d\n",
                        fn[i], fc[i].flat_value,
                        fc[i].upward_range, fc[i].downward_range);
    }

    off += snprintf(buf + off, sizeof(buf) - off, "\nHall (norm / +front / -back):\n");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        " %s: %d / +%d / -%d\n",
                        fn[i], hc[i].normal,
                        hc[i].front_range, hc[i].back_range);
    }

    off += snprintf(buf + off, sizeof(buf) - off, "\nHall Top (norm / +front / -back):\n");
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        " %s: %d / +%d / -%d\n",
                        fn[i], htc[i].normal,
                        htc[i].front_range, htc[i].back_range);
    }

    lv_label_set_text(lbl_calib_info, buf);
}

// ════════════════════════════════════════════════════════════════════
//  Speaker panel refresh — called from gui_test_update (case 6)
//  and from the spk poll timer.
//  Reads extern volatile state exposed by gui_callbacks.cpp.
// ════════════════════════════════════════════════════════════════════
extern volatile int   spk_step_idx;   // 0-based current step
extern volatile int   spk_num_steps;  // total steps (9)
extern volatile bool  spk_paused;
extern volatile bool  spk_running;
extern volatile bool  spk_done_flag;
extern const char    *spk_step_names[];

void refresh_spk_panel() {
    if (!spk_panel) return;

    int step  = spk_step_idx;
    int total = spk_num_steps;
    bool paused  = spk_paused;
    bool running = spk_running;
    bool done    = spk_done_flag;

    // Full row width and half-width for when both buttons are visible
    const int row_w = BTN_W - 24;
    const int half  = (row_w - 8) / 2;

    // Progress bar
    if (spk_prog_bar)
        lv_bar_set_value(spk_prog_bar, step, LV_ANIM_OFF);

    // Step label
    if (lbl_spk_step) {
        if (done) {
            lv_label_set_text(lbl_spk_step, LV_SYMBOL_OK " All tests complete!");
        } else if (!running && step == 0) {
            lv_label_set_text(lbl_spk_step, "Tap Play to start...");
        } else if (!running && step > 0) {
            // Stopped mid-way
            char buf[64];
            snprintf(buf, sizeof(buf), "Stopped at %d/%d. Tap Play to restart.", step, total);
            lv_label_set_text(lbl_spk_step, buf);
        } else if (paused) {
            char buf[64];
            snprintf(buf, sizeof(buf), LV_SYMBOL_PAUSE " %d/%d: %s (paused)",
                     step + 1, total, spk_step_names[step]);
            lv_label_set_text(lbl_spk_step, buf);
        } else if (running && step < total) {
            char buf[64];
            snprintf(buf, sizeof(buf), LV_SYMBOL_PLAY " %d/%d: %s",
                     step + 1, total, spk_step_names[step]);
            lv_label_set_text(lbl_spk_step, buf);
        }
    }

    // ── Play / Pause / Resume / Restart button ──────────────────────
    // Always enabled (idle = ready to play, done = ready to restart).
    // Width: full row when Stop is hidden, half when Stop is visible.
    if (btn_spk_pause) {
        lv_obj_t *lbl = lv_obj_get_child(btn_spk_pause, 0);
        lv_obj_clear_state(btn_spk_pause, LV_STATE_DISABLED);

        if (!running) {
            // Idle or finished — show Play (full width, Stop hidden)
            if (lbl) lv_label_set_text(lbl,
                done ? LV_SYMBOL_PLAY " Play Again" : LV_SYMBOL_PLAY " Play");
            lv_obj_set_width(btn_spk_pause, row_w);
        } else if (paused) {
            if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY " Resume");
            lv_obj_set_width(btn_spk_pause, half);
        } else {
            if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE " Pause");
            lv_obj_set_width(btn_spk_pause, half);
        }
    }

    // ── Stop button — only visible while running ────────────────────
    if (btn_spk_stop) {
        if (running && !done) {
            lv_obj_clear_flag(btn_spk_stop, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(btn_spk_stop, LV_STATE_DISABLED);
        } else {
            lv_obj_add_flag(btn_spk_stop, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
