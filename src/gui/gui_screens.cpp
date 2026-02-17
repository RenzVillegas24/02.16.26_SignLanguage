/*
 * @file gui/gui_screens.cpp
 * @brief Screen builders (build_splash, build_menu, …)
 *        Now with back-gesture support, accent colours, and emoji test titles.
 */
#include "gui_internal.h"

// ════════════════════════════════════════════════════════════════════
//  Splash
// ════════════════════════════════════════════════════════════════════
void build_splash() {
    scr_splash = mk_scr();

    lv_obj_t *title = lv_label_create(scr_splash);
    lv_label_set_text(title, "Signa");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(scr_splash);
    lv_label_set_text(sub, "Sign Language Translator");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, accent_primary(), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);
}

// ════════════════════════════════════════════════════════════════════
//  Main menu  — navigation-style buttons
// ════════════════════════════════════════════════════════════════════
void build_menu() {
    scr_menu = mk_scr();
    int hh = mk_header(scr_menu, "Signa Menu", NULL);

    lv_obj_t *cont = mk_content(scr_menu, hh);

    mk_nav_btn(cont, LV_SYMBOL_UPLOAD     " Train",    cb_btn_train);
    mk_nav_btn(cont, LV_SYMBOL_EYE_OPEN   " Predict",  cb_btn_predict);
    mk_nav_btn(cont, LV_SYMBOL_SETTINGS   " Settings", cb_btn_settings);
}

// ════════════════════════════════════════════════════════════════════
//  Predict sub-menu  — 2 buttons: Local & Web
// ════════════════════════════════════════════════════════════════════
void build_predict_menu() {
    scr_predict = mk_scr();
    int hh = mk_header(scr_predict, "Predict", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_predict, hh);

    mk_nav_btn(cont, LV_SYMBOL_HOME   " Local",  cb_btn_local);
    mk_nav_btn(cont, LV_SYMBOL_WIFI   " Web",    cb_btn_web);

    add_back_gesture(scr_predict, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Train
// ════════════════════════════════════════════════════════════════════
void build_train() {
    scr_train = mk_scr();
    int hh = mk_header(scr_train, "Train", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_train, hh);

    lv_obj_t *info = lv_label_create(cont);
    lv_label_set_text(info,
        "Connect Edge Impulse\n"
        "Data Forwarder via USB.\n\n"
        "Sensor data streams over\n"
        "Serial at 115200 baud.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, tc->sub_text, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info, BTN_W);

    lbl_train_stat = lv_label_create(cont);
    lv_label_set_text(lbl_train_stat, "Status: Streaming...");
    lv_obj_set_style_text_font(lbl_train_stat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_train_stat, lv_color_make(0x66,0xFF,0x66), 0);

    add_back_gesture(scr_train, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Local  — unified local prediction with toggle options
// ════════════════════════════════════════════════════════════════════
void build_local() {
    scr_local = mk_scr();
    int hh = mk_header(scr_local, "Local", cb_btn_back_predict);

    lv_obj_t *cont = mk_content(scr_local, hh);

    // ── Gesture label (large, centred) ──
    lbl_gesture = lv_label_create(cont);
    lv_label_set_text(lbl_gesture, "---");
    lv_obj_set_style_text_font(lbl_gesture, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture, lv_color_make(0x00,0xFF,0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture, BTN_W);
    if (!cfg_local_words) lv_obj_add_flag(lbl_gesture, LV_OBJ_FLAG_HIDDEN);

    // ── Toggle options ──
    mk_section(cont, "OPTIONS");
    add_switch_row(cont, LV_SYMBOL_LIST,       "Show Sensors",  cfg_local_sensors, cb_local_sensors);
    add_switch_row(cont, LV_SYMBOL_EDIT,       "Show Words",    cfg_local_words,   cb_local_words);
    add_switch_row(cont, LV_SYMBOL_VOLUME_MAX, "Use Speech",    cfg_local_speech,  cb_local_speech);

    // ── Sensor bars container ──
    bars_container = lv_obj_create(cont);
    lv_obj_set_size(bars_container, BTN_W, 10 * 20 + 10);
    lv_obj_set_style_bg_opa(bars_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bars_container, 0, 0);
    lv_obj_set_style_pad_all(bars_container, 0, 0);
    lv_obj_clear_flag(bars_container, LV_OBJ_FLAG_SCROLLABLE);
    create_bars(bars_container, bar_flex, bar_hall, 0);
    if (!cfg_local_sensors) lv_obj_add_flag(bars_container, LV_OBJ_FLAG_HIDDEN);

    add_back_gesture(scr_local, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Web  — WiFi QR first, then webpage QR when client connects
// ════════════════════════════════════════════════════════════════════
void build_web() {
    scr_web = mk_scr();
    int hh = mk_header(scr_web, "Web", cb_btn_back_predict);
    (void)hh;

    // ── Status label (above QR) ──
    lbl_web_stat = lv_label_create(scr_web);
    lv_label_set_text(lbl_web_stat,
        "Scan to connect to WiFi\n"
        "SSID: " WIFI_AP_SSID "\n"
        "Pass: " WIFI_AP_PASS);
    lv_obj_set_style_text_font(lbl_web_stat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_web_stat, tc->sub_text, 0);
    lv_obj_set_style_text_align(lbl_web_stat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_web_stat, SCR_W - 2*SIDE_PAD);
    lv_obj_align(lbl_web_stat, LV_ALIGN_CENTER, 0, -120);

    // ── WiFi credentials QR (initially visible) ──
    qr_wifi = lv_qrcode_create(scr_web, 140,
                                lv_color_white(), lv_color_black());
    lv_obj_align(qr_wifi, LV_ALIGN_CENTER, 0, 10);
    char wifi_qr_data[128];
    snprintf(wifi_qr_data, sizeof(wifi_qr_data),
        "WIFI:T:WPA;S:%s;P:%s;;", WIFI_AP_SSID, WIFI_AP_PASS);
    lv_qrcode_update(qr_wifi, wifi_qr_data, strlen(wifi_qr_data));
    lv_obj_set_style_border_color(qr_wifi, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr_wifi, 5, 0);

    // ── Webpage QR (initially hidden — shown when client connects) ──
    qr_web = lv_qrcode_create(scr_web, 140,
                               lv_color_white(), lv_color_black());
    lv_obj_align(qr_web, LV_ALIGN_CENTER, 0, 10);
    const char *ph = "http://192.168.4.1";
    lv_qrcode_update(qr_web, ph, strlen(ph));
    lv_obj_set_style_border_color(qr_web, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr_web, 5, 0);
    lv_obj_add_flag(qr_web, LV_OBJ_FLAG_HIDDEN);

    web_client_connected = false;

    add_back_gesture(scr_web, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Settings  (scrollable, with Theme + Accent + About section)
// ════════════════════════════════════════════════════════════════════
void build_settings() {
    scr_settings = mk_scr();
    int hh = mk_header(scr_settings, "Settings", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_settings, hh);

    // -- Display --
    mk_section(cont, "DISPLAY");
    slider_brightness = add_slider_row(cont, LV_SYMBOL_IMAGE, "Brightness",
                                       50, 255, cfg_brightness,
                                       cb_slider_brightness, &lbl_brt_val);

    dd_fps = add_dropdown_row(cont, LV_SYMBOL_REFRESH, "Max FPS",
                              "30\n60", cfg_fps == 60 ? 1 : 0,
                              cb_fps_dropdown);

    // -- Theme --
    mk_section(cont, "THEME");
    sw_dark_mode = add_switch_row(cont, LV_SYMBOL_EYE_OPEN, "Dark Mode",
                                  cfg_dark_mode, cb_dark_mode_switch);

    dd_accent = add_dropdown_row(cont, LV_SYMBOL_TINT, "Accent Color",
                                 accent_dropdown_opts(), cfg_accent,
                                 cb_accent_dropdown);

    // -- Interaction --
    mk_section(cont, "INTERACTION");
    add_switch_row(cont, LV_SYMBOL_LEFT, "Back Gesture",
                   cfg_back_gesture, cb_back_gesture_switch);

    // -- Audio --
    mk_section(cont, "AUDIO");
    slider_volume = add_slider_row(cont, LV_SYMBOL_VOLUME_MAX, "Volume",
                                   0, 100, cfg_volume,
                                   cb_slider_volume, &lbl_vol_val);

    // -- Power --
    mk_section(cont, "POWER");
    slider_sleep = add_slider_row(cont, LV_SYMBOL_POWER, "Auto-sleep (min)",
                                  1, 30, cfg_sleep_min,
                                  cb_slider_sleep, &lbl_slp_val);

    // -- Diagnostics — nav button to Tests --
    mk_section(cont, "DIAGNOSTICS");
    mk_nav_btn(cont, LV_SYMBOL_CHARGE " Hardware Tests", cb_btn_tests);

    // -- About --
    mk_section(cont, "ABOUT");
    lv_obj_t *abox = lv_obj_create(cont);
    lv_obj_set_size(abox, BTN_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(abox, tc->about_bg, 0);
    lv_obj_set_style_bg_opa(abox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(abox, 10, 0);
    lv_obj_set_style_border_width(abox, 0, 0);
    lv_obj_set_style_pad_all(abox, 10, 0);
    lv_obj_clear_flag(abox, LV_OBJ_FLAG_SCROLLABLE);

    lbl_about = lv_label_create(abox);
    lv_label_set_text(lbl_about,
        "Signa v4.0\n"
        "Loading system info...");
    lv_obj_set_style_text_font(lbl_about, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_about, tc->about_text, 0);
    lv_obj_set_width(lbl_about, BTN_W - 24);
    lv_label_set_long_mode(lbl_about, LV_LABEL_LONG_WRAP);

    add_back_gesture(scr_settings, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Tests  (menu — navigation-style buttons with emoji)
// ════════════════════════════════════════════════════════════════════
void build_test() {
    scr_test = mk_scr();
    int hh = mk_header(scr_test, "Tests", cb_btn_back_tests);

    lv_obj_t *cont = mk_content(scr_test, hh);

    mk_nav_btn(cont, LV_SYMBOL_IMAGE          " OLED",         cb_test_oled);
    mk_nav_btn(cont, LV_SYMBOL_LOOP           " MPU6050",      cb_test_mpu);
    mk_nav_btn(cont, LV_SYMBOL_MINUS          " Flex Sensor",  cb_test_flex);
    mk_nav_btn(cont, LV_SYMBOL_GPS            " Hall Effect",  cb_test_hall);
    mk_nav_btn(cont, LV_SYMBOL_BATTERY_FULL   " Battery",      cb_test_battery);
    mk_nav_btn(cont, LV_SYMBOL_VOLUME_MAX     " Speaker",      cb_test_speaker);

    add_back_gesture(scr_test, cb_btn_back_tests);
}

// ════════════════════════════════════════════════════════════════════
//  Test detail  (individual test sub-window — dynamic title)
//  All accent-coloured sliders and benchmark button.
// ════════════════════════════════════════════════════════════════════
void build_test_detail() {
    scr_test_detail = mk_scr();
    int hh = mk_header(scr_test_detail, "Test", cb_btn_back_test_detail, &lbl_test_title);

    lv_obj_t *cont = mk_content(scr_test_detail, hh);

    // Main content label (updated live)
    lbl_test_detail = lv_label_create(cont);
    lv_label_set_text(lbl_test_detail, "Running test...");
    lv_obj_set_style_text_font(lbl_test_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_test_detail, tc->sub_text, 0);
    lv_obj_set_width(lbl_test_detail, BTN_W);
    lv_label_set_long_mode(lbl_test_detail, LV_LABEL_LONG_WRAP);

    // ── OLED brightness slider (hidden by default) — accent colours ──
    test_brt_row = lv_obj_create(cont);
    lv_obj_set_size(test_brt_row, BTN_W, 64);
    lv_obj_set_style_bg_color(test_brt_row, tc->card_bg, 0);
    lv_obj_set_style_bg_opa(test_brt_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(test_brt_row, 10, 0);
    lv_obj_set_style_border_width(test_brt_row, 0, 0);
    lv_obj_set_style_pad_left(test_brt_row, 12, 0);
    lv_obj_set_style_pad_right(test_brt_row, 12, 0);
    lv_obj_set_style_pad_top(test_brt_row, 8, 0);
    lv_obj_set_style_pad_bottom(test_brt_row, 10, 0);
    lv_obj_clear_flag(test_brt_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(test_brt_row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bl = lv_label_create(test_brt_row);
    lv_label_set_text(bl, LV_SYMBOL_IMAGE " Brightness");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bl, tc->card_text, 0);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_test_brt_val = lv_label_create(test_brt_row);
    char bb[8]; snprintf(bb, sizeof(bb), "%d", cfg_brightness);
    lv_label_set_text(lbl_test_brt_val, bb);
    lv_obj_set_style_text_font(lbl_test_brt_val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_test_brt_val, accent_primary(), 0);
    lv_obj_align(lbl_test_brt_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    slider_test_brt = lv_slider_create(test_brt_row);
    lv_obj_set_size(slider_test_brt, BTN_W - 28, 8);
    lv_obj_align(slider_test_brt, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(slider_test_brt, 50, 255);
    lv_slider_set_value(slider_test_brt, cfg_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_test_brt, tc->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_test_brt, accent_dark(),  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_test_brt, accent_light(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_test_brt, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_test_brt, cb_slider_test_brt, LV_EVENT_VALUE_CHANGED, NULL);

    // ── OLED benchmark button (hidden by default) — accent colour ──
    btn_benchmark = mk_btn(cont, LV_SYMBOL_PLAY " Run Benchmark", BTN_W, 44, cb_benchmark);
    lv_obj_set_style_bg_color(btn_benchmark, accent_dark(), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(btn_benchmark, 0), &lv_font_montserrat_16, 0);
    lv_obj_add_flag(btn_benchmark, LV_OBJ_FLAG_HIDDEN);

    // ── Speaker volume control row (hidden by default) — accent colours ──
    test_vol_row = lv_obj_create(cont);
    lv_obj_set_size(test_vol_row, BTN_W, 64);
    lv_obj_set_style_bg_color(test_vol_row, tc->card_bg, 0);
    lv_obj_set_style_bg_opa(test_vol_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(test_vol_row, 10, 0);
    lv_obj_set_style_border_width(test_vol_row, 0, 0);
    lv_obj_set_style_pad_left(test_vol_row, 12, 0);
    lv_obj_set_style_pad_right(test_vol_row, 12, 0);
    lv_obj_set_style_pad_top(test_vol_row, 8, 0);
    lv_obj_set_style_pad_bottom(test_vol_row, 10, 0);
    lv_obj_clear_flag(test_vol_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *vl = lv_label_create(test_vol_row);
    lv_label_set_text(vl, LV_SYMBOL_VOLUME_MAX " Volume");
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vl, tc->card_text, 0);
    lv_obj_align(vl, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_test_vol_val = lv_label_create(test_vol_row);
    char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%d", cfg_volume);
    lv_label_set_text(lbl_test_vol_val, vbuf);
    lv_obj_set_style_text_font(lbl_test_vol_val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_test_vol_val, accent_primary(), 0);
    lv_obj_align(lbl_test_vol_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    slider_test_vol = lv_slider_create(test_vol_row);
    lv_obj_set_size(slider_test_vol, BTN_W - 28, 8);
    lv_obj_align(slider_test_vol, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(slider_test_vol, 0, 100);
    lv_slider_set_value(slider_test_vol, cfg_volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_test_vol, tc->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_test_vol, accent_dark(),  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_test_vol, accent_light(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_test_vol, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_test_vol, cb_slider_test_vol, LV_EVENT_VALUE_CHANGED, NULL);

    add_back_gesture(scr_test_detail, cb_btn_back_test_detail);
}

// ════════════════════════════════════════════════════════════════════
//  populate_test_detail — fill content based on test_active,
//  update header title to match the button label (includes emoji)
// ════════════════════════════════════════════════════════════════════
void populate_test_detail() {
    if (!lbl_test_detail) return;
    int t = test_active;

    // Update header title — test_names[] now include LV_SYMBOL prefixes
    if (lbl_test_title && t >= 0 && t < 6)
        lv_label_set_text(lbl_test_title, test_names[t]);

    // Hide all optional rows first
    lv_obj_add_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(test_brt_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_benchmark, LV_OBJ_FLAG_HIDDEN);

    switch (t) {
    case 0:
        lv_label_set_text(lbl_test_detail,
            "Display Benchmark\n\n"
            "Runs 17 rendering scenes\n"
            "measuring real-time FPS:\n"
            "fills, gradients, shapes,\n"
            "text, arcs, shadows and\n"
            "complex overlap layers.\n\n"
            "Adjust brightness or tap\n"
            "Run Benchmark below.");
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        // Show OLED-specific controls
        lv_obj_clear_flag(test_brt_row, LV_OBJ_FLAG_HIDDEN);
        lv_slider_set_value(slider_test_brt, cfg_brightness, LV_ANIM_OFF);
        { char bv[8]; snprintf(bv, sizeof(bv), "%d", cfg_brightness);
          lv_label_set_text(lbl_test_brt_val, bv); }
        lv_obj_clear_flag(btn_benchmark, LV_OBJ_FLAG_HIDDEN);
        break;
    case 1:
        lv_label_set_text(lbl_test_detail,
            "MPU6050 Test\n\n"
            "Reading IMU data...\n"
            "Waiting for sensor data.");
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        break;
    case 2:
        lv_label_set_text(lbl_test_detail,
            "Flex Sensor Test\n\n"
            "Reading channels...\n"
            "Bend each finger to test.");
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        break;
    case 3:
        lv_label_set_text(lbl_test_detail,
            "Hall Effect Test\n\n"
            "Reading channels...\n"
            "Move magnets near sensors.");
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        break;
    case 4: {
        char buf[160];
        snprintf(buf, sizeof(buf),
            "Battery Test\n\n"
            LV_SYMBOL_BATTERY_FULL " %d%%\n"
            "Voltage: %.2fV\n\n"
            "Status: %s",
            bat_pct_cache, bat_voltage_v,
            bat_pct_cache > 20 ? "OK" : "LOW");
        lv_label_set_text(lbl_test_detail, buf);
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        break;
    }
    case 5:
        lv_label_set_text(lbl_test_detail,
            "Speaker Test\n\n"
            "Playing tone...\n"
            "Adjust volume below.");
        lv_obj_set_style_text_color(lbl_test_detail, accent_primary(), 0);
        // Show speaker volume control
        lv_obj_clear_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);
        lv_slider_set_value(slider_test_vol, cfg_volume, LV_ANIM_OFF);
        { char vb[8]; snprintf(vb, sizeof(vb), "%d", cfg_volume);
          lv_label_set_text(lbl_test_vol_val, vb); }
        break;
    default:
        lv_label_set_text(lbl_test_detail, "Unknown test");
        break;
    }
}
