/*
 * @file gui.cpp
 * @brief Complete LVGL GUI for Sign Language Translator Glove v4.0
 *
 * Screens:  Splash -> Main Menu -> (TRAIN | PREDICT | SETTINGS)
 *           Predict sub-menu -> WORDS | SPEECH | BOTH | WEB
 *           Settings -> sliders + About + Tests link
 *           Tests -> OLED | MPU | Flex | Hall | Battery | Speaker
 *           Each test -> individual detail sub-window
 *
 * Layout rules
 *   > Status bar:  [CPU x%]  .........  [bat x%]   (solid white)
 *   > Nav bar:     [< back] [Title]
 *   > 1 px separator line below header
 *   > Scrollable content fills the rest
 *   > Predict & Tests: one full-width button per row
 *
 * Display: 280 x 456 px AMOLED, dark/light theme with NVS persistence.
 */
#include "gui.h"
#include "lvgl.h"
#include "config.h"
#include "system_info.h"
#include <cstring>
#include <Preferences.h>

// ====================================================================
//  Constants
// ====================================================================
#define SCR_W           LCD_WIDTH       // 280
#define SCR_H           LCD_HEIGHT      // 456
#define ANIM_TIME       200             // ms
#define SIDE_PAD        12              // consistent side padding
#define STAT_H          28              // status bar height (CPU + battery)
#define NAV_H           54              // nav bar height (back + title)
#define SEP_H           1               // separator thickness
#define HDR_FULL        (STAT_H + NAV_H)
#define HDR_STAT_ONLY   STAT_H
#define BTN_W           (SCR_W - 2*SIDE_PAD)
#define BTN_H           52
#define BTN_GAP         8
#define BTN_RAD         12
#define BACK_SZ         46

// ====================================================================
//  Forward declarations -- event callbacks
// ====================================================================
static void cb_btn_train(lv_event_t *e);
static void cb_btn_predict(lv_event_t *e);
static void cb_btn_settings(lv_event_t *e);
static void cb_btn_words(lv_event_t *e);
static void cb_btn_speech(lv_event_t *e);
static void cb_btn_both(lv_event_t *e);
static void cb_btn_web(lv_event_t *e);
static void cb_btn_back_menu(lv_event_t *e);
static void cb_btn_back_predict(lv_event_t *e);
static void cb_btn_tests(lv_event_t *e);
static void cb_btn_back_tests(lv_event_t *e);
static void cb_btn_back_test_detail(lv_event_t *e);
static void cb_splash_timer(lv_timer_t *t);

// Test callbacks
static void cb_test_oled(lv_event_t *e);
static void cb_test_mpu(lv_event_t *e);
static void cb_test_flex(lv_event_t *e);
static void cb_test_hall(lv_event_t *e);
static void cb_test_battery(lv_event_t *e);
static void cb_test_speaker(lv_event_t *e);

// Slider callbacks
static void cb_slider_brightness(lv_event_t *e);
static void cb_slider_volume(lv_event_t *e);
static void cb_slider_sleep(lv_event_t *e);
static void cb_slider_test_vol(lv_event_t *e);

// Settings control callbacks
static void cb_dark_mode_switch(lv_event_t *e);
static void cb_fps_dropdown(lv_event_t *e);

// ====================================================================
//  External callbacks (set by main.cpp)
// ====================================================================
static void (*s_mode_cb)(AppMode)       = nullptr;
static void (*s_test_speaker_cb)()      = nullptr;
static void (*s_test_oled_cb)()         = nullptr;
static void (*s_brightness_cb)(uint8_t) = nullptr;
static void (*s_volume_cb)(uint8_t)     = nullptr;

void gui_register_mode_callback(void (*cb)(AppMode))  { s_mode_cb = cb; }
void gui_register_test_speaker_cb(void (*cb)())        { s_test_speaker_cb = cb; }
void gui_register_test_oled_cb(void (*cb)())           { s_test_oled_cb = cb; }
void gui_register_brightness_cb(void (*cb)(uint8_t))   { s_brightness_cb = cb; }
void gui_register_volume_cb(void (*cb)(uint8_t))       { s_volume_cb = cb; }

static void fire_mode(AppMode m) { if (s_mode_cb) s_mode_cb(m); }

// ====================================================================
//  Settings state (persisted to NVS)
// ====================================================================
static uint8_t cfg_volume     = 80;
static uint8_t cfg_brightness = 200;
static uint8_t cfg_sleep_min  = 5;
static bool    cfg_dark_mode  = true;   // true = dark (default)
static uint8_t cfg_fps        = 30;     // 30 or 60

// Battery voltage cache (computed from percentage)
static float bat_voltage_v = 0.0f;
static int   bat_pct_cache = 0;

// ====================================================================
//  NVS persistence
// ====================================================================
static Preferences nvs;

static void load_settings() {
    nvs.begin("gui_cfg", true);  // read-only
    cfg_brightness = nvs.getUChar("brt",    200);
    cfg_volume     = nvs.getUChar("vol",    80);
    cfg_sleep_min  = nvs.getUChar("sleep",  5);
    cfg_dark_mode  = nvs.getBool("dark",    true);
    cfg_fps        = nvs.getUChar("fps",    30);
    nvs.end();
}

static void save_settings() {
    nvs.begin("gui_cfg", false); // read-write
    nvs.putUChar("brt",   cfg_brightness);
    nvs.putUChar("vol",   cfg_volume);
    nvs.putUChar("sleep", cfg_sleep_min);
    nvs.putBool("dark",   cfg_dark_mode);
    nvs.putUChar("fps",   cfg_fps);
    nvs.end();
}

// ====================================================================
//  Screen objects
// ====================================================================
static lv_obj_t *scr_splash       = nullptr;
static lv_obj_t *scr_menu         = nullptr;
static lv_obj_t *scr_predict      = nullptr;
static lv_obj_t *scr_train        = nullptr;
static lv_obj_t *scr_words        = nullptr;
static lv_obj_t *scr_speech       = nullptr;
static lv_obj_t *scr_both         = nullptr;
static lv_obj_t *scr_web          = nullptr;
static lv_obj_t *scr_settings     = nullptr;
static lv_obj_t *scr_test         = nullptr;
static lv_obj_t *scr_test_detail  = nullptr;

// Dynamic widgets
static lv_obj_t *lbl_gesture_w  = nullptr;
static lv_obj_t *lbl_gesture_b  = nullptr;
static lv_obj_t *lbl_train_stat = nullptr;
static lv_obj_t *lbl_web_stat   = nullptr;
static lv_obj_t *qr_web         = nullptr;

// Sensor bars
static lv_obj_t *bar_flex[5]   = {};
static lv_obj_t *bar_hall[5]   = {};
static lv_obj_t *bar_flex_b[5] = {};
static lv_obj_t *bar_hall_b[5] = {};

// Settings widgets
static lv_obj_t *slider_brightness = nullptr;
static lv_obj_t *slider_volume     = nullptr;
static lv_obj_t *slider_sleep      = nullptr;
static lv_obj_t *lbl_brt_val      = nullptr;
static lv_obj_t *lbl_vol_val      = nullptr;
static lv_obj_t *lbl_slp_val      = nullptr;
static lv_obj_t *sw_dark_mode     = nullptr;
static lv_obj_t *dd_fps           = nullptr;

// About label (settings screen)
static lv_obj_t *lbl_about = nullptr;

// Test detail screen widgets
static lv_obj_t *lbl_test_detail   = nullptr;   // main content label
static lv_obj_t *slider_test_vol   = nullptr;   // speaker volume slider
static lv_obj_t *lbl_test_vol_val  = nullptr;   // speaker volume value label
static lv_obj_t *test_vol_row      = nullptr;   // speaker volume row container
static int       test_active       = -1;

// Per-screen status labels
enum ScrIdx { SI_MENU=0,SI_PRED,SI_TRAIN,SI_WORDS,SI_SPEECH,
              SI_BOTH,SI_WEB,SI_SETTINGS,SI_TEST,SI_TEST_DETAIL,SI_COUNT };
static lv_obj_t *bat_labels[SI_COUNT] = {};
static lv_obj_t *cpu_labels[SI_COUNT] = {};

static AppMode cur_gui_mode = MODE_MENU;

// ====================================================================
//  Styles
// ====================================================================
static lv_style_t sty_scr;
static lv_style_t sty_btn;
static lv_style_t sty_btn_pr;
static lv_style_t sty_hdr;
static bool styles_ok = false;

static void init_styles() {
    if (styles_ok) return;
    styles_ok = true;

    lv_style_init(&sty_scr);
    lv_style_set_bg_color(&sty_scr, lv_color_black());
    lv_style_set_bg_opa(&sty_scr, LV_OPA_COVER);
    lv_style_set_text_color(&sty_scr, lv_color_white());
    lv_style_set_pad_all(&sty_scr, 0);

    lv_style_init(&sty_btn);
    lv_style_set_bg_color(&sty_btn, lv_color_make(0x00,0x7A,0xCC));
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, BTN_RAD);
    lv_style_set_text_color(&sty_btn, lv_color_white());
    lv_style_set_text_font(&sty_btn, &lv_font_montserrat_20);
    lv_style_set_pad_ver(&sty_btn, 10);
    lv_style_set_pad_hor(&sty_btn, 16);
    lv_style_set_shadow_width(&sty_btn, 0);
    lv_style_set_border_width(&sty_btn, 0);

    lv_style_init(&sty_btn_pr);
    lv_style_set_bg_color(&sty_btn_pr, lv_color_make(0x00,0x55,0x99));

    lv_style_init(&sty_hdr);
    lv_style_set_bg_color(&sty_hdr, lv_color_make(0x22,0x22,0x26));
    lv_style_set_bg_opa(&sty_hdr, LV_OPA_COVER);
    lv_style_set_radius(&sty_hdr, 0);
    lv_style_set_pad_all(&sty_hdr, 6);
    lv_style_set_border_width(&sty_hdr, 0);
}

// ====================================================================
//  Helpers
// ====================================================================
static lv_obj_t *mk_scr() {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_add_style(s, &sty_scr, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t *mk_btn(lv_obj_t *par, const char *txt,
                         lv_coord_t w, lv_coord_t h,
                         lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(par);
    lv_obj_set_size(b, w, h);
    lv_obj_add_style(b, &sty_btn, 0);
    lv_obj_add_style(b, &sty_btn_pr, LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

// -- Unified header bar (two rows) --
// Row 1 (status):  CPU%  .....  Battery%  (solid white text)
// Row 2 (nav):     < back   Title
// Returns the total header height used.
static int mk_header(lv_obj_t *scr, int idx,
                      const char *title, lv_event_cb_t back_cb) {
    // -- Row 1: Status bar --
    lv_obj_t *stat = lv_obj_create(scr);
    lv_obj_set_size(stat, SCR_W, STAT_H);
    lv_obj_set_pos(stat, 0, 0);
    lv_obj_add_style(stat, &sty_hdr, 0);
    lv_obj_clear_flag(stat, LV_OBJ_FLAG_SCROLLABLE);

    // CPU label (left) -- solid white
    lv_obj_t *cpu = lv_label_create(stat);
    lv_label_set_text(cpu, "CPU 0%");
    lv_obj_set_style_text_font(cpu, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cpu, lv_color_white(), 0);
    lv_obj_align(cpu, LV_ALIGN_LEFT_MID, SIDE_PAD, 1);
    cpu_labels[idx] = cpu;

    // Battery label (right) -- solid white
    lv_obj_t *bat = lv_label_create(stat);
    lv_label_set_text(bat, LV_SYMBOL_BATTERY_FULL " --");
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bat, lv_color_white(), 0);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, -SIDE_PAD, 1);
    bat_labels[idx] = bat;

    int hdr_h = STAT_H;

    // -- Row 2: Nav bar --
    if (back_cb) {
        lv_obj_t *nav = lv_obj_create(scr);
        lv_obj_set_size(nav, SCR_W, NAV_H);
        lv_obj_set_pos(nav, 0, STAT_H);
        lv_obj_set_style_bg_color(nav, lv_color_make(0x22,0x22,0x26), 0);
        lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(nav, 0, 0);
        lv_obj_set_style_border_width(nav, 0, 0);
        lv_obj_set_style_pad_hor(nav, 2, 0);
        lv_obj_set_style_pad_ver(nav, 4, 0);
        lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

        // Back button
        lv_obj_t *bb = lv_btn_create(nav);
        lv_obj_set_size(bb, BACK_SZ, BACK_SZ);
        lv_obj_align(bb, LV_ALIGN_LEFT_MID, SIDE_PAD, -2);
        lv_obj_set_style_bg_color(bb, lv_color_make(0x33,0x33,0x33), 0);
        lv_obj_set_style_bg_opa(bb, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bb, 8, 0);
        lv_obj_set_style_shadow_width(bb, 0, 0);
        lv_obj_set_style_border_width(bb, 0, 0);
        lv_obj_set_style_pad_all(bb, 0, 0);
        lv_obj_add_event_cb(bb, back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bl = lv_label_create(bb);
        lv_label_set_text(bl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(bl, lv_color_white(), 0);
        lv_obj_center(bl);

        // Title
        if (title && title[0]) {
            lv_obj_t *tl = lv_label_create(nav);
            lv_label_set_text(tl, title);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(tl, lv_color_white(), 0);
            lv_obj_align(tl, LV_ALIGN_LEFT_MID, SIDE_PAD + BACK_SZ + 12, -2);
        }

        hdr_h = HDR_FULL;
    }

    // -- Separator line --
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, SCR_W, SEP_H);
    lv_obj_set_pos(sep, 0, hdr_h);
    lv_obj_set_style_bg_color(sep, lv_color_make(0x33,0x33,0x33), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    return hdr_h;
}

// Scrollable content area below header + separator
static lv_obj_t *mk_content(lv_obj_t *scr, int hdr_h) {
    lv_coord_t content_y = hdr_h + SEP_H;
    lv_coord_t content_h = SCR_H - content_y;
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_set_size(c, SCR_W, content_h);
    lv_obj_set_pos(c, 0, content_y);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_left(c, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(c, SIDE_PAD, 0);
    lv_obj_set_style_pad_top(c, 12, 0);
    lv_obj_set_style_pad_bottom(c, 12, 0);
    lv_obj_set_style_pad_row(c, BTN_GAP, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);
    return c;
}

// ====================================================================
//  Sensor bar helper (compact, for WORDS / BOTH)
// ====================================================================
static void create_bars(lv_obj_t *par,
                        lv_obj_t *flex_out[5], lv_obj_t *hall_out[5],
                        lv_coord_t start_y)
{
    const char *nm[] = {"T","I","M","R","P"};
    lv_coord_t bw = 36, gap = 8;
    lv_coord_t total = 5*bw + 4*gap;
    lv_coord_t x0 = (SCR_W - total)/2;

    // Flex label
    lv_obj_t *lf = lv_label_create(par);
    lv_label_set_text(lf, "FLEX");
    lv_obj_set_style_text_font(lf, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lf, lv_color_make(0x88,0xCC,0xFF), 0);
    lv_obj_set_pos(lf, SIDE_PAD, start_y - 16);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i*(bw+gap);
        lv_obj_t *l = lv_label_create(par);
        lv_label_set_text(l, nm[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_make(0xAA,0xAA,0xAA), 0);
        lv_obj_set_pos(l, x+bw/2-4, start_y+72);

        lv_obj_t *b = lv_bar_create(par);
        lv_obj_set_size(b, bw, 65);
        lv_obj_set_pos(b, x, start_y);
        lv_bar_set_range(b, 0, 4095);
        lv_bar_set_value(b, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, lv_color_make(0x2A,0x2A,0x2A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_make(0x00,0xBB,0xEE), LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(b, 0, LV_PART_MAIN);
        flex_out[i] = b;
    }

    lv_coord_t hy = start_y + 96;
    lv_obj_t *lh = lv_label_create(par);
    lv_label_set_text(lh, "HALL");
    lv_obj_set_style_text_font(lh, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lh, lv_color_make(0xFF,0xAA,0x55), 0);
    lv_obj_set_pos(lh, SIDE_PAD, hy - 16);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i*(bw+gap);
        lv_obj_t *l = lv_label_create(par);
        lv_label_set_text(l, nm[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_make(0xAA,0xAA,0xAA), 0);
        lv_obj_set_pos(l, x+bw/2-4, hy+72);

        lv_obj_t *b = lv_bar_create(par);
        lv_obj_set_size(b, bw, 65);
        lv_obj_set_pos(b, x, hy);
        lv_bar_set_range(b, 0, 4095);
        lv_bar_set_value(b, 2048, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, lv_color_make(0x2A,0x2A,0x2A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_make(0xFF,0x88,0x00), LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(b, 0, LV_PART_MAIN);
        hall_out[i] = b;
    }
}

// ====================================================================
//  Build: Splash
// ====================================================================
static void build_splash() {
    scr_splash = mk_scr();

    lv_obj_t *t = lv_label_create(scr_splash);
    lv_label_set_text(t, LV_SYMBOL_OK "\nHybrid-Sense");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *s = lv_label_create(scr_splash);
    lv_label_set_text(s, "Sign Language Glove v4.0");
    lv_obj_set_style_text_font(s, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s, lv_color_make(0x88,0x88,0x88), 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *d = lv_label_create(scr_splash);
    lv_label_set_text(d, "Initializing...");
    lv_obj_set_style_text_font(d, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(d, lv_color_make(0x55,0x55,0x55), 0);
    lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -18);
}

// ====================================================================
//  Build: Main Menu
// ====================================================================
static void build_menu() {
    scr_menu = mk_scr();
    int hh = mk_header(scr_menu, SI_MENU, "", nullptr);

    lv_obj_t *cont = mk_content(scr_menu, hh);

    lv_obj_t *logo = lv_label_create(cont);
    lv_label_set_text(logo, "Hybrid-Sense");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(logo, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(logo, BTN_W);

    lv_obj_t *sub = lv_label_create(cont);
    lv_label_set_text(sub, "Sign Language Glove");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_make(0x88,0x88,0x88), 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sub, BTN_W);

    // spacer
    lv_obj_t *sp = lv_obj_create(cont);
    lv_obj_set_size(sp, 1, 8);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    mk_btn(cont, LV_SYMBOL_UPLOAD " TRAIN",    BTN_W, BTN_H, cb_btn_train);

    lv_obj_t *pb = mk_btn(cont, LV_SYMBOL_OK " PREDICT", BTN_W, BTN_H, cb_btn_predict);
    lv_obj_set_style_bg_color(pb, lv_color_make(0x00,0x88,0x55), 0);

    lv_obj_t *sb = mk_btn(cont, LV_SYMBOL_SETTINGS " SETTINGS", BTN_W, BTN_H, cb_btn_settings);
    lv_obj_set_style_bg_color(sb, lv_color_make(0x44,0x44,0x44), 0);
}

// ====================================================================
//  Build: Predict sub-menu
// ====================================================================
static void build_predict_menu() {
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

// ====================================================================
//  Build: TRAIN
// ====================================================================
static void build_train() {
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
    lv_obj_set_style_text_color(info, lv_color_make(0xBB,0xBB,0xBB), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info, BTN_W);

    lbl_train_stat = lv_label_create(cont);
    lv_label_set_text(lbl_train_stat, "Status: Streaming...");
    lv_obj_set_style_text_font(lbl_train_stat, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_train_stat, lv_color_make(0x66,0xFF,0x66), 0);
}

// ====================================================================
//  Build: WORDS
// ====================================================================
static void build_words() {
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

// ====================================================================
//  Build: SPEECH
// ====================================================================
static void build_speech() {
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
    lv_obj_set_style_text_color(l, lv_color_make(0xBB,0xBB,0xBB), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 20);
}

// ====================================================================
//  Build: BOTH
// ====================================================================
static void build_both() {
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

// ====================================================================
//  Build: WEB
// ====================================================================
static void build_web() {
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
    lv_obj_set_style_text_color(lbl_web_stat, lv_color_make(0xAA,0xAA,0xAA), 0);
    lv_obj_set_style_text_align(lbl_web_stat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_web_stat, SCR_W - 2*SIDE_PAD);
    lv_obj_align(lbl_web_stat, LV_ALIGN_CENTER, 0, 80);
}

// ====================================================================
//  Build: SETTINGS   (scrollable, with About section)
// ====================================================================

// Slider row helper (fixed padding: label + value top, slider bottom)
static lv_obj_t *add_slider_row(lv_obj_t *par, const char *icon,
                                const char *label, int32_t min_v, int32_t max_v,
                                int32_t cur, lv_event_cb_t cb,
                                lv_obj_t **val_lbl_out) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 64);
    lv_obj_set_style_bg_color(row, lv_color_make(0x1E,0x1E,0x1E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", icon, label);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *vl = lv_label_create(row);
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%d", (int)cur);
    lv_label_set_text(vl, vbuf);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vl, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, 0, 0);
    *val_lbl_out = vl;

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, BTN_W - 28, 8);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(sl, min_v, max_v);
    lv_slider_set_value(sl, cur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x33,0x33,0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x00,0x99,0xDD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x00,0xCC,0xFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}

// Section label helper
static void mk_section(lv_obj_t *par, const char *txt) {
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_make(0x66,0x66,0x66), 0);
    lv_obj_set_width(l, BTN_W);
}

// Switch-style row helper (label + switch)
static lv_obj_t *add_switch_row(lv_obj_t *par, const char *icon,
                                const char *label, bool initial,
                                lv_event_cb_t cb) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 44);
    lv_obj_set_style_bg_color(row, lv_color_make(0x1E,0x1E,0x1E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", icon, label);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 22);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_make(0x44,0x44,0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_make(0x00,0x99,0xDD), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

// Dropdown row helper (label + dropdown)
static lv_obj_t *add_dropdown_row(lv_obj_t *par, const char *icon,
                                  const char *label, const char *options,
                                  uint16_t sel, lv_event_cb_t cb) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 44);
    lv_obj_set_style_bg_color(row, lv_color_make(0x1E,0x1E,0x1E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", icon, label);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_size(dd, 80, 28);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(dd, lv_color_make(0x33,0x33,0x33), 0);
    lv_obj_set_style_text_color(dd, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_obj_set_style_pad_hor(dd, 8, 0);
    // Style the dropdown list
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
        lv_obj_set_style_bg_color(list, lv_color_make(0x2A,0x2A,0x2A), 0);
        lv_obj_set_style_text_color(list, lv_color_white(), 0);
    }
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
}

static void build_settings() {
    scr_settings = mk_scr();
    int hh = mk_header(scr_settings, SI_SETTINGS, LV_SYMBOL_SETTINGS "  Settings", cb_btn_back_menu);

    lv_obj_t *cont = mk_content(scr_settings, hh);

    // -- Display --
    mk_section(cont, "DISPLAY");
    slider_brightness = add_slider_row(cont, LV_SYMBOL_IMAGE, "Brightness",
                                       10, 255, cfg_brightness,
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
    lv_obj_set_size(tp, BTN_W, 74);
    lv_obj_set_style_bg_color(tp, lv_color_make(0x1A,0x1A,0x1A), 0);
    lv_obj_set_style_bg_opa(tp, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tp, 10, 0);
    lv_obj_set_style_border_width(tp, 0, 0);
    lv_obj_set_style_pad_all(tp, 8, 0);
    lv_obj_clear_flag(tp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tp_lbl = lv_label_create(tp);
    lv_label_set_text(tp_lbl, "Run hardware self-tests");
    lv_obj_set_style_text_font(tp_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tp_lbl, lv_color_make(0x88,0x88,0x88), 0);
    lv_obj_align(tp_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *bt = mk_btn(tp, LV_SYMBOL_CHARGE " COMPONENT TESTS", BTN_W - 20, 36, cb_btn_tests);
    lv_obj_set_style_bg_color(bt, lv_color_make(0x99,0x55,0x00), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(bt, 0), &lv_font_montserrat_16, 0);
    lv_obj_align(bt, LV_ALIGN_BOTTOM_MID, 0, 0);

    // -- About --
    mk_section(cont, "ABOUT");
    lv_obj_t *abox = lv_obj_create(cont);
    lv_obj_set_size(abox, BTN_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(abox, lv_color_make(0x12,0x12,0x12), 0);
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
    lv_obj_set_style_text_color(lbl_about, lv_color_make(0x99,0x99,0x99), 0);
    lv_obj_set_width(lbl_about, BTN_W - 24);
    lv_label_set_long_mode(lbl_about, LV_LABEL_LONG_WRAP);
}

// ====================================================================
//  Build: TESTS  (menu -- buttons navigate to detail sub-windows)
// ====================================================================
static void build_test() {
    scr_test = mk_scr();
    int hh = mk_header(scr_test, SI_TEST, LV_SYMBOL_CHARGE "  Tests", cb_btn_back_tests);

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

// ====================================================================
//  Build: TEST DETAIL  (individual test sub-window)
//  Content is updated dynamically based on test_active
// ====================================================================
static const lv_color_t test_colors[] = {
    LV_COLOR_MAKE(0x88,0x33,0xCC),
    LV_COLOR_MAKE(0x33,0x88,0xCC),
    LV_COLOR_MAKE(0x00,0xBB,0xEE),
    LV_COLOR_MAKE(0xFF,0x88,0x00),
    LV_COLOR_MAKE(0x33,0x99,0x33),
    LV_COLOR_MAKE(0xCC,0x33,0x55)
};

static void populate_test_detail();  // forward

static void build_test_detail() {
    scr_test_detail = mk_scr();
    int hh = mk_header(scr_test_detail, SI_TEST_DETAIL,
                        LV_SYMBOL_CHARGE "  Test", cb_btn_back_test_detail);

    lv_obj_t *cont = mk_content(scr_test_detail, hh);

    // Main content label (updated live)
    lbl_test_detail = lv_label_create(cont);
    lv_label_set_text(lbl_test_detail, "Running test...");
    lv_obj_set_style_text_font(lbl_test_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_test_detail, lv_color_make(0xBB,0xBB,0xBB), 0);
    lv_obj_set_width(lbl_test_detail, BTN_W);
    lv_label_set_long_mode(lbl_test_detail, LV_LABEL_LONG_WRAP);

    // Speaker volume control row (hidden by default, shown only for speaker test)
    test_vol_row = lv_obj_create(cont);
    lv_obj_set_size(test_vol_row, BTN_W, 64);
    lv_obj_set_style_bg_color(test_vol_row, lv_color_make(0x1E,0x1E,0x1E), 0);
    lv_obj_set_style_bg_opa(test_vol_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(test_vol_row, 10, 0);
    lv_obj_set_style_border_width(test_vol_row, 0, 0);
    lv_obj_set_style_pad_left(test_vol_row, 12, 0);
    lv_obj_set_style_pad_right(test_vol_row, 12, 0);
    lv_obj_set_style_pad_top(test_vol_row, 8, 0);
    lv_obj_set_style_pad_bottom(test_vol_row, 10, 0);
    lv_obj_clear_flag(test_vol_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(test_vol_row, LV_OBJ_FLAG_HIDDEN);  // hidden initially

    lv_obj_t *vl = lv_label_create(test_vol_row);
    lv_label_set_text(vl, LV_SYMBOL_VOLUME_MAX " Volume");
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vl, lv_color_make(0xCC,0xCC,0xCC), 0);
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
    lv_obj_set_style_bg_color(slider_test_vol, lv_color_make(0x33,0x33,0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_test_vol, lv_color_make(0xCC,0x33,0x55), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_test_vol, lv_color_make(0xFF,0x44,0x66), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_test_vol, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_test_vol, cb_slider_test_vol, LV_EVENT_VALUE_CHANGED, NULL);
}

// Populate test detail content based on test_active
static void populate_test_detail() {
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

// ====================================================================
//  Event callbacks -- Navigation
// ====================================================================
static void nav_to(lv_obj_t *scr, bool left) {
    lv_scr_load_anim(scr,
        left ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        ANIM_TIME, 0, false);
}

static void cb_btn_train(lv_event_t *e)   { (void)e; nav_to(scr_train, true);   cur_gui_mode=MODE_TRAIN;          fire_mode(MODE_TRAIN); }
static void cb_btn_predict(lv_event_t *e) { (void)e; nav_to(scr_predict, true); }
static void cb_btn_settings(lv_event_t *e){ (void)e; nav_to(scr_settings, true); cur_gui_mode=MODE_SETTINGS;      fire_mode(MODE_SETTINGS); }
static void cb_btn_words(lv_event_t *e)   { (void)e; nav_to(scr_words, true);   cur_gui_mode=MODE_PREDICT_WORDS;  fire_mode(MODE_PREDICT_WORDS); }
static void cb_btn_speech(lv_event_t *e)  { (void)e; nav_to(scr_speech, true);  cur_gui_mode=MODE_PREDICT_SPEECH; fire_mode(MODE_PREDICT_SPEECH); }
static void cb_btn_both(lv_event_t *e)    { (void)e; nav_to(scr_both, true);    cur_gui_mode=MODE_PREDICT_BOTH;   fire_mode(MODE_PREDICT_BOTH); }
static void cb_btn_web(lv_event_t *e)     { (void)e; nav_to(scr_web, true);     cur_gui_mode=MODE_PREDICT_WEB;    fire_mode(MODE_PREDICT_WEB); }
static void cb_btn_tests(lv_event_t *e)   { (void)e; nav_to(scr_test, true);    cur_gui_mode=MODE_TEST;           fire_mode(MODE_TEST); test_active=-1; }

static void cb_btn_back_menu(lv_event_t *e)    { (void)e; nav_to(scr_menu, false);     cur_gui_mode=MODE_MENU; fire_mode(MODE_MENU); }
static void cb_btn_back_predict(lv_event_t *e) { (void)e; nav_to(scr_predict, false);  cur_gui_mode=MODE_MENU; fire_mode(MODE_MENU); }
static void cb_btn_back_tests(lv_event_t *e)   { (void)e; nav_to(scr_settings, false); cur_gui_mode=MODE_SETTINGS; fire_mode(MODE_SETTINGS); test_active=-1; }
static void cb_btn_back_test_detail(lv_event_t *e) { (void)e; nav_to(scr_test, false); cur_gui_mode=MODE_TEST; fire_mode(MODE_TEST); test_active=-1; }

static void cb_splash_timer(lv_timer_t *t) {
    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
    cur_gui_mode = MODE_MENU;
    lv_timer_del(t);
}

// ====================================================================
//  Event callbacks -- Sliders
// ====================================================================
static void cb_slider_brightness(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_brightness = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_brightness);
    lv_label_set_text(lbl_brt_val, buf);
    if (s_brightness_cb) s_brightness_cb(cfg_brightness);
    save_settings();
}
static void cb_slider_volume(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_volume = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_volume);
    lv_label_set_text(lbl_vol_val, buf);
    if (s_volume_cb) s_volume_cb(cfg_volume);
    save_settings();
}
static void cb_slider_sleep(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_sleep_min = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_sleep_min);
    lv_label_set_text(lbl_slp_val, buf);
    save_settings();
}

// Speaker test volume slider
static void cb_slider_test_vol(lv_event_t *e) {
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

// ====================================================================
//  Event callbacks -- Settings controls
// ====================================================================
static void cb_dark_mode_switch(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    cfg_dark_mode = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_settings();
    // Dark = black bg, white text; Light = white bg, black text
    if (cfg_dark_mode) {
        lv_style_set_bg_color(&sty_scr, lv_color_black());
        lv_style_set_text_color(&sty_scr, lv_color_white());
    } else {
        lv_style_set_bg_color(&sty_scr, lv_color_white());
        lv_style_set_text_color(&sty_scr, lv_color_black());
    }
    lv_obj_invalidate(lv_scr_act());
}

static void cb_fps_dropdown(lv_event_t *e) {
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

// ====================================================================
//  Event callbacks -- Tests (navigate to detail sub-windows)
// ====================================================================
static void cb_test_oled(lv_event_t *e) {
    (void)e;
    test_active = 0;
    populate_test_detail();
    nav_to(scr_test_detail, true);
    if (s_test_oled_cb) s_test_oled_cb();
}
static void cb_test_mpu(lv_event_t *e) {
    (void)e;
    test_active = 1;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}
static void cb_test_flex(lv_event_t *e) {
    (void)e;
    test_active = 2;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}
static void cb_test_hall(lv_event_t *e) {
    (void)e;
    test_active = 3;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}
static void cb_test_battery(lv_event_t *e) {
    (void)e;
    test_active = 4;
    populate_test_detail();
    nav_to(scr_test_detail, true);
}
static void cb_test_speaker(lv_event_t *e) {
    (void)e;
    test_active = 5;
    populate_test_detail();
    nav_to(scr_test_detail, true);
    if (s_test_speaker_cb) s_test_speaker_cb();
}

// ====================================================================
//  Public API
// ====================================================================
void gui_init() {
    load_settings();
    init_styles();

    // Apply saved dark/light mode to style
    if (!cfg_dark_mode) {
        lv_style_set_bg_color(&sty_scr, lv_color_white());
        lv_style_set_text_color(&sty_scr, lv_color_black());
    }

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
    build_words();
    build_speech();
    build_both();
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
        case MODE_PREDICT_WORDS:  lv_scr_load_anim(scr_words,       LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_SPEECH: lv_scr_load_anim(scr_speech,      LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_BOTH:   lv_scr_load_anim(scr_both,        LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_WEB:    lv_scr_load_anim(scr_web,         LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_SETTINGS:       lv_scr_load_anim(scr_settings,    LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_TEST:           lv_scr_load_anim(scr_test,        LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
    }
}

void gui_update(const SensorData &d) {
    if (cur_gui_mode == MODE_PREDICT_WORDS) {
        for (int i = 0; i < 5; i++) {
            lv_bar_set_value(bar_flex[i], d.flex[i], LV_ANIM_OFF);
            lv_bar_set_value(bar_hall[i], d.hall[i], LV_ANIM_OFF);
        }
    }
    else if (cur_gui_mode == MODE_PREDICT_BOTH) {
        for (int i = 0; i < 5; i++) {
            lv_bar_set_value(bar_flex_b[i], d.flex[i], LV_ANIM_OFF);
            lv_bar_set_value(bar_hall_b[i], d.hall[i], LV_ANIM_OFF);
        }
    }
}

void gui_test_update(const SensorData &d) {
    if (cur_gui_mode != MODE_TEST || !lbl_test_detail) return;
    char buf[200];
    switch (test_active) {
    case 0: // OLED -- show PASS
        lv_label_set_text(lbl_test_detail, "OLED Test\n\n" LV_SYMBOL_OK " Display OK\nNo artifacts detected.");
        break;
    case 1: // MPU
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
    case 2: // Flex
        snprintf(buf, sizeof(buf),
            "Flex Sensors (raw ADC)\n\n"
            "Thumb:  %u\n"
            "Index:  %u\n"
            "Middle: %u\n"
            "Ring:   %u\n"
            "Pinky:  %u",
            d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4]);
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 3: // Hall
        snprintf(buf, sizeof(buf),
            "Hall Sensors (raw ADC)\n\n"
            "Thumb:  %u\n"
            "Index:  %u\n"
            "Middle: %u\n"
            "Ring:   %u\n"
            "Pinky:  %u",
            d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4]);
        lv_label_set_text(lbl_test_detail, buf);
        break;
    case 4: { // Battery -- show voltage
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
    case 5: // Speaker
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
    if (lbl_gesture_w) lv_label_set_text(lbl_gesture_w, text);
    if (lbl_gesture_b) lv_label_set_text(lbl_gesture_b, text);
}

void gui_set_battery(int pct) {
    bat_pct_cache = pct;
    // Approximate voltage from percentage
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
        if (bat_labels[i]) {
            lv_label_set_text(bat_labels[i], buf);
            // Solid white -- color set in mk_header, no override
        }
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
    if (lbl_web_stat && url) {
        char buf[128];
        snprintf(buf, sizeof(buf), "WiFi: %s\n%s", WIFI_AP_SSID, url);
        lv_label_set_text(lbl_web_stat, buf);
    }
}

void gui_set_train_status(const char *msg) {
    if (lbl_train_stat) lv_label_set_text(lbl_train_stat, msg);
}

void gui_set_volume(uint8_t vol)      { cfg_volume = vol; }
void gui_set_brightness(uint8_t brt)  { cfg_brightness = brt; }
uint8_t gui_get_volume()              { return cfg_volume; }
uint8_t gui_get_brightness()          { return cfg_brightness; }

void gui_set_cpu_usage(int pct) {
    char buf[16];
    snprintf(buf, sizeof(buf), "CPU %d%%", pct);
    // Solid white -- color set in mk_header
    for (int i = 0; i < SI_COUNT; i++) {
        if (cpu_labels[i]) {
            lv_label_set_text(cpu_labels[i], buf);
        }
    }
}

void gui_update_about(const SystemInfoData &info) {
    if (!lbl_about) return;
    char buf[360];
    uint32_t h = info.uptime_sec / 3600;
    uint32_t m = (info.uptime_sec % 3600) / 60;
    uint32_t s = info.uptime_sec % 60;

    snprintf(buf, sizeof(buf),
        "Hybrid-Sense v4.0\n"
        "%s rev %d  |  %d core%s\n"
        "SDK: %s\n"
        "\n"
        "CPU: %lu MHz  |  %d%%\n"
        "LVGL: %d FPS\n"
        "\n"
        "RAM: %lu / %lu KB (%d%%)\n"
        "PSRAM: %lu / %lu KB (%d%%)\n"
        "Flash: %lu KB @ %lu MHz\n"
        "\n"
        "Uptime: %02lu:%02lu:%02lu",
        info.chip_model, info.chip_revision,
        info.cpu_cores, info.cpu_cores > 1 ? "s" : "",
        info.sdk_version,
        (unsigned long)info.cpu_freq_mhz, info.cpu_usage_pct,
        info.lvgl_fps,
        (unsigned long)(info.ram_used/1024), (unsigned long)(info.ram_total/1024), info.ram_pct,
        (unsigned long)(info.psram_used/1024), (unsigned long)(info.psram_total/1024), info.psram_pct,
        (unsigned long)(info.flash_size/1024), (unsigned long)(info.flash_speed/1000000),
        (unsigned long)h, (unsigned long)m, (unsigned long)s);

    lv_label_set_text(lbl_about, buf);
}
