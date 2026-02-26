/**
 * @file gui_internal.h
 * @brief Internal shared declarations for all GUI compilation units.
 *        NOT part of the public API - only included by gui cpp files.
 */
#pragma once

#include <lvgl.h>
#include "config.h"
#include "sensor_module/sensor_module.h"
#include <cstring>

// ════════════════════════════════════════════════════════════════════
//  Layout constants
// ════════════════════════════════════════════════════════════════════
static const int SCR_W    = LCD_WIDTH;           // 280
static const int SCR_H    = LCD_HEIGHT;          // 456
static const int STAT_H   = 28;                  // status-bar height
static const int NAV_H    = 54;                  // navigation-bar height
static const int SEP_H    = 1;                   // separator thickness
static const int SIDE_PAD = 12;
static const int BACK_SZ  = 46;                  // back-button touchable size
static const int BTN_W    = SCR_W - 2 * SIDE_PAD;// 256
static const int BTN_H    = 52;
static const int BTN_GAP  = 8;
static const int BTN_RAD  = 12;
static const int ANIM_TIME= 200;

// ════════════════════════════════════════════════════════════════════
//  Theme colour palette
// ════════════════════════════════════════════════════════════════════
struct ThemeColors {
    lv_color_t scr_bg;
    lv_color_t scr_text;
    lv_color_t hdr_bg;
    lv_color_t hdr_text;
    lv_color_t sep;
    lv_color_t card_bg;
    lv_color_t card_text;
    lv_color_t section_text;
    lv_color_t sub_text;
    lv_color_t about_bg;
    lv_color_t about_text;
    lv_color_t diag_bg;
    lv_color_t diag_text;
    lv_color_t back_btn_bg;
    lv_color_t slider_track;
    lv_color_t dd_bg;
    lv_color_t dd_list_bg;
    lv_color_t dd_list_text;
    lv_color_t bar_bg;
    lv_color_t bar_label;
    lv_color_t sw_bg;
};

extern const ThemeColors TC_DARK;
extern const ThemeColors TC_LIGHT;
extern const ThemeColors *tc;

// ════════════════════════════════════════════════════════════════════
//  Accent colour presets
// ════════════════════════════════════════════════════════════════════
struct AccentPreset {
    const char *name;
    lv_color_t primary;
    lv_color_t light;
    lv_color_t dark;
};

#define NUM_ACCENTS 9
extern const AccentPreset ACCENTS[NUM_ACCENTS];
extern uint8_t cfg_accent;

inline lv_color_t accent_primary() { return ACCENTS[cfg_accent].primary; }
inline lv_color_t accent_light()   { return ACCENTS[cfg_accent].light; }
inline lv_color_t accent_dark()    { return ACCENTS[cfg_accent].dark; }
lv_color_t accent_hdr_tint();
const char *accent_dropdown_opts();

// ════════════════════════════════════════════════════════════════════
//  Screen-index enum
// ════════════════════════════════════════════════════════════════════
enum ScrIdx {
    SI_MENU = 0, SI_PRED, SI_LOCAL, SI_TRAIN,
    SI_WEB, SI_SETTINGS, SI_TEST, SI_TEST_SENSORS, SI_TEST_DETAIL,
    SI_COUNT
};

// ════════════════════════════════════════════════════════════════════
//  Screen objects  (defined in gui_api.cpp)
// ════════════════════════════════════════════════════════════════════
extern lv_obj_t *scr_splash;
extern lv_obj_t *scr_menu;
extern lv_obj_t *scr_predict;
extern lv_obj_t *scr_local;
extern lv_obj_t *scr_train;
extern lv_obj_t *scr_web;
extern lv_obj_t *scr_settings;
extern lv_obj_t *scr_test;
extern lv_obj_t *scr_test_sensors;
extern lv_obj_t *scr_test_detail;

// ════════════════════════════════════════════════════════════════════
//  Widget pointers  (defined in gui_api.cpp)
// ════════════════════════════════════════════════════════════════════
extern lv_obj_t *lbl_gesture;
extern lv_obj_t *bar_flex[5];
extern lv_obj_t *bar_hall[5];
extern lv_obj_t *bar_hall_top[5];
extern lv_obj_t *bars_container;

extern lv_obj_t *slider_brightness;
extern lv_obj_t *slider_volume;
extern lv_obj_t *slider_sleep;
extern lv_obj_t *lbl_brt_val;
extern lv_obj_t *lbl_vol_val;
extern lv_obj_t *lbl_slp_val;

extern lv_obj_t *sw_dark_mode;
extern lv_obj_t *dd_fps;
extern lv_obj_t *dd_accent;

extern lv_obj_t *lbl_about;
extern lv_obj_t *lbl_train_stat;

extern lv_obj_t *qr_wifi;
extern lv_obj_t *qr_web;
extern lv_obj_t *lbl_web_stat;
extern bool      web_client_connected;

extern lv_obj_t *lbl_test_detail;
extern lv_obj_t *lbl_test_title;
extern lv_obj_t *test_vol_row;
extern lv_obj_t *slider_test_vol;
extern lv_obj_t *lbl_test_vol_val;
extern lv_obj_t *test_brt_row;
extern lv_obj_t *slider_test_brt;
extern lv_obj_t *lbl_test_brt_val;
extern lv_obj_t *btn_benchmark;

// Sensor test detail bars + labels (for Flex/Hall/Hall Top screens)
extern lv_obj_t *sensor_test_container;
extern lv_obj_t *sensor_test_bars[5];
extern lv_obj_t *sensor_test_lbls[5];

// Calibration dialog widgets
extern lv_obj_t *calib_overlay;
extern lv_obj_t *calib_bar;
extern lv_obj_t *calib_lbl;
extern lv_obj_t *lbl_calib_info;   // Calibration status footer in sensors menu

extern lv_obj_t *bat_label;
extern lv_obj_t *cpu_label;
extern lv_obj_t *stat_bar;

// ════════════════════════════════════════════════════════════════════
//  Styles
// ════════════════════════════════════════════════════════════════════
extern lv_style_t sty_scr;
extern lv_style_t sty_btn;
extern lv_style_t sty_btn_pr;
extern lv_style_t sty_hdr;

// ════════════════════════════════════════════════════════════════════
//  Settings / runtime state
// ════════════════════════════════════════════════════════════════════
extern uint8_t  cfg_volume;
extern uint8_t  cfg_brightness;
extern uint8_t  cfg_sleep_min;
extern bool     cfg_dark_mode;
extern uint8_t  cfg_fps;

extern bool     cfg_local_sensors;
extern bool     cfg_local_words;
extern bool     cfg_local_speech;
extern bool     cfg_back_gesture;

extern float    bat_voltage_v;
extern int      bat_pct_cache;
extern AppMode  cur_gui_mode;
extern int      test_active;

extern const char *test_names[];

// ════════════════════════════════════════════════════════════════════
//  External callbacks
// ════════════════════════════════════════════════════════════════════
extern void (*s_mode_cb)(AppMode);
extern void (*s_test_speaker_cb)();
extern void (*s_test_oled_cb)();
extern void (*s_brightness_cb)(uint8_t);
extern void (*s_volume_cb)(uint8_t);

// ════════════════════════════════════════════════════════════════════
//  Internal functions — theme
// ════════════════════════════════════════════════════════════════════
void init_styles();
void apply_theme();

// ════════════════════════════════════════════════════════════════════
//  Internal functions — NVS
// ════════════════════════════════════════════════════════════════════
void load_settings();
void save_settings();

// ════════════════════════════════════════════════════════════════════
//  Internal functions — helpers
// ════════════════════════════════════════════════════════════════════
lv_obj_t *mk_scr();
lv_obj_t *mk_btn(lv_obj_t *parent, const char *text,
                  int w, int h, lv_event_cb_t cb);
lv_obj_t *mk_nav_btn(lv_obj_t *parent, const char *text,
                      lv_event_cb_t cb);
int       mk_header(lv_obj_t *scr,
                     const char *title, lv_event_cb_t back_cb,
                     lv_obj_t **title_out = nullptr);
lv_obj_t *mk_content(lv_obj_t *scr, int header_h);
void      create_bars(lv_obj_t *scr, lv_obj_t *flex[], lv_obj_t *hall[],
                      lv_obj_t *hall_top[], int y_start);

lv_obj_t *add_slider_row(lv_obj_t *par, const char *icon,
                         const char *label, int32_t min_v, int32_t max_v,
                         int32_t cur, lv_event_cb_t cb,
                         lv_obj_t **val_lbl_out);
void      mk_section(lv_obj_t *par, const char *txt);
lv_obj_t *add_switch_row(lv_obj_t *par, const char *icon,
                         const char *label, bool initial,
                         lv_event_cb_t cb);
lv_obj_t *add_dropdown_row(lv_obj_t *par, const char *icon,
                           const char *label, const char *options,
                           uint16_t sel, lv_event_cb_t cb);

void add_back_gesture(lv_obj_t *scr, lv_event_cb_t back_cb);
void clear_back_gestures();

// ════════════════════════════════════════════════════════════════════
//  Internal functions — screen builders
// ════════════════════════════════════════════════════════════════════
void build_splash();
void build_menu();
void build_predict_menu();
void build_train();
void build_local();
void build_web();
void build_settings();
void build_test();
void build_test_sensors();
void build_test_detail();
void build_status_bar();
void populate_test_detail();
void show_calibration_dialog();
void hide_calibration_dialog();
void update_calibration_progress(int pct);

// ════════════════════════════════════════════════════════════════════
//  Internal functions — callbacks
// ════════════════════════════════════════════════════════════════════
void fire_mode(AppMode m);
void nav_to(lv_obj_t *scr, bool left);

void cb_btn_train(lv_event_t *e);
void cb_btn_predict(lv_event_t *e);
void cb_btn_settings(lv_event_t *e);
void cb_btn_local(lv_event_t *e);
void cb_btn_web(lv_event_t *e);
void cb_btn_tests(lv_event_t *e);

void cb_btn_back_menu(lv_event_t *e);
void cb_btn_back_predict(lv_event_t *e);
void cb_btn_back_tests(lv_event_t *e);
void cb_btn_back_test_detail(lv_event_t *e);

void cb_splash_timer(lv_timer_t *t);

void cb_slider_brightness(lv_event_t *e);
void cb_slider_volume(lv_event_t *e);
void cb_slider_sleep(lv_event_t *e);
void cb_slider_test_vol(lv_event_t *e);
void cb_slider_test_brt(lv_event_t *e);

void cb_dark_mode_switch(lv_event_t *e);
void cb_fps_dropdown(lv_event_t *e);
void cb_accent_dropdown(lv_event_t *e);

void cb_local_sensors(lv_event_t *e);
void cb_local_words(lv_event_t *e);
void cb_local_speech(lv_event_t *e);
void cb_back_gesture_switch(lv_event_t *e);

void cb_test_oled(lv_event_t *e);
void cb_test_mpu(lv_event_t *e);
void cb_test_sensors(lv_event_t *e);
void cb_test_flex(lv_event_t *e);
void cb_test_hall(lv_event_t *e);
void cb_test_hall_top(lv_event_t *e);
void cb_test_battery(lv_event_t *e);
void cb_test_speaker(lv_event_t *e);
void cb_btn_back_test_sensors(lv_event_t *e);

void cb_benchmark(lv_event_t *e);
bool is_bench_running();
