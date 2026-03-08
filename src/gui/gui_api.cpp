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
lv_obj_t *lbl_predict_conf   = nullptr;
lv_obj_t *lbl_predict_status = nullptr;

lv_obj_t *bar_flex[5]      = {};
lv_obj_t *bar_hall[5]      = {};
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
lv_obj_t *dd_local_voice   = nullptr;

lv_obj_t *lbl_about        = nullptr;
lv_obj_t *lbl_train_stat   = nullptr;

// Train screen live sensor widgets
lv_obj_t *train_bar_flex[5]  = {};
lv_obj_t *train_lbl_flex[5]  = {};
lv_obj_t *train_bar_hall[5]  = {};
lv_obj_t *train_lbl_hall[5]  = {};
lv_obj_t *train_lbl_imu      = nullptr;
lv_obj_t *train_lbl_counter  = nullptr;

// Predict Local screen live sensor widgets (same style as train)
lv_obj_t *local_bar_flex[5]  = {};
lv_obj_t *local_lbl_flex[5]  = {};
lv_obj_t *local_bar_hall[5]  = {};
lv_obj_t *local_lbl_hall[5]  = {};
lv_obj_t *local_lbl_imu      = nullptr;

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
lv_obj_t *calib_overlay        = nullptr;
lv_obj_t *calib_bar            = nullptr;
lv_obj_t *calib_lbl            = nullptr;
lv_obj_t *calib_dialog         = nullptr;
lv_obj_t *calib_btn_continue   = nullptr;
lv_obj_t *calib_btn_cancel     = nullptr;
lv_obj_t *calib_phase_lbl      = nullptr;
lv_obj_t *lbl_calib_info       = nullptr;
lv_obj_t *btn_calibrate        = nullptr;

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

// Charging popup overlay widgets
lv_obj_t *charge_popup_overlay = nullptr;
lv_obj_t *charge_popup_icon    = nullptr;
lv_obj_t *charge_popup_pct     = nullptr;
lv_obj_t *charge_popup_status  = nullptr;
lv_timer_t *charge_popup_timer = nullptr;

// ════════════════════════════════════════════════════════════════════
//  Settings state
// ════════════════════════════════════════════════════════════════════
uint8_t  cfg_volume     = 80;
uint8_t  cfg_brightness = 200;
uint8_t  cfg_sleep_min  = 5;
bool     cfg_dark_mode  = true;
uint8_t  cfg_fps        = 30;
uint8_t  cfg_accent     = 0;        // accent colour index (0..NUM_ACCENTS-1)

bool     cfg_local_sensors = true;
bool     cfg_local_words   = true;
bool     cfg_local_speech  = true;
uint8_t  cfg_local_voice   = 0;   // 0 = Boy, 1 = Girl
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
    build_charge_popup();

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
    // Legacy raw-ADC bars — kept for any future use but predict local now uses
    // gui_local_sensor_update() with processed percentage data.
    (void)d;
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
        // Dynamic colour: green for positive (up), red for negative (down)
        if (sensor_test_container && !lv_obj_has_flag(sensor_test_container, LV_OBJ_FLAG_HIDDEN)) {
            const lv_color_t col_green = lv_color_make(0x00, 0xE6, 0x76);
            const lv_color_t col_red   = lv_color_make(0xFF, 0x44, 0x44);
            for (int i = 0; i < 5; i++) {
                int8_t pct = pd.flex_pct[i];
                lv_bar_set_value(sensor_test_bars[i], pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(sensor_test_bars[i],
                    pct >= 0 ? col_green : col_red, LV_PART_INDICATOR);
                lv_label_set_text_fmt(sensor_test_lbls[i], "Flex %d: %+d%% (R:%d)",
                                      i + 1, pct, pd.flex_raw[i]);
            }
        }
        break;
    case 3:
        // Hall effect side test — update bars + labels (including RAW)
        // Dynamic colour: cyan for positive (front), red for negative (back)
        if (sensor_test_container && !lv_obj_has_flag(sensor_test_container, LV_OBJ_FLAG_HIDDEN)) {
            const lv_color_t col_cyan  = lv_color_make(0x00, 0xBB, 0xFF);
            const lv_color_t col_red   = lv_color_make(0xFF, 0x44, 0x44);
            for (int i = 0; i < 5; i++) {
                int8_t pct = pd.hall_pct[i];
                lv_bar_set_value(sensor_test_bars[i], pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(sensor_test_bars[i],
                    pct >= 0 ? col_cyan : col_red, LV_PART_INDICATOR);
                lv_label_set_text_fmt(sensor_test_lbls[i], "Hall %d: %+d%% (R:%d)",
                                      i + 1, pct, pd.hall_raw[i]);
            }
        }
        break;
    case 4: {
        // Comprehensive SY6970 battery info (updated every 100ms)
        PowerInfo pi = power_get_info();
        const char *bicon;
        if      (pi.battery_pct > 80) bicon = LV_SYMBOL_BATTERY_FULL;
        else if (pi.battery_pct > 50) bicon = LV_SYMBOL_BATTERY_3;
        else if (pi.battery_pct > 25) bicon = LV_SYMBOL_BATTERY_2;
        else if (pi.battery_pct > 10) bicon = LV_SYMBOL_BATTERY_1;
        else                          bicon = LV_SYMBOL_BATTERY_EMPTY;
        char vbuf[480];
        snprintf(vbuf, sizeof(vbuf),
            "Battery Test\n\n"
            "%s %d%%\n"
            "Battery:  %d mV\n"
            "System:   %d mV\n"
            "Input:    %d mV\n"
            "Current:  %d mA\n"
            "NTC:      %.1f%%\n\n"
            "Status:   %s\n"
            "Bus:      %s\n"
            "USB:      %s\n\n"
            "ADC raw:  %d\n"
            "ADC volt: %d mV\n\n"
            "Faults:\n"
            "  Chg: %s\n"
            "  Bat: %s\n"
            "  NTC: %s",
            bicon, pi.battery_pct,
            pi.battery_mv,
            pi.system_mv,
            pi.input_mv,
            pi.charge_ma,
            pi.ntc_pct,
            pi.charge_status,
            pi.bus_status,
            pi.bus_connection,
            pi.adc_raw, pi.adc_mv,
            pi.charging_fault,
            pi.battery_fault,
            pi.ntc_fault);
        lv_label_set_text(lbl_test_detail, vbuf);
        break;
    }
    case 5:
        // Speaker test UI is updated via the poll timer in gui_callbacks.cpp
        // gui_test_update is called from the loop — just refresh the panel here
        refresh_spk_panel();
        break;
    default:
        break;
    }

}

void gui_set_gesture(const char *text) {
    if (lbl_gesture) lv_label_set_text(lbl_gesture, text);
}

void gui_set_predict_confidence(float conf) {
    if (!lbl_predict_conf) return;
    if (conf > 0.01f) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Confidence: %d%%", (int)(conf * 100));
        lv_label_set_text(lbl_predict_conf, buf);
    } else {
        lv_label_set_text(lbl_predict_conf, "");
    }
}

void gui_set_predict_status(const char *text) {
    if (lbl_predict_status) lv_label_set_text(lbl_predict_status, text);
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

void gui_train_update(const SensorData &d, const ProcessedSensorData &pd, uint32_t sample_count) {
    if (cur_gui_mode != MODE_TRAIN) return;

    // Colour constants matching the Test_EI_Training style
    const lv_color_t col_green = lv_color_make(0x00, 0xE6, 0x76);
    const lv_color_t col_red   = lv_color_make(0xFF, 0x44, 0x44);
    const lv_color_t col_cyan  = lv_color_make(0x00, 0xBB, 0xFF);

    // Flex bars — green for positive, red for negative
    for (int i = 0; i < 5; i++) {
        if (train_bar_flex[i]) {
            int8_t pct = pd.flex_pct[i];
            lv_bar_set_value(train_bar_flex[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(train_bar_flex[i],
                pct >= 0 ? col_green : col_red, LV_PART_INDICATOR);
        }
        if (train_lbl_flex[i]) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pd.flex_pct[i]);
            lv_label_set_text(train_lbl_flex[i], tmp);
        }
    }

    // Hall bars — cyan for positive, red for negative
    for (int i = 0; i < 5; i++) {
        if (train_bar_hall[i]) {
            int8_t pct = pd.hall_pct[i];
            lv_bar_set_value(train_bar_hall[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(train_bar_hall[i],
                pct >= 0 ? col_cyan : col_red, LV_PART_INDICATOR);
        }
        if (train_lbl_hall[i]) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pd.hall_pct[i]);
            lv_label_set_text(train_lbl_hall[i], tmp);
        }
    }

    // IMU text
    if (train_lbl_imu) {
        char imu_buf[160];
        snprintf(imu_buf, sizeof(imu_buf),
            "Ax:%6.2f  Ay:%6.2f  Az:%6.2f\n"
            "Gx:%6.1f  Gy:%6.1f  Gz:%6.1f\n"
            "Pitch:%5.1f   Roll:%5.1f",
            pd.accel_x, pd.accel_y, pd.accel_z,
            pd.gyro_x,  pd.gyro_y,  pd.gyro_z,
            pd.pitch,   pd.roll);
        lv_label_set_text(train_lbl_imu, imu_buf);
    }

    // Sample counter
    if (train_lbl_counter) {
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "Samples: %lu", (unsigned long)sample_count);
        lv_label_set_text(train_lbl_counter, cnt);
    }
}

void gui_local_sensor_update(const ProcessedSensorData &pd) {
    if (!cfg_local_sensors) return;

    const lv_color_t col_green = lv_color_make(0x00, 0xE6, 0x76);
    const lv_color_t col_red   = lv_color_make(0xFF, 0x44, 0x44);
    const lv_color_t col_cyan  = lv_color_make(0x00, 0xBB, 0xFF);

    // Flex bars — green for positive (bend up), red for negative (bend down)
    for (int i = 0; i < 5; i++) {
        if (local_bar_flex[i]) {
            int8_t pct = pd.flex_pct[i];
            lv_bar_set_value(local_bar_flex[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(local_bar_flex[i],
                pct >= 0 ? col_green : col_red, LV_PART_INDICATOR);
        }
        if (local_lbl_flex[i]) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pd.flex_pct[i]);
            lv_label_set_text(local_lbl_flex[i], tmp);
        }
    }

    // Hall bars — cyan for positive (front), red for negative (back)
    for (int i = 0; i < 5; i++) {
        if (local_bar_hall[i]) {
            int8_t pct = pd.hall_pct[i];
            lv_bar_set_value(local_bar_hall[i], pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(local_bar_hall[i],
                pct >= 0 ? col_cyan : col_red, LV_PART_INDICATOR);
        }
        if (local_lbl_hall[i]) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pd.hall_pct[i]);
            lv_label_set_text(local_lbl_hall[i], tmp);
        }
    }

    // IMU
    if (local_lbl_imu) {
        char imu_buf[160];
        snprintf(imu_buf, sizeof(imu_buf),
            "Ax:%6.2f  Ay:%6.2f  Az:%6.2f\n"
            "Gx:%6.1f  Gy:%6.1f  Gz:%6.1f\n"
            "Pitch:%5.1f   Roll:%5.1f",
            pd.accel_x, pd.accel_y, pd.accel_z,
            pd.gyro_x,  pd.gyro_y,  pd.gyro_z,
            pd.pitch,   pd.roll);
        lv_label_set_text(local_lbl_imu, imu_buf);
    }
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
const char *gui_local_voice_dir()     { return cfg_local_voice == 0 ? "boy" : "girl"; }

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

// ════════════════════════════════════════════════════════════════
//  Charging popup overlay (5-second auto-dismiss)
// ════════════════════════════════════════════════════════════════
static void charge_popup_timer_cb(lv_timer_t *t) {
    (void)t;
    gui_hide_charge_popup();
}

void gui_show_charge_popup(bool charging, int pct) {
    if (!charge_popup_overlay) return;

    // ── Update icon ──
    const char *icon;
    if      (pct > 80) icon = LV_SYMBOL_BATTERY_FULL;
    else if (pct > 50) icon = LV_SYMBOL_BATTERY_3;
    else if (pct > 25) icon = LV_SYMBOL_BATTERY_2;
    else if (pct > 10) icon = LV_SYMBOL_BATTERY_1;
    else               icon = LV_SYMBOL_BATTERY_EMPTY;

    if (charge_popup_icon) {
        if (charging)
            lv_label_set_text(charge_popup_icon, LV_SYMBOL_CHARGE);
        else
            lv_label_set_text(charge_popup_icon, icon);
    }

    // ── Update percentage ──
    if (charge_popup_pct) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(charge_popup_pct, buf);
    }

    // ── Update status text ──
    if (charge_popup_status) {
        if (charging)
            lv_label_set_text(charge_popup_status, "Charging");
        else
            lv_label_set_text(charge_popup_status, "Unplugged");
    }

    // ── Update colour ──
    lv_color_t clr = charging ? lv_color_make(0x4C, 0xAF, 0x50)    // green
                              : lv_color_make(0xFF, 0x57, 0x22);   // deep orange
    if (charge_popup_icon)
        lv_obj_set_style_text_color(charge_popup_icon, clr, 0);
    if (charge_popup_pct)
        lv_obj_set_style_text_color(charge_popup_pct, clr, 0);

    // ── Show overlay (topmost) ──
    lv_obj_clear_flag(charge_popup_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(charge_popup_overlay);

    // ── (Re)start 5-second auto-dismiss timer ──
    if (charge_popup_timer) {
        lv_timer_reset(charge_popup_timer);
        lv_timer_resume(charge_popup_timer);
    } else {
        charge_popup_timer = lv_timer_create(charge_popup_timer_cb, 5000, NULL);
        lv_timer_set_repeat_count(charge_popup_timer, 1);
    }
}

void gui_hide_charge_popup() {
    if (charge_popup_overlay)
        lv_obj_add_flag(charge_popup_overlay, LV_OBJ_FLAG_HIDDEN);
    if (charge_popup_timer) {
        lv_timer_del(charge_popup_timer);
        charge_popup_timer = nullptr;
    }
}

bool gui_charge_popup_visible() {
    return charge_popup_overlay
        && !lv_obj_has_flag(charge_popup_overlay, LV_OBJ_FLAG_HIDDEN);
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
    sensor_module_get_flex_cal(fc);
    sensor_module_get_hall_cal(hc);

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
