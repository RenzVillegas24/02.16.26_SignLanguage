/*
 * @file gui/gui_callbacks.cpp
 * @brief All LVGL event callbacks: navigation, sliders, settings, tests,
 *        accent colour picker, and comprehensive 15-scene OLED benchmark.
 */
#include "gui_internal.h"

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
    if (stat_bar) {
        lv_obj_clear_flag(stat_bar, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, stat_bar);
        lv_anim_set_values(&anim, 0, 255);
        lv_anim_set_time(&anim, 400);
        lv_anim_set_exec_cb(&anim, [](void *var, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
        });
        lv_anim_start(&anim);
    }
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

void cb_back_gesture_switch(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_back_gesture = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_settings();
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
    lv_timer_create(theme_rebuild_timer, 0, NULL);
}

void cb_fps_dropdown(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    cfg_fps = (sel == 1) ? 60 : 30;
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        uint32_t period = (cfg_fps == 60) ? 16 : 33;
        lv_timer_set_period(disp->refr_timer, period);
    }
    save_settings();
}

void cb_accent_dropdown(lv_event_t *e) {
    lv_obj_t *dd = lv_event_get_target(e);
    cfg_accent = (uint8_t)lv_dropdown_get_selected(dd);
    save_settings();
    // Defer full theme rebuild (recreates all screens with new accent)
    lv_timer_create(theme_rebuild_timer, 0, NULL);
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
    (void)e; test_active = 1;
    populate_test_detail(); nav_to(scr_test_detail, true);
}

void cb_test_flex(lv_event_t *e) {
    (void)e; test_active = 2;
    populate_test_detail(); nav_to(scr_test_detail, true);
}

void cb_test_hall(lv_event_t *e) {
    (void)e; test_active = 3;
    populate_test_detail(); nav_to(scr_test_detail, true);
}

void cb_test_hall_top(lv_event_t *e) {
    (void)e; test_active = 4;
    populate_test_detail(); nav_to(scr_test_detail, true);
}

void cb_test_battery(lv_event_t *e) {
    (void)e; test_active = 5;
    populate_test_detail(); nav_to(scr_test_detail, true);
}

void cb_test_speaker(lv_event_t *e) {
    (void)e; test_active = 6;
    populate_test_detail(); nav_to(scr_test_detail, true);
    if (s_test_speaker_cb) s_test_speaker_cb();
}

// ════════════════════════════════════════════════════════════════════
//  Comprehensive OLED Benchmark  (15 scenes, monitor_cb FPS)
// ════════════════════════════════════════════════════════════════════

// ── Benchmark constants ──
#define BENCH_NUM_SCENES   17
#define BENCH_SCENE_TIME_MS 1500

struct BenchScene {
    const char *name;
    void (*setup)(lv_obj_t*);
    uint32_t refr_cnt;
    uint32_t time_sum;
    uint32_t fps;
};

// ── Deterministic PRNG (like LVGL demo) ──
static uint32_t _rnd = 12345;
static int32_t rnd(int32_t lo, int32_t hi) {
    _rnd = _rnd * 1103515245u + 12345u;
    return lo + (int32_t)((_rnd >> 16) % (uint32_t)(hi - lo + 1));
}

// ── Animation helpers ──
static void anim_y_cb(void *var, int32_t v) { lv_obj_set_y((lv_obj_t*)var, v); }
static void anim_x_cb(void *var, int32_t v) { lv_obj_set_x((lv_obj_t*)var, v); }
static void anim_arc_end(void *var, int32_t v) { lv_arc_set_end_angle((lv_obj_t*)var, (uint16_t)v); }

static void bounce(lv_obj_t *o, int32_t y0, int32_t y1, uint32_t ms) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, y0, y1);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);
}

static void drift_x(lv_obj_t *o, int32_t x0, int32_t x1, uint32_t ms) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_values(&a, x0, x1);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);
}

// ── Scene area dimensions (used by setup fns) ──
#define SA_W  280
#define SA_H  320
#define OBJ_N 6

// ── Scene setup functions ──
static void scene_solid(lv_obj_t *p) {
    // One large rect that shifts position, forcing full-screen redraws
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SA_W, SA_H);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_make(0x33, 0x99, 0xFF), 0);
    bounce(o, 0, 10, 300);
}

static void scene_gradient(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SA_W, SA_H);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, accent_primary(), 0);
    lv_obj_set_style_bg_grad_color(o, accent_dark(), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    bounce(o, 0, 8, 350);
}

static void scene_small_rects(lv_obj_t *p) {
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(20, 40);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(0, SA_W - sz), rnd(0, 40));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(60,255), rnd(60,255), rnd(60,255)), 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/2, SA_H - sz), 600 + i * 120);
        drift_x(o, lv_obj_get_x(o), rnd(0, SA_W - sz), 900 + i * 80);
    }
}

static void scene_large_rects(lv_obj_t *p) {
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int w = rnd(80, 140); int h = rnd(50, 90);
        lv_obj_set_size(o, w, h);
        lv_obj_set_pos(o, rnd(0, SA_W - w), rnd(0, 30));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(60,255), rnd(60,255), rnd(60,255)), 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/2, SA_H - h), 700 + i * 150);
        drift_x(o, lv_obj_get_x(o), rnd(0, SA_W - w), 1000 + i * 100);
    }
}

static void scene_rounded_rects(lv_obj_t *p) {
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(30, 60);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(0, SA_W - sz), rnd(0, 40));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(o, 12, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(80,255), rnd(80,255), rnd(80,255)), 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/2, SA_H - sz), 650 + i * 100);
    }
}

static void scene_circles(lv_obj_t *p) {
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int d = rnd(24, 56);
        lv_obj_set_size(o, d, d);
        lv_obj_set_pos(o, rnd(0, SA_W - d), rnd(0, 30));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(80,255), rnd(60,200), rnd(80,255)), 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/2, SA_H - d), 500 + i * 130);
        drift_x(o, lv_obj_get_x(o), rnd(0, SA_W - d), 700 + i * 90);
    }
}

static void scene_borders(lv_obj_t *p) {
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(30, 60);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(0, SA_W - sz), rnd(0, 30));
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, rnd(2, 6), 0);
        lv_obj_set_style_border_color(o, lv_color_make(rnd(80,255), rnd(80,255), rnd(80,255)), 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/2, SA_H - sz), 550 + i * 110);
    }
}

static void scene_shadows(lv_obj_t *p) {
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(40, 70);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(10, SA_W - sz - 10), rnd(10, 40));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, tc->card_bg, 0);
        lv_obj_set_style_radius(o, 8, 0);
        lv_obj_set_style_shadow_width(o, rnd(10, 25), 0);
        lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/3, SA_H - sz - 20), 800 + i * 140);
    }
}

static void scene_shadow_ofs(lv_obj_t *p) {
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(40, 65);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(10, SA_W - sz - 20), rnd(10, 30));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, accent_light(), 0);
        lv_obj_set_style_radius(o, 10, 0);
        lv_obj_set_style_shadow_width(o, rnd(12, 20), 0);
        lv_obj_set_style_shadow_ofs_x(o, rnd(4, 10), 0);
        lv_obj_set_style_shadow_ofs_y(o, rnd(4, 10), 0);
        lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_50, 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/3, SA_H - sz - 20), 750 + i * 160);
    }
}

static void scene_opacity(lv_obj_t *p) {
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(50, 90);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(0, SA_W - sz), rnd(0, 40));
        lv_obj_set_style_bg_opa(o, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(60,255), rnd(60,255), rnd(60,255)), 0);
        lv_obj_set_style_radius(o, 8, 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/3, SA_H - sz), 600 + i * 100);
        drift_x(o, lv_obj_get_x(o), rnd(0, SA_W - sz), 800 + i * 70);
    }
}

static void scene_text_small(lv_obj_t *p) {
    const char *strs[] = {"Signa", "Hello", "LVGL", "Test!", "ESP32", "OLED!"};
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, strs[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_make(rnd(100,255), rnd(100,255), rnd(100,255)), 0);
        lv_obj_set_pos(l, rnd(10, SA_W - 60), rnd(0, 30));
        bounce(l, lv_obj_get_y(l), rnd(SA_H/2, SA_H - 20), 500 + i * 90);
        drift_x(l, lv_obj_get_x(l), rnd(10, SA_W - 60), 700 + i * 80);
    }
}

static void scene_text_medium(lv_obj_t *p) {
    const char *strs[] = {"Signa", "Hello", "LVGL", "Test!", "ESP32", "OLED!"};
    for (int i = 0; i < OBJ_N; i++) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, strs[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, lv_color_make(rnd(100,255), rnd(100,255), rnd(100,255)), 0);
        lv_obj_set_pos(l, rnd(10, SA_W - 80), rnd(0, 30));
        bounce(l, lv_obj_get_y(l), rnd(SA_H/2, SA_H - 30), 550 + i * 100);
        drift_x(l, lv_obj_get_x(l), rnd(10, SA_W - 80), 750 + i * 90);
    }
}

static void scene_text_large(lv_obj_t *p) {
    const char *strs[] = {"Signa", "Hello", "LVGL", "ESP32"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, strs[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(l, lv_color_make(rnd(100,255), rnd(100,255), rnd(100,255)), 0);
        lv_obj_set_pos(l, rnd(10, SA_W - 100), rnd(0, 20));
        bounce(l, lv_obj_get_y(l), rnd(SA_H/3, SA_H - 40), 600 + i * 130);
        drift_x(l, lv_obj_get_x(l), rnd(10, SA_W - 100), 850 + i * 100);
    }
}

static void scene_arc_thin(lv_obj_t *p) {
    for (int i = 0; i < 4; i++) {
        lv_obj_t *a = lv_arc_create(p);
        int d = rnd(50, 80);
        lv_obj_set_size(a, d, d);
        lv_obj_set_pos(a, rnd(10, SA_W - d - 10), rnd(10, 40));
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_angles(a, 0, rnd(90, 270));
        lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, tc->slider_track, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, accent_primary(), LV_PART_INDICATOR);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        bounce(a, lv_obj_get_y(a), rnd(SA_H/3, SA_H - d - 10), 700 + i * 150);
        // Animate end angle
        lv_anim_t an; lv_anim_init(&an);
        lv_anim_set_var(&an, a);
        lv_anim_set_values(&an, 30, 330);
        lv_anim_set_time(&an, 1200 + i * 100);
        lv_anim_set_playback_time(&an, 1200 + i * 100);
        lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&an, anim_arc_end);
        lv_anim_start(&an);
    }
}

static void scene_arc_thick(lv_obj_t *p) {
    for (int i = 0; i < 4; i++) {
        lv_obj_t *a = lv_arc_create(p);
        int d = rnd(55, 85);
        lv_obj_set_size(a, d, d);
        lv_obj_set_pos(a, rnd(10, SA_W - d - 10), rnd(10, 40));
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_angles(a, 0, rnd(90, 270));
        lv_obj_set_style_arc_width(a, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, tc->slider_track, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, accent_dark(), LV_PART_INDICATOR);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        bounce(a, lv_obj_get_y(a), rnd(SA_H/3, SA_H - d - 10), 650 + i * 130);
        lv_anim_t an; lv_anim_init(&an);
        lv_anim_set_var(&an, a);
        lv_anim_set_values(&an, 20, 340);
        lv_anim_set_time(&an, 1000 + i * 120);
        lv_anim_set_playback_time(&an, 1000 + i * 120);
        lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&an, anim_arc_end);
        lv_anim_start(&an);
    }
}

// ── Heavy benchmark 1: Multi-layer Shadows (very expensive rendering) ──
static void scene_multilayer_shadows(lv_obj_t *p) {
    // Multiple large objects with thick shadows - very GPU/render intensive
    for (int i = 0; i < 3; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(70, 110);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(15, SA_W - sz - 15), rnd(10, 50));
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, accent_light(), 0);
        lv_obj_set_style_radius(o, 15, 0);
        // Very thick shadow with high opacity - computationally expensive
        lv_obj_set_style_shadow_width(o, 30, 0);
        lv_obj_set_style_shadow_ofs_x(o, 8, 0);
        lv_obj_set_style_shadow_ofs_y(o, 8, 0);
        lv_obj_set_style_shadow_spread(o, 5, 0);
        lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_70, 0);
        // Add gradient for extra complexity
        lv_obj_set_style_bg_grad_color(o, accent_dark(), 0);
        lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/3, SA_H - sz - 30), 900 + i * 200);
        drift_x(o, lv_obj_get_x(o), rnd(15, SA_W - sz - 15), 1100 + i * 150);
    }
}

// ── Heavy benchmark 2: Complex Overlapping with Gradients ──
static void scene_complex_overlap(lv_obj_t *p) {
    // Many overlapping semi-transparent objects with gradients and borders
    for (int i = 0; i < 8; i++) {
        lv_obj_t *o = lv_obj_create(p);
        lv_obj_remove_style_all(o);
        int sz = rnd(60, 100);
        lv_obj_set_size(o, sz, sz);
        lv_obj_set_pos(o, rnd(0, SA_W - sz), rnd(0, 50));
        // Semi-transparent with gradient - heavy blend operations
        lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(o, lv_color_make(rnd(100,255), rnd(100,255), rnd(100,255)), 0);
        lv_obj_set_style_bg_grad_color(o, lv_color_make(rnd(50,200), rnd(50,200), rnd(50,200)), 0);
        lv_obj_set_style_bg_grad_dir(o, (i % 2) ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_radius(o, rnd(8, 20), 0);
        // Thick borders add more rendering work
        lv_obj_set_style_border_width(o, 4, 0);
        lv_obj_set_style_border_color(o, accent_primary(), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_80, 0);
        // Add shadow for maximum complexity
        lv_obj_set_style_shadow_width(o, 15, 0);
        lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_40, 0);
        bounce(o, lv_obj_get_y(o), rnd(SA_H/3, SA_H - sz), 550 + i * 80);
        drift_x(o, lv_obj_get_x(o), rnd(0, SA_W - sz), 750 + i * 70);
    }
}

// ── Scene descriptors (with emoji symbols in names) ──
static BenchScene _scenes[BENCH_NUM_SCENES] = {
    { LV_SYMBOL_IMAGE     " Solid Fill",      scene_solid,        0,0,0 },
    { LV_SYMBOL_IMAGE     " Gradient",        scene_gradient,     0,0,0 },
    { LV_SYMBOL_LIST      " Small Rects",     scene_small_rects,  0,0,0 },
    { LV_SYMBOL_LIST      " Large Rects",     scene_large_rects,  0,0,0 },
    { LV_SYMBOL_LIST      " Rounded Rects",   scene_rounded_rects,0,0,0 },
    { LV_SYMBOL_LOOP      " Circles",         scene_circles,      0,0,0 },
    { LV_SYMBOL_LIST      " Borders",         scene_borders,      0,0,0 },
    { LV_SYMBOL_CHARGE    " Shadows",         scene_shadows,      0,0,0 },
    { LV_SYMBOL_CHARGE    " Shadow + Offset", scene_shadow_ofs,   0,0,0 },
    { LV_SYMBOL_EYE_OPEN  " Opacity 50%",     scene_opacity,      0,0,0 },
    { LV_SYMBOL_EDIT      " Text Small",      scene_text_small,   0,0,0 },
    { LV_SYMBOL_EDIT      " Text Medium",     scene_text_medium,  0,0,0 },
    { LV_SYMBOL_EDIT      " Text Large",      scene_text_large,   0,0,0 },
    { LV_SYMBOL_REFRESH   " Arc Thin",        scene_arc_thin,     0,0,0 },
    { LV_SYMBOL_REFRESH   " Arc Thick",       scene_arc_thick,    0,0,0 },
    { LV_SYMBOL_WARNING   " Heavy: Multi Shadow", scene_multilayer_shadows, 0,0,0 },
    { LV_SYMBOL_WARNING   " Heavy: Complex Overlap", scene_complex_overlap, 0,0,0 },
};

// ── Benchmark runtime state ──
static lv_obj_t  *_bench_overlay   = nullptr;
static lv_obj_t  *_bench_title     = nullptr;
static lv_obj_t  *_bench_fps_lbl   = nullptr;
static lv_obj_t  *_bench_area      = nullptr;
static lv_timer_t *_bench_tmr      = nullptr;
static int         _bench_cur      = 0;
static uint32_t    _bench_scene_t0 = 0;
static bool        _bench_running  = false;

// Saved original monitor callback
static void (*_orig_monitor)(struct _lv_disp_drv_t*, uint32_t, uint32_t) = nullptr;

// Query whether benchmark is in progress (used by gesture blocker)
bool is_bench_running() { return _bench_running || _bench_overlay != nullptr; }

// ── Back from results — delete overlay, return to test detail ──
static void bench_results_back_cb(lv_event_t *e) {
    (void)e;
    if (_bench_overlay) {
        lv_obj_del(_bench_overlay);
        _bench_overlay = nullptr;
        _bench_title   = nullptr;
        _bench_fps_lbl = nullptr;
        _bench_area    = nullptr;
    }
}

// ── Monitor callback — counts refreshes & render time per scene ──
static void bench_monitor(lv_disp_drv_t *drv, uint32_t time_ms, uint32_t px) {
    if (_orig_monitor) _orig_monitor(drv, time_ms, px);
    if (_bench_running && _bench_cur < BENCH_NUM_SCENES) {
        _scenes[_bench_cur].refr_cnt++;
        _scenes[_bench_cur].time_sum += time_ms;
    }
}

// ── Clear scene area children (delete animated objects) ──
static void clear_area() {
    if (!_bench_area) return;
    // Stop ALL animations on all children first
    uint32_t child_cnt = lv_obj_get_child_cnt(_bench_area);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(_bench_area, i);
        if (child) lv_anim_del(child, NULL);
    }
    // Stop animations on the area itself
    lv_anim_del(_bench_area, NULL);
    // Small delay to ensure animations are fully stopped
    lv_refr_now(NULL);
    // Clean all children - this properly frees memory
    lv_obj_clean(_bench_area);
    // Force garbage collection and refresh
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    lv_refr_now(NULL);
}

// ── Show final results in the overlay ──
static void show_results(int count) {
    // Clean up animations first
    clear_area();
    if (!_bench_overlay) return;

    // Hide live FPS label and old title
    if (_bench_fps_lbl) lv_obj_add_flag(_bench_fps_lbl, LV_OBJ_FLAG_HIDDEN);
    if (_bench_title)   lv_obj_add_flag(_bench_title, LV_OBJ_FLAG_HIDDEN);

    // ── Nav-style header bar (below the global status bar) ──
    lv_obj_t *nav = lv_obj_create(_bench_overlay);
    lv_obj_set_size(nav, SCR_W, NAV_H);
    lv_obj_set_pos(nav, 0, STAT_H);
    lv_obj_add_style(nav, &sty_hdr, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_left(nav, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(nav, SIDE_PAD, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t *bb = lv_btn_create(nav);
    lv_obj_set_size(bb, BACK_SZ, BACK_SZ);
    lv_obj_align(bb, LV_ALIGN_LEFT_MID, -4, 0);
    lv_obj_set_style_bg_color(bb, lv_color_mix(accent_primary(), tc->back_btn_bg, 30), 0);
    lv_obj_set_style_bg_opa(bb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bb, 10, 0);
    lv_obj_set_style_border_width(bb, 0, 0);
    lv_obj_set_style_shadow_width(bb, 0, 0);
    lv_obj_add_event_cb(bb, bench_results_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ba = lv_label_create(bb);
    lv_label_set_text(ba, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(ba, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ba, tc->hdr_text, 0);
    lv_obj_center(ba);

    // Title
    lv_obj_t *tt = lv_label_create(nav);
    lv_label_set_text(tt, "Results");
    lv_obj_set_style_text_font(tt, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(tt, tc->hdr_text, 0);
    lv_obj_align(tt, LV_ALIGN_LEFT_MID, BACK_SZ + 2, 0);

    // ── Separator ──
    lv_obj_t *sep0 = lv_obj_create(_bench_overlay);
    lv_obj_set_size(sep0, SCR_W, SEP_H);
    lv_obj_set_pos(sep0, 0, STAT_H + NAV_H);
    lv_obj_set_style_bg_color(sep0, tc->sep, 0);
    lv_obj_set_style_bg_opa(sep0, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep0, 0, 0);
    lv_obj_set_style_radius(sep0, 0, 0);

    // ── Reposition results area below header ──
    int results_y = STAT_H + NAV_H + SEP_H;
    lv_obj_set_pos(_bench_area, 0, results_y);
    lv_obj_set_size(_bench_area, SCR_W, SCR_H - results_y);

    // Make area scrollable for results
    lv_obj_set_flex_flow(_bench_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_bench_area, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_bench_area, 2, 0);
    lv_obj_set_style_pad_left(_bench_area, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(_bench_area, SIDE_PAD, 0);
    lv_obj_set_style_pad_top(_bench_area, 10, 0);
    lv_obj_set_style_pad_bottom(_bench_area, 10, 0);
    lv_obj_add_flag(_bench_area, LV_OBJ_FLAG_SCROLLABLE);

    // Average FPS
    float total = 0;
    int valid = 0;
    for (int i = 0; i < count; i++) {
        if (_scenes[i].time_sum > 0) {
            _scenes[i].fps = (uint32_t)(_scenes[i].refr_cnt * 1000u
                                        / _scenes[i].time_sum);
            total += _scenes[i].fps;
            valid++;
        }
    }
    float avg = valid > 0 ? total / valid : 0;

    // Average label at top
    char abuf[40];
    snprintf(abuf, sizeof(abuf), LV_SYMBOL_PLAY " Average: %.0f FPS", avg);
    lv_obj_t *al = lv_label_create(_bench_area);
    lv_label_set_text(al, abuf);
    lv_obj_set_style_text_font(al, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(al, accent_primary(), 0);
    lv_obj_set_width(al, SCR_W - 2 * SIDE_PAD - 20);

    // Separator
    lv_obj_t *sep = lv_obj_create(_bench_area);
    lv_obj_set_size(sep, SCR_W - 2 * SIDE_PAD - 24, 1);
    lv_obj_set_style_bg_color(sep, tc->sep, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    // Per-scene rows
    for (int i = 0; i < count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  %lu FPS",
                 _scenes[i].name, (unsigned long)_scenes[i].fps);
        lv_obj_t *r = lv_label_create(_bench_area);
        lv_label_set_text(r, buf);
        lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(r, tc->card_text, 0);
        lv_obj_set_width(r, SCR_W - 2 * SIDE_PAD - 20);
    }
}

// ── Stop benchmark (and clean up) ──
static void bench_finish(bool show_partial) {
    _bench_running = false;

    // Delete timer first
    if (_bench_tmr) { lv_timer_del(_bench_tmr); _bench_tmr = nullptr; }

    // Restore original monitor callback
    lv_disp_t *d = lv_disp_get_default();
    if (d && d->driver) d->driver->monitor_cb = _orig_monitor;
    _orig_monitor = nullptr;

    if (!_bench_overlay) return;

    // Stop all animations completely before anything else
    clear_area();
    lv_anim_del(_bench_overlay, NULL);
    lv_anim_del(_bench_title, NULL);
    lv_anim_del(_bench_fps_lbl, NULL);
    
    // Force complete refresh to finish all pending draws
    lv_refr_now(NULL);
    delay(50);
    
    if (show_partial) {
        // Show only fully completed scenes
        int completed = (_bench_cur >= BENCH_NUM_SCENES)
                        ? BENCH_NUM_SCENES : _bench_cur;
        if (completed > 0) {
            show_results(completed);
        } else {
            // Nothing completed — just close overlay
            lv_obj_del(_bench_overlay);
            _bench_overlay = nullptr;
            _bench_title   = nullptr;
            _bench_fps_lbl = nullptr;
            _bench_area    = nullptr;
            lv_refr_now(NULL);
            delay(50);
        }
    } else {
        lv_obj_del(_bench_overlay);
        _bench_overlay = nullptr;
        _bench_title   = nullptr;
        _bench_fps_lbl = nullptr;
        _bench_area    = nullptr;
        lv_refr_now(NULL);
        delay(50);
    }
}

// ── Stop button callback ──
static void bench_stop_cb(lv_event_t *e) {
    (void)e;
    bench_finish(true);
}

// ── Scene advance timer (200 ms tick) ──
static void bench_tick_cb(lv_timer_t *t) {
    (void)t;
    if (!_bench_running || !_bench_overlay) {
        bench_finish(false);
        return;
    }

    uint32_t elapsed = millis() - _bench_scene_t0;

    // Update live FPS
    if (_bench_fps_lbl && _scenes[_bench_cur].time_sum > 0) {
        uint32_t fps = _scenes[_bench_cur].refr_cnt * 1000u
                       / _scenes[_bench_cur].time_sum;
        char fb[24];
        snprintf(fb, sizeof(fb), "FPS: %lu", (unsigned long)fps);
        lv_label_set_text(_bench_fps_lbl, fb);
    }

    // Check if current scene time is up
    if (elapsed >= BENCH_SCENE_TIME_MS) {
        _bench_cur++;
        if (_bench_cur >= BENCH_NUM_SCENES) {
            bench_finish(true);  // show_results is called inside bench_finish
            return;
        }
        // Advance to next scene - clean up properly
        clear_area();  // This now stops all animations thoroughly
        _rnd = 12345u + _bench_cur * 7u;  // Reset PRNG per scene
        _scenes[_bench_cur].refr_cnt = 0;
        _scenes[_bench_cur].time_sum = 0;
        
        // Delay to allow complete cleanup before creating new objects
        delay(20);
        
        _scenes[_bench_cur].setup(_bench_area);
        _bench_scene_t0 = millis();

        // Update title
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "%d/%d  %s",
                 _bench_cur + 1, BENCH_NUM_SCENES, _scenes[_bench_cur].name);
        if (_bench_title) lv_label_set_text(_bench_title, tbuf);
    }
}

// ── Public entry point (called from cb_benchmark) ──
void cb_benchmark(lv_event_t *e) {
    (void)e;
    if (_bench_overlay) return;  // already running

    // Reset all scenes
    _rnd = 12345u;
    for (int i = 0; i < BENCH_NUM_SCENES; i++) {
        _scenes[i].refr_cnt = 0;
        _scenes[i].time_sum = 0;
        _scenes[i].fps      = 0;
    }
    _bench_cur = 0;

    // ── Build fullscreen overlay ──
    _bench_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_bench_overlay);
    lv_obj_set_size(_bench_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(_bench_overlay, 0, 0);
    lv_obj_set_style_bg_color(_bench_overlay, tc->scr_bg, 0);
    lv_obj_set_style_bg_opa(_bench_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_bench_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    _bench_title = lv_label_create(_bench_overlay);
    char tbuf[64];
    snprintf(tbuf, sizeof(tbuf), "1/%d  %s", BENCH_NUM_SCENES, _scenes[0].name);
    lv_label_set_text(_bench_title, tbuf);
    lv_obj_set_style_text_font(_bench_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_bench_title, tc->scr_text, 0);
    lv_obj_align(_bench_title, LV_ALIGN_TOP_MID, 0, 8);

    // Live FPS label
    _bench_fps_lbl = lv_label_create(_bench_overlay);
    lv_label_set_text(_bench_fps_lbl, "FPS: --");
    lv_obj_set_style_text_font(_bench_fps_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_bench_fps_lbl, accent_primary(), 0);
    lv_obj_align(_bench_fps_lbl, LV_ALIGN_TOP_MID, 0, 30);

    // Scene area (between FPS label and stop button)
    _bench_area = lv_obj_create(_bench_overlay);
    lv_obj_remove_style_all(_bench_area);
    lv_obj_set_size(_bench_area, SCR_W, SCR_H - 60 - 56);
    lv_obj_set_pos(_bench_area, 0, 60);
    lv_obj_set_style_bg_opa(_bench_area, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(_bench_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(_bench_area, true, 0);

    // Stop button
    lv_obj_t *sb = lv_btn_create(_bench_overlay);
    lv_obj_set_size(sb, 120, 40);
    lv_obj_align(sb, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(sb, lv_color_make(0xCC,0x33,0x33), 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sb, 10, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    lv_obj_set_style_shadow_width(sb, 0, 0);
    lv_obj_add_event_cb(sb, bench_stop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(sb);
    lv_label_set_text(sl, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_center(sl);

    // ── Install monitor callback ──
    lv_disp_t *d = lv_disp_get_default();
    _orig_monitor = d->driver->monitor_cb;
    d->driver->monitor_cb = bench_monitor;

    // ── Start first scene ──
    _scenes[0].setup(_bench_area);
    _bench_scene_t0 = millis();
    _bench_running  = true;
    _bench_tmr      = lv_timer_create(bench_tick_cb, 200, NULL);
}
