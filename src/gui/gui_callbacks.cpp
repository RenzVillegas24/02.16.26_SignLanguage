/*
 * @file gui/gui_callbacks.cpp
 * @brief All LVGL event callbacks (navigation, sliders, settings, tests)
 */
#include "gui_internal.h"
#include "demos/benchmark/lv_demo_benchmark.h"

// ════════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════════
void fire_mode(AppMode m) {
    if (s_mode_cb) s_mode_cb(m);
}

void nav_to(lv_obj_t *scr, bool left) {
    lv_scr_load_anim(scr,
        left ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        ANIM_TIME, 0, false);
}

// ════════════════════════════════════════════════════════════════════
//  Navigation callbacks
// ════════════════════════════════════════════════════════════════════
void cb_btn_train(lv_event_t *e)   { (void)e; nav_to(scr_train, true);   cur_gui_mode=MODE_TRAIN;          fire_mode(MODE_TRAIN); }
void cb_btn_predict(lv_event_t *e) { (void)e; nav_to(scr_predict, true); }
void cb_btn_settings(lv_event_t *e){ (void)e; nav_to(scr_settings, true); cur_gui_mode=MODE_SETTINGS;      fire_mode(MODE_SETTINGS); }
void cb_btn_local(lv_event_t *e)   { (void)e; nav_to(scr_local, true);   cur_gui_mode=MODE_PREDICT_LOCAL;  fire_mode(MODE_PREDICT_LOCAL); }
void cb_btn_web(lv_event_t *e)     { (void)e; nav_to(scr_web, true);     cur_gui_mode=MODE_PREDICT_WEB;    fire_mode(MODE_PREDICT_WEB); }
void cb_btn_tests(lv_event_t *e)   { (void)e; nav_to(scr_test, true);    cur_gui_mode=MODE_TEST;           fire_mode(MODE_TEST); test_active=-1; }

void cb_btn_back_menu(lv_event_t *e)    { (void)e; nav_to(scr_menu, false);     cur_gui_mode=MODE_MENU; fire_mode(MODE_MENU); }
void cb_btn_back_predict(lv_event_t *e) { (void)e; nav_to(scr_predict, false);  cur_gui_mode=MODE_MENU; fire_mode(MODE_MENU); }
void cb_btn_back_tests(lv_event_t *e)   { (void)e; nav_to(scr_settings, false); cur_gui_mode=MODE_SETTINGS; fire_mode(MODE_SETTINGS); test_active=-1; }
void cb_btn_back_test_detail(lv_event_t *e) { (void)e; nav_to(scr_test, false); cur_gui_mode=MODE_TEST; fire_mode(MODE_TEST); test_active=-1; }

void cb_splash_timer(lv_timer_t *t) {
    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
    cur_gui_mode = MODE_MENU;
    lv_timer_del(t);
}

// ════════════════════════════════════════════════════════════════════
//  Local screen toggle callbacks
// ════════════════════════════════════════════════════════════════════
void cb_local_sensors(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_local_sensors = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (bars_container) {
        if (cfg_local_sensors) lv_obj_clear_flag(bars_container, LV_OBJ_FLAG_HIDDEN);
        else                   lv_obj_add_flag(bars_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void cb_local_words(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_local_words = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (lbl_gesture) {
        if (cfg_local_words) lv_obj_clear_flag(lbl_gesture, LV_OBJ_FLAG_HIDDEN);
        else                 lv_obj_add_flag(lbl_gesture, LV_OBJ_FLAG_HIDDEN);
    }
}

void cb_local_speech(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_local_speech = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

// ════════════════════════════════════════════════════════════════════
//  Slider callbacks
// ════════════════════════════════════════════════════════════════════
void cb_slider_brightness(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_brightness = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_brightness);
    lv_label_set_text(lbl_brt_val, buf);
    if (s_brightness_cb) s_brightness_cb(cfg_brightness);
    save_settings();
}

void cb_slider_volume(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_volume = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_volume);
    lv_label_set_text(lbl_vol_val, buf);
    if (s_volume_cb) s_volume_cb(cfg_volume);
    save_settings();
}

void cb_slider_sleep(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_sleep_min = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_sleep_min);
    lv_label_set_text(lbl_slp_val, buf);
    save_settings();
}

void cb_slider_test_vol(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_volume = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_volume);
    lv_label_set_text(lbl_test_vol_val, buf);
    // Sync settings volume slider
    if (slider_volume) lv_slider_set_value(slider_volume, cfg_volume, LV_ANIM_OFF);
    if (lbl_vol_val) lv_label_set_text(lbl_vol_val, buf);
    if (s_volume_cb) s_volume_cb(cfg_volume);
    save_settings();
}

void cb_slider_test_brt(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_brightness = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_brightness);
    if (lbl_test_brt_val) lv_label_set_text(lbl_test_brt_val, buf);
    // Sync settings brightness slider
    if (slider_brightness) lv_slider_set_value(slider_brightness, cfg_brightness, LV_ANIM_OFF);
    if (lbl_brt_val) lv_label_set_text(lbl_brt_val, buf);
    if (s_brightness_cb) s_brightness_cb(cfg_brightness);
    save_settings();
}

// ════════════════════════════════════════════════════════════════════
//  Settings control callbacks
// ════════════════════════════════════════════════════════════════════

// Timer callback that performs the deferred theme rebuild
static void theme_rebuild_timer(lv_timer_t *t) {
    apply_theme();
    lv_timer_del(t);
}

void cb_dark_mode_switch(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_dark_mode = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_settings();
    // Defer the full rebuild to after the event finishes
    lv_timer_create(theme_rebuild_timer, 0, NULL);
}

void cb_fps_dropdown(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    cfg_fps = (sel == 1) ? 60 : 30;
    // Apply FPS limit via display refresh timer
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        uint32_t period = (cfg_fps == 60) ? 16 : 33;
        lv_timer_set_period(disp->refr_timer, period);
    }
    save_settings();
}

// ════════════════════════════════════════════════════════════════════
//  Test callbacks  (navigate to detail sub-windows)
// ════════════════════════════════════════════════════════════════════
void cb_test_oled(lv_event_t *e) {
    (void)e;
    test_active = 0;
    populate_test_detail();
    nav_to(scr_test_detail, true);
    if (s_test_oled_cb) s_test_oled_cb();
}

void cb_test_mpu(lv_event_t *e) {
    (void)e;
    test_active = 1;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}

void cb_test_flex(lv_event_t *e) {
    (void)e;
    test_active = 2;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}

void cb_test_hall(lv_event_t *e) {
    (void)e;
    test_active = 3;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}

void cb_test_battery(lv_event_t *e) {
    (void)e;
    test_active = 4;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}

void cb_test_speaker(lv_event_t *e) {
    (void)e;
    test_active = 5;
    populate_test_detail();
    nav_to(scr_test_detail, true);
    if (s_test_speaker_cb) s_test_speaker_cb();
}

// ════════════════════════════════════════════════════════════════════
//  OLED benchmark callback
// ════════════════════════════════════════════════════════════════════
void cb_benchmark(lv_event_t *e) {
    (void)e;
    // Launch the LVGL benchmark demo — takes over the display.
    // The device must be reset to return to the normal GUI.
    lv_demo_benchmark();
}
