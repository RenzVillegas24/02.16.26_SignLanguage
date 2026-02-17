/*
 * @file gui/gui_screens.cpp
 * @brief Screen builders (build_splash, build_menu, …)
 */
#include "gui_internal.h"

// ════════════════════════════════════════════════════════════════════
//  Splash
// ════════════════════════════════════════════════════════════════════
void build_splash() {
    scr_splash = mk_scr();

    lv_obj_t *title = lv_label_create(scr_splash);
    lv_label_set_text(title, "Hybrid-Sense");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(scr_splash);
    lv_label_set_text(sub, "Sign Language Glove v4.0");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);
}

// ════════════════════════════════════════════════════════════════════
//  Main menu
// ════════════════════════════════════════════════════════════════════
void build_menu() {
    scr_menu = mk_scr();
    int hh = mk_header(scr_menu, SI_MENU, "Menu", NULL);

    lv_obj_t *cont = mk_content(scr_menu, hh);

    mk_btn(cont, LV_SYMBOL_UPLOAD     " Train",    BTN_W, BTN_H, cb_btn_train);

    lv_obj_t *b2 = mk_btn(cont, LV_SYMBOL_EYE_OPEN  " Predict",  BTN_W, BTN_H, cb_btn_predict);
    lv_obj_set_style_bg_color(b2, lv_color_make(0x00,0x77,0xAA), 0);

    lv_obj_t *b3 = mk_btn(cont, LV_SYMBOL_SETTINGS   " Settings", BTN_W, BTN_H, cb_btn_settings);
    lv_obj_set_style_bg_color(b3, lv_color_make(0x44,0x44,0x66), 0);
}

// ════════════════════════════════════════════════════════════════════
//  Predict sub-menu
// ════════════════════════════════════════════════════════════════════
void build_predict_menu() {
    scr_predict = mk_scr();
    int hh = mk_header(scr_predict, SI_PRED, "Predict", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_predict, hh);

    mk_btn(cont, LV_SYMBOL_EDIT    " Words",  BTN_W, BTN_H, cb_btn_words);

    lv_obj_t *b2 = mk_btn(cont, LV_SYMBOL_VOLUME_MAX " Speech", BTN_W, BTN_H, cb_btn_speech);
    lv_obj_set_style_bg_color(b2, lv_color_make(0x00,0x66,0xAA), 0);

    lv_obj_t *b3 = mk_btn(cont, LV_SYMBOL_SHUFFLE " Both", BTN_W, BTN_H, cb_btn_both);
    lv_obj_set_style_bg_color(b3, lv_color_make(0x55,0x55,0x99), 0);

    lv_obj_t *b4 = mk_btn(cont, LV_SYMBOL_WIFI " Web",   BTN_W, BTN_H, cb_btn_web);
    lv_obj_set_style_bg_color(b4, lv_color_make(0x00,0x99,0x55), 0);
}

// ════════════════════════════════════════════════════════════════════
//  Train
// ════════════════════════════════════════════════════════════════════
void build_train() {
    scr_train = mk_scr();
    int hh = mk_header(scr_train, SI_TRAIN, LV_SYMBOL_UPLOAD "  Train", cb_btn_back_menu);

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
}

// ════════════════════════════════════════════════════════════════════
//  Words
// ════════════════════════════════════════════════════════════════════
void build_words() {
    scr_words = mk_scr();
    int hh = mk_header(scr_words, SI_WORDS, LV_SYMBOL_EDIT "  Words", cb_btn_back_predict);
    int cy = hh + SEP_H;

    lbl_gesture_w = lv_label_create(scr_words);
    lv_label_set_text(lbl_gesture_w, "---");
    lv_obj_set_style_text_font(lbl_gesture_w, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_w, lv_color_make(0x00,0xFF,0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_w, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_w, SCR_W - 2*SIDE_PAD);
    lv_obj_align(lbl_gesture_w, LV_ALIGN_TOP_MID, 0, cy + 6);

    create_bars(scr_words, bar_flex, bar_hall, cy + 46);
}

// ════════════════════════════════════════════════════════════════════
//  Speech
// ════════════════════════════════════════════════════════════════════
void build_speech() {
    scr_speech = mk_scr();
    int hh = mk_header(scr_speech, SI_SPEECH, LV_SYMBOL_VOLUME_MAX "  Speech", cb_btn_back_predict);
    (void)hh;

    lv_obj_t *ic = lv_label_create(scr_speech);
    lv_label_set_text(ic, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ic, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -36);

    lv_obj_t *l = lv_label_create(scr_speech);
    lv_label_set_text(l, "SPEECH MODE\nAudio output active");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, tc->sub_text, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 20);
}

// ════════════════════════════════════════════════════════════════════
//  Both  (Words + Speech)
// ════════════════════════════════════════════════════════════════════
void build_both() {
    scr_both = mk_scr();
    int hh = mk_header(scr_both, SI_BOTH, LV_SYMBOL_SHUFFLE "  Both", cb_btn_back_predict);
    int cy = hh + SEP_H;

    lbl_gesture_b = lv_label_create(scr_both);
    lv_label_set_text(lbl_gesture_b, "---");
    lv_obj_set_style_text_font(lbl_gesture_b, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_b, lv_color_make(0x00,0xFF,0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_b, SCR_W - 2*SIDE_PAD);
    lv_obj_align(lbl_gesture_b, LV_ALIGN_TOP_MID, 0, cy + 6);

    create_bars(scr_both, bar_flex_b, bar_hall_b, cy + 46);

    lv_obj_t *spk = lv_label_create(scr_both);
    lv_label_set_text(spk, LV_SYMBOL_VOLUME_MAX " Audio ON");
    lv_obj_set_style_text_font(spk, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(spk, lv_color_make(0xFF,0xCC,0x00), 0);
    lv_obj_align(spk, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ════════════════════════════════════════════════════════════════════
//  Web
// ════════════════════════════════════════════════════════════════════
void build_web() {
    scr_web = mk_scr();
    int hh = mk_header(scr_web, SI_WEB, LV_SYMBOL_WIFI "  Web", cb_btn_back_predict);
    (void)hh;

    qr_web = lv_qrcode_create(scr_web, 140,
                               lv_color_white(), lv_color_black());
    lv_obj_align(qr_web, LV_ALIGN_CENTER, 0, -24);
    const char *ph = "http://192.168.4.1";
    lv_qrcode_update(qr_web, ph, strlen(ph));
    lv_obj_set_style_border_color(qr_web, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr_web, 5, 0);

    lbl_web_stat = lv_label_create(scr_web);
    lv_label_set_text(lbl_web_stat,
        "WiFi: " WIFI_AP_SSID "\nhttp://192.168.4.1");
    lv_obj_set_style_text_font(lbl_web_stat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_web_stat, tc->sub_text, 0);
    lv_obj_set_style_text_align(lbl_web_stat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_web_stat, SCR_W - 2*SIDE_PAD);
    lv_obj_align(lbl_web_stat, LV_ALIGN_CENTER, 0, 80);
}

// ════════════════════════════════════════════════════════════════════
//  Settings  (scrollable, with About section)
// ════════════════════════════════════════════════════════════════════
void build_settings() {
    scr_settings = mk_scr();
    int hh = mk_header(scr_settings, SI_SETTINGS,
                        LV_SYMBOL_SETTINGS "  Settings", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_settings, hh);

    // -- Display --
    mk_section(cont, "DISPLAY");
    slider_brightness = add_slider_row(cont, LV_SYMBOL_IMAGE, "Brightness",
                                       50, 255, cfg_brightness,
                                       cb_slider_brightness, &lbl_brt_val);

    // Dark / Light mode switch
    sw_dark_mode = add_switch_row(cont, LV_SYMBOL_EYE_OPEN, "Dark Mode",
                                  cfg_dark_mode, cb_dark_mode_switch);

    // FPS limit dropdown
    dd_fps = add_dropdown_row(cont, LV_SYMBOL_REFRESH, "Max FPS",
                              "30\n60", cfg_fps == 60 ? 1 : 0,
                              cb_fps_dropdown);

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

    // -- Component Tests --
    mk_section(cont, "DIAGNOSTICS");
    lv_obj_t *tp = lv_obj_create(cont);
    lv_obj_set_size(tp, BTN_W, 76);
    lv_obj_set_style_bg_color(tp, tc->diag_bg, 0);
    lv_obj_set_style_bg_opa(tp, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tp, 10, 0);
    lv_obj_set_style_border_width(tp, 0, 0);
    lv_obj_set_style_pad_all(tp, 8, 0);
    lv_obj_clear_flag(tp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tp_lbl = lv_label_create(tp);
    lv_label_set_text(tp_lbl, "Run hardware self-tests");
    lv_obj_set_style_text_font(tp_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tp_lbl, tc->diag_text, 0);
    lv_obj_align(tp_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *bt = mk_btn(tp, LV_SYMBOL_CHARGE " COMPONENT TESTS",
                           BTN_W - 20, 36, cb_btn_tests);
    lv_obj_set_style_bg_color(bt, lv_color_make(0x99,0x55,0x00), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(bt, 0),
                               &lv_font_montserrat_16, 0);
    lv_obj_align(bt, LV_ALIGN_BOTTOM_MID, 0, 0);

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
        "Hybrid-Sense v4.0\n"
        "Loading system info...");
    lv_obj_set_style_text_font(lbl_about, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_about, tc->about_text, 0);
    lv_obj_set_width(lbl_about, BTN_W - 24);
    lv_label_set_long_mode(lbl_about, LV_LABEL_LONG_WRAP);
}

// ════════════════════════════════════════════════════════════════════
//  Tests  (menu → detail sub-windows)
// ════════════════════════════════════════════════════════════════════
void build_test() {
    scr_test = mk_scr();
    int hh = mk_header(scr_test, SI_TEST,
                        LV_SYMBOL_CHARGE "  Tests", cb_btn_back_tests);

    lv_obj_t *cont = mk_content(scr_test, hh);

    lv_obj_t *t1 = mk_btn(cont, LV_SYMBOL_IMAGE          " OLED",    BTN_W, BTN_H, cb_test_oled);
    lv_obj_set_style_bg_color(t1, lv_color_make(0x88,0x33,0xCC), 0);

    lv_obj_t *t2 = mk_btn(cont, LV_SYMBOL_LOOP            " MPU",    BTN_W, BTN_H, cb_test_mpu);
    lv_obj_set_style_bg_color(t2, lv_color_make(0x33,0x88,0xCC), 0);

    lv_obj_t *t3 = mk_btn(cont, LV_SYMBOL_MINUS            " Flex",   BTN_W, BTN_H, cb_test_flex);
    lv_obj_set_style_bg_color(t3, lv_color_make(0x00,0x88,0xAA), 0);

    lv_obj_t *t4 = mk_btn(cont, LV_SYMBOL_GPS              " Hall",   BTN_W, BTN_H, cb_test_hall);
    lv_obj_set_style_bg_color(t4, lv_color_make(0xCC,0x66,0x00), 0);

    lv_obj_t *t5 = mk_btn(cont, LV_SYMBOL_BATTERY_FULL     " Battery",BTN_W, BTN_H, cb_test_battery);
    lv_obj_set_style_bg_color(t5, lv_color_make(0x33,0x99,0x33), 0);

    lv_obj_t *t6 = mk_btn(cont, LV_SYMBOL_VOLUME_MAX       " Speaker",BTN_W, BTN_H, cb_test_speaker);
    lv_obj_set_style_bg_color(t6, lv_color_make(0xCC,0x33,0x55), 0);
}

// ════════════════════════════════════════════════════════════════════
//  Test detail  (individual test sub-window)
// ════════════════════════════════════════════════════════════════════
static const lv_color_t test_colors[] = {
    LV_COLOR_MAKE(0x88,0x33,0xCC),
    LV_COLOR_MAKE(0x33,0x88,0xCC),
    LV_COLOR_MAKE(0x00,0xBB,0xEE),
    LV_COLOR_MAKE(0xFF,0x88,0x00),
    LV_COLOR_MAKE(0x33,0x99,0x33),
    LV_COLOR_MAKE(0xCC,0x33,0x55)
};

void build_test_detail() {
    scr_test_detail = mk_scr();
    int hh = mk_header(scr_test_detail, SI_TEST_DETAIL,
                        LV_SYMBOL_CHARGE "  Test", cb_btn_back_test_detail);

    lv_obj_t *cont = mk_content(scr_test_detail, hh);

    // Main content label (updated live)
    lbl_test_detail = lv_label_create(cont);
    lv_label_set_text(lbl_test_detail, "Running test...");
    lv_obj_set_style_text_font(lbl_test_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_test_detail, tc->sub_text, 0);
    lv_obj_set_width(lbl_test_detail, BTN_W);
    lv_label_set_long_mode(lbl_test_detail, LV_LABEL_LONG_WRAP);

    // Speaker volume control row (hidden by default)
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
    lv_obj_set_style_text_color(lbl_test_vol_val, lv_color_make(0xCC,0x33,0x55), 0);
    lv_obj_align(lbl_test_vol_val, LV_ALIGN_TOP_RIGHT, 0, 0);

    slider_test_vol = lv_slider_create(test_vol_row);
    lv_obj_set_size(slider_test_vol, BTN_W - 28, 8);
    lv_obj_align(slider_test_vol, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(slider_test_vol, 0, 100);
    lv_slider_set_value(slider_test_vol, cfg_volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_test_vol, tc->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_test_vol, lv_color_make(0xCC,0x33,0x55), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_test_vol, lv_color_make(0xFF,0x44,0x66), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_test_vol, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_test_vol, cb_slider_test_vol, LV_EVENT_VALUE_CHANGED, NULL);
}

// ════════════════════════════════════════════════════════════════════
//  populate_test_detail — fill content based on test_active
// ════════════════════════════════════════════════════════════════════
void populate_test_detail() {
    if (!lbl_test_detail) return;
    int t = test_active;

    switch (t) {
    case 0:
        lv_label_set_text(lbl_test_detail,
            "OLED Test\n\n"
            "Running color pattern...\n"
            "Check display for artifacts.");
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[0], 0);
        break;
    case 1:
        lv_label_set_text(lbl_test_detail,
            "MPU6050 Test\n\n"
            "Reading IMU data...\n"
            "Waiting for sensor data.");
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[1], 0);
        break;
    case 2:
        lv_label_set_text(lbl_test_detail,
            "Flex Sensor Test\n\n"
            "Reading channels...\n"
            "Bend each finger to test.");
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[2], 0);
        break;
    case 3:
        lv_label_set_text(lbl_test_detail,
            "Hall Effect Test\n\n"
            "Reading channels...\n"
            "Move magnets near sensors.");
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[3], 0);
        break;
    case 4: {
        char buf[120];
        snprintf(buf, sizeof(buf),
            "Battery Test\n\n"
            LV_SYMBOL_BATTERY_FULL " %d%%\n"
            "Voltage: %.2fV\n\n"
            "Status: %s",
            bat_pct_cache, bat_voltage_v,
            bat_pct_cache > 20 ? "OK" : "LOW");
        lv_label_set_text(lbl_test_detail, buf);
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[4], 0);
        break;
    }
    case 5:
        lv_label_set_text(lbl_test_detail,
            "Speaker Test\n\n"
            "Playing tone...\n"
            "Adjust volume below.");
        lv_obj_set_style_text_color(lbl_test_detail, test_colors[5], 0);
        break;
    default:
        lv_label_set_text(lbl_test_detail, "Unknown test");
        break;
    }

    // Show/hide speaker volume control
    if (t == 5) {
        lv_obj_clear_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);
        lv_slider_set_value(slider_test_vol, cfg_volume, LV_ANIM_OFF);
        char vb[8]; snprintf(vb, sizeof(vb), "%d", cfg_volume);
        lv_label_set_text(lbl_test_vol_val, vb);
    } else {
        lv_obj_add_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);
    }
}
