/*
 * @file gui.cpp
 * @brief Complete LVGL GUI for Sign Language Translator Glove v4.0
 *
 * Screens:  Splash → Main Menu → (TRAIN | PREDICT | SETTINGS)
 *           Predict sub-menu → WORDS | SPEECH | BOTH | WEB
 *           Settings → sliders + Tests sub-screen
 *           Tests → OLED | MPU | Flex | Hall | Battery | Speaker
 *
 * Display: 280 × 456 px AMOLED (reversed portrait), dark theme.
 */
#include "gui.h"
#include "lvgl.h"
#include "config.h"
#include <cstring>

// ════════════════════════════════════════════════════════════════════
//  Constants
// ════════════════════════════════════════════════════════════════════
#define SCR_W           LCD_WIDTH       // 280
#define SCR_H           LCD_HEIGHT      // 456
#define ANIM_TIME       200             // ms – snappy transitions
#define TOPBAR_H        28
#define BTN_W           240
#define BTN_H           56
#define BTN_GAP         10
#define BTN_RAD         12

// ════════════════════════════════════════════════════════════════════
//  Forward declarations — event callbacks
// ════════════════════════════════════════════════════════════════════
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
static void cb_splash_timer(lv_timer_t *t);

// Test buttons
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

// ════════════════════════════════════════════════════════════════════
//  External callbacks (set by main.cpp)
// ════════════════════════════════════════════════════════════════════
static void (*s_mode_cb)(AppMode)   = nullptr;
static void (*s_test_speaker_cb)()  = nullptr;
static void (*s_test_oled_cb)()     = nullptr;
static void (*s_brightness_cb)(uint8_t) = nullptr;
static void (*s_volume_cb)(uint8_t)     = nullptr;

void gui_register_mode_callback(void (*cb)(AppMode))      { s_mode_cb = cb; }
void gui_register_test_speaker_cb(void (*cb)())            { s_test_speaker_cb = cb; }
void gui_register_test_oled_cb(void (*cb)())               { s_test_oled_cb = cb; }
void gui_register_brightness_cb(void (*cb)(uint8_t))       { s_brightness_cb = cb; }
void gui_register_volume_cb(void (*cb)(uint8_t))           { s_volume_cb = cb; }

static void fire_mode(AppMode m) { if (s_mode_cb) s_mode_cb(m); }

// ════════════════════════════════════════════════════════════════════
//  Settings state
// ════════════════════════════════════════════════════════════════════
static uint8_t cfg_volume     = 80;    // 0-100
static uint8_t cfg_brightness = 200;   // 0-255
static uint8_t cfg_sleep_min  = 5;     // minutes

// ════════════════════════════════════════════════════════════════════
//  Screen objects
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *scr_splash    = nullptr;
static lv_obj_t *scr_menu      = nullptr;
static lv_obj_t *scr_predict   = nullptr;
static lv_obj_t *scr_train     = nullptr;
static lv_obj_t *scr_words     = nullptr;
static lv_obj_t *scr_speech    = nullptr;
static lv_obj_t *scr_both      = nullptr;
static lv_obj_t *scr_web       = nullptr;
static lv_obj_t *scr_settings  = nullptr;
static lv_obj_t *scr_test      = nullptr;

// Dynamic widgets
static lv_obj_t *lbl_gesture_w  = nullptr;
static lv_obj_t *lbl_gesture_b  = nullptr;
static lv_obj_t *lbl_train_stat = nullptr;
static lv_obj_t *lbl_web_stat   = nullptr;
static lv_obj_t *qr_web         = nullptr;

// Sensor bars (WORDS)
static lv_obj_t *bar_flex[5]    = {};
static lv_obj_t *bar_hall[5]    = {};
// Sensor bars (BOTH)
static lv_obj_t *bar_flex_b[5]  = {};
static lv_obj_t *bar_hall_b[5]  = {};

// Settings widgets
static lv_obj_t *slider_brightness = nullptr;
static lv_obj_t *slider_volume     = nullptr;
static lv_obj_t *slider_sleep      = nullptr;
static lv_obj_t *lbl_brt_val      = nullptr;
static lv_obj_t *lbl_vol_val      = nullptr;
static lv_obj_t *lbl_slp_val      = nullptr;

// Test screen live labels
static lv_obj_t *lbl_test_result  = nullptr;
static int       test_active      = -1;       // which test panel is visible

// Battery + CPU labels (one per screen)
#define NUM_SCREENS 10
static lv_obj_t *bat_labels[NUM_SCREENS] = {};
static lv_obj_t *cpu_labels[NUM_SCREENS] = {};
enum ScreenIdx { SI_MENU=0, SI_PRED, SI_TRAIN, SI_WORDS, SI_SPEECH,
                 SI_BOTH, SI_WEB, SI_SETTINGS, SI_TEST, SI_COUNT };

static AppMode cur_gui_mode = MODE_MENU;

// ════════════════════════════════════════════════════════════════════
//  Styles (initialized once)
// ════════════════════════════════════════════════════════════════════
static lv_style_t sty_scr;
static lv_style_t sty_btn;
static lv_style_t sty_btn_pr;
static lv_style_t sty_topbar;
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
    lv_style_set_bg_color(&sty_btn, lv_color_make(0x00, 0x7A, 0xCC));
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, BTN_RAD);
    lv_style_set_text_color(&sty_btn, lv_color_white());
    lv_style_set_text_font(&sty_btn, &lv_font_montserrat_20);
    lv_style_set_pad_ver(&sty_btn, 12);
    lv_style_set_pad_hor(&sty_btn, 16);
    lv_style_set_shadow_width(&sty_btn, 0);
    lv_style_set_border_width(&sty_btn, 0);

    lv_style_init(&sty_btn_pr);
    lv_style_set_bg_color(&sty_btn_pr, lv_color_make(0x00, 0x55, 0x99));

    lv_style_init(&sty_topbar);
    lv_style_set_bg_color(&sty_topbar, lv_color_make(0x18, 0x18, 0x18));
    lv_style_set_bg_opa(&sty_topbar, LV_OPA_COVER);
    lv_style_set_radius(&sty_topbar, 0);
    lv_style_set_pad_all(&sty_topbar, 4);
    lv_style_set_border_width(&sty_topbar, 0);
}

// ════════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════════
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

static lv_obj_t *mk_bat(lv_obj_t *scr, int idx) {
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SCR_W, TOPBAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(bar, &sty_topbar, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // CPU label (left side)
    lv_obj_t *cpu = lv_label_create(bar);
    lv_label_set_text(cpu, "CPU 0%");
    lv_obj_set_style_text_font(cpu, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cpu, lv_color_make(0xAA, 0xAA, 0xAA), 0);
    lv_obj_align(cpu, LV_ALIGN_LEFT_MID, 6, 0);
    cpu_labels[idx] = cpu;

    // Battery label (right side)
    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, LV_SYMBOL_BATTERY_FULL " 100%");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0x66, 0xFF, 0x66), 0);
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -6, 0);
    bat_labels[idx] = lbl;
    return lbl;
}

static lv_obj_t *mk_back(lv_obj_t *scr, lv_event_cb_t cb) {
    lv_obj_t *b = mk_btn(scr, LV_SYMBOL_LEFT " Back", 120, 40, cb);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(b, lv_color_make(0x44, 0x44, 0x44), 0);
    return b;
}

static lv_obj_t *mk_title(lv_obj_t *scr, const char *txt, lv_coord_t y) {
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

// Scrollable content panel that sits below the top bar (used in settings/test)
// (removed: using inline layout in build functions instead)

// ════════════════════════════════════════════════════════════════════
//  Sensor bar helper (compact, for WORDS / BOTH)
// ════════════════════════════════════════════════════════════════════
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
    lv_obj_set_style_text_font(lf, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lf, lv_color_make(0x88,0xCC,0xFF), 0);
    lv_obj_set_pos(lf, 8, start_y - 16);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i*(bw+gap);
        lv_obj_t *l = lv_label_create(par);
        lv_label_set_text(l, nm[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
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
        lv_obj_set_style_anim_time(b, 0, LV_PART_MAIN);  // instant updates
        flex_out[i] = b;
    }

    lv_coord_t hy = start_y + 96;
    lv_obj_t *lh = lv_label_create(par);
    lv_label_set_text(lh, "HALL");
    lv_obj_set_style_text_font(lh, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lh, lv_color_make(0xFF,0xAA,0x55), 0);
    lv_obj_set_pos(lh, 8, hy - 16);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i*(bw+gap);
        lv_obj_t *l = lv_label_create(par);
        lv_label_set_text(l, nm[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
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

// ════════════════════════════════════════════════════════════════════
//  Build: Splash
// ════════════════════════════════════════════════════════════════════
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
    lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d, lv_color_make(0x55,0x55,0x55), 0);
    lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -18);
}

// ════════════════════════════════════════════════════════════════════
//  Build: Main Menu  (project name as title)
// ════════════════════════════════════════════════════════════════════
static void build_menu() {
    scr_menu = mk_scr();
    mk_bat(scr_menu, SI_MENU);

    // Project name
    lv_obj_t *t = lv_label_create(scr_menu);
    lv_label_set_text(t, "Hybrid-Sense");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *sub = lv_label_create(scr_menu);
    lv_label_set_text(sub, "Sign Language Glove");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_make(0x88,0x88,0x88), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 74);

    // Three main buttons stacked
    lv_coord_t y0 = 110;

    lv_obj_t *b1 = mk_btn(scr_menu, LV_SYMBOL_UPLOAD " TRAIN", BTN_W, BTN_H, cb_btn_train);
    lv_obj_align(b1, LV_ALIGN_TOP_MID, 0, y0);

    lv_obj_t *b2 = mk_btn(scr_menu, LV_SYMBOL_EYE_OPEN " PREDICT", BTN_W, BTN_H, cb_btn_predict);
    lv_obj_align(b2, LV_ALIGN_TOP_MID, 0, y0 + BTN_H + BTN_GAP);

    lv_obj_t *b3 = mk_btn(scr_menu, LV_SYMBOL_SETTINGS " SETTINGS", BTN_W, BTN_H, cb_btn_settings);
    lv_obj_align(b3, LV_ALIGN_TOP_MID, 0, y0 + 2*(BTN_H + BTN_GAP));
    lv_obj_set_style_bg_color(b3, lv_color_make(0x44,0x44,0x44), 0);
}

// ════════════════════════════════════════════════════════════════════
//  Build: Predict sub-menu
// ════════════════════════════════════════════════════════════════════
static void build_predict_menu() {
    scr_predict = mk_scr();
    mk_bat(scr_predict, SI_PRED);
    mk_title(scr_predict, "PREDICT MODE", 40);

    lv_coord_t bw = 120, bh = 54;
    lv_coord_t y0 = 84;

    lv_obj_t *b1 = mk_btn(scr_predict, "WORDS",  bw, bh, cb_btn_words);
    lv_obj_align(b1, LV_ALIGN_TOP_MID, -(bw/2+4), y0);

    lv_obj_t *b2 = mk_btn(scr_predict, "SPEECH", bw, bh, cb_btn_speech);
    lv_obj_align(b2, LV_ALIGN_TOP_MID,  (bw/2+4), y0);

    lv_obj_t *b3 = mk_btn(scr_predict, "BOTH",   bw, bh, cb_btn_both);
    lv_obj_align(b3, LV_ALIGN_TOP_MID, -(bw/2+4), y0+bh+BTN_GAP);

    lv_obj_t *b4 = mk_btn(scr_predict, "WEB",    bw, bh, cb_btn_web);
    lv_obj_align(b4, LV_ALIGN_TOP_MID,  (bw/2+4), y0+bh+BTN_GAP);
    lv_obj_set_style_bg_color(b4, lv_color_make(0x00,0x99,0x55), 0);

    mk_back(scr_predict, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Build: TRAIN
// ════════════════════════════════════════════════════════════════════
static void build_train() {
    scr_train = mk_scr();
    mk_bat(scr_train, SI_TRAIN);
    mk_title(scr_train, LV_SYMBOL_UPLOAD " TRAIN", 40);

    lv_obj_t *info = lv_label_create(scr_train);
    lv_label_set_text(info,
        "Connect Edge Impulse\n"
        "Data Forwarder via USB.\n\n"
        "Sensor data streams over\n"
        "Serial at 115200 baud.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, lv_color_make(0xBB,0xBB,0xBB), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info, 260);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -20);

    lbl_train_stat = lv_label_create(scr_train);
    lv_label_set_text(lbl_train_stat, "Status: Streaming...");
    lv_obj_set_style_text_font(lbl_train_stat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_train_stat, lv_color_make(0x66,0xFF,0x66), 0);
    lv_obj_align(lbl_train_stat, LV_ALIGN_CENTER, 0, 60);

    mk_back(scr_train, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Build: WORDS
// ════════════════════════════════════════════════════════════════════
static void build_words() {
    scr_words = mk_scr();
    mk_bat(scr_words, SI_WORDS);

    lbl_gesture_w = lv_label_create(scr_words);
    lv_label_set_text(lbl_gesture_w, "---");
    lv_obj_set_style_text_font(lbl_gesture_w, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_w, lv_color_make(0x00,0xFF,0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_w, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_w, 260);
    lv_obj_align(lbl_gesture_w, LV_ALIGN_TOP_MID, 0, 36);

    create_bars(scr_words, bar_flex, bar_hall, 76);
    mk_back(scr_words, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Build: SPEECH
// ════════════════════════════════════════════════════════════════════
static void build_speech() {
    scr_speech = mk_scr();
    mk_bat(scr_speech, SI_SPEECH);

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

    mk_back(scr_speech, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Build: BOTH
// ════════════════════════════════════════════════════════════════════
static void build_both() {
    scr_both = mk_scr();
    mk_bat(scr_both, SI_BOTH);

    lbl_gesture_b = lv_label_create(scr_both);
    lv_label_set_text(lbl_gesture_b, "---");
    lv_obj_set_style_text_font(lbl_gesture_b, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_b, lv_color_make(0x00,0xFF,0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_b, 260);
    lv_obj_align(lbl_gesture_b, LV_ALIGN_TOP_MID, 0, 36);

    create_bars(scr_both, bar_flex_b, bar_hall_b, 76);

    lv_obj_t *spk = lv_label_create(scr_both);
    lv_label_set_text(spk, LV_SYMBOL_VOLUME_MAX " Audio ON");
    lv_obj_set_style_text_font(spk, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spk, lv_color_make(0xFF,0xCC,0x00), 0);
    lv_obj_align(spk, LV_ALIGN_BOTTOM_MID, 0, -56);

    mk_back(scr_both, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Build: WEB
// ════════════════════════════════════════════════════════════════════
static void build_web() {
    scr_web = mk_scr();
    mk_bat(scr_web, SI_WEB);
    mk_title(scr_web, LV_SYMBOL_WIFI " WEB MODE", 40);

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
    lv_obj_set_style_text_font(lbl_web_stat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_web_stat, lv_color_make(0xAA,0xAA,0xAA), 0);
    lv_obj_set_style_text_align(lbl_web_stat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_web_stat, 260);
    lv_obj_align(lbl_web_stat, LV_ALIGN_CENTER, 0, 76);

    mk_back(scr_web, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Build: SETTINGS
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *add_slider_row(lv_obj_t *par, const char *icon,
                                const char *label, int32_t min_v, int32_t max_v,
                                int32_t cur, lv_event_cb_t cb,
                                lv_obj_t **val_lbl_out) {
    // Container row
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, SCR_W - 24, 70);
    lv_obj_set_style_bg_color(row, lv_color_make(0x1E,0x1E,0x1E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Icon + label
    lv_obj_t *lbl = lv_label_create(row);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", icon, label);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    // Value label
    lv_obj_t *vl = lv_label_create(row);
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%d", (int)cur);
    lv_label_set_text(vl, vbuf);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vl, lv_color_make(0x00,0xCC,0xFF), 0);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, 0, 0);
    *val_lbl_out = vl;

    // Slider
    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, SCR_W - 56, 8);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(sl, min_v, max_v);
    lv_slider_set_value(sl, cur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x33,0x33,0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x00,0x99,0xDD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_make(0x00,0xCC,0xFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}

static void build_settings() {
    scr_settings = mk_scr();
    mk_bat(scr_settings, SI_SETTINGS);
    mk_title(scr_settings, LV_SYMBOL_SETTINGS " Settings", 40);

    // Scrollable content area below title
    lv_obj_t *cont = lv_obj_create(scr_settings);
    lv_obj_set_size(cont, SCR_W, SCR_H - 80 - 52);  // leave room for topbar+title & back btn
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 8, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    // Brightness slider
    slider_brightness = add_slider_row(cont, LV_SYMBOL_IMAGE, "Brightness",
                                       10, 255, cfg_brightness,
                                       cb_slider_brightness, &lbl_brt_val);

    // Volume slider
    slider_volume = add_slider_row(cont, LV_SYMBOL_VOLUME_MAX, "Volume",
                                   0, 100, cfg_volume,
                                   cb_slider_volume, &lbl_vol_val);

    // Auto-sleep slider (minutes)
    slider_sleep = add_slider_row(cont, LV_SYMBOL_POWER, "Auto-sleep (min)",
                                  1, 30, cfg_sleep_min,
                                  cb_slider_sleep, &lbl_slp_val);

    // Tests button
    lv_obj_t *bt = mk_btn(cont, LV_SYMBOL_CHARGE " Component Tests", SCR_W-40, 50, cb_btn_tests);
    lv_obj_set_style_bg_color(bt, lv_color_make(0x99,0x55,0x00), 0);

    mk_back(scr_settings, cb_btn_back_menu);
}

// ════════════════════════════════════════════════════════════════════
//  Build: TESTS
// ════════════════════════════════════════════════════════════════════
static void build_test() {
    scr_test = mk_scr();
    mk_bat(scr_test, SI_TEST);
    mk_title(scr_test, LV_SYMBOL_CHARGE " Tests", 40);

    lv_obj_t *cont = lv_obj_create(scr_test);
    lv_obj_set_size(cont, SCR_W, SCR_H - 80 - 52);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    lv_coord_t bw2 = 120, bh2 = 46;

    // Row 1: OLED + MPU
    lv_obj_t *r1 = lv_obj_create(cont);
    lv_obj_set_size(r1, SCR_W - 24, bh2 + 8);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    lv_obj_clear_flag(r1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tb1 = mk_btn(r1, LV_SYMBOL_IMAGE " OLED", bw2, bh2, cb_test_oled);
    lv_obj_align(tb1, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb1, lv_color_make(0x88,0x33,0xCC), 0);

    lv_obj_t *tb2 = mk_btn(r1, LV_SYMBOL_LOOP " MPU", bw2, bh2, cb_test_mpu);
    lv_obj_align(tb2, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb2, lv_color_make(0x33,0x88,0xCC), 0);

    // Row 2: Flex + Hall
    lv_obj_t *r2 = lv_obj_create(cont);
    lv_obj_set_size(r2, SCR_W - 24, bh2 + 8);
    lv_obj_set_style_bg_opa(r2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2, 0, 0);
    lv_obj_set_style_pad_all(r2, 0, 0);
    lv_obj_clear_flag(r2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tb3 = mk_btn(r2, LV_SYMBOL_MINUS " Flex", bw2, bh2, cb_test_flex);
    lv_obj_align(tb3, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb3, lv_color_make(0x00,0x88,0xAA), 0);

    lv_obj_t *tb4 = mk_btn(r2, LV_SYMBOL_GPS " Hall", bw2, bh2, cb_test_hall);
    lv_obj_align(tb4, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb4, lv_color_make(0xCC,0x66,0x00), 0);

    // Row 3: Battery + Speaker
    lv_obj_t *r3 = lv_obj_create(cont);
    lv_obj_set_size(r3, SCR_W - 24, bh2 + 8);
    lv_obj_set_style_bg_opa(r3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r3, 0, 0);
    lv_obj_set_style_pad_all(r3, 0, 0);
    lv_obj_clear_flag(r3, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tb5 = mk_btn(r3, LV_SYMBOL_BATTERY_FULL " Battery", bw2, bh2, cb_test_battery);
    lv_obj_align(tb5, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb5, lv_color_make(0x33,0x99,0x33), 0);

    lv_obj_t *tb6 = mk_btn(r3, LV_SYMBOL_VOLUME_MAX " Speaker", bw2, bh2, cb_test_speaker);
    lv_obj_align(tb6, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(tb6, lv_color_make(0xCC,0x33,0x55), 0);

    // Result area (live data)
    lv_obj_t *res_box = lv_obj_create(cont);
    lv_obj_set_size(res_box, SCR_W - 24, 120);
    lv_obj_set_style_bg_color(res_box, lv_color_make(0x12,0x12,0x12), 0);
    lv_obj_set_style_bg_opa(res_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(res_box, 10, 0);
    lv_obj_set_style_border_width(res_box, 0, 0);
    lv_obj_set_style_pad_all(res_box, 8, 0);
    lv_obj_clear_flag(res_box, LV_OBJ_FLAG_SCROLLABLE);

    lbl_test_result = lv_label_create(res_box);
    lv_label_set_text(lbl_test_result, "Tap a test button above\nto see live results.");
    lv_obj_set_style_text_font(lbl_test_result, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0x88,0x88,0x88), 0);
    lv_obj_set_width(lbl_test_result, SCR_W - 48);
    lv_label_set_long_mode(lbl_test_result, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_test_result, LV_ALIGN_TOP_LEFT, 0, 0);

    mk_back(scr_test, cb_btn_back_tests);
}

// ════════════════════════════════════════════════════════════════════
//  Event callbacks — Navigation
// ════════════════════════════════════════════════════════════════════
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

static void cb_splash_timer(lv_timer_t *t) {
    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
    cur_gui_mode = MODE_MENU;
    lv_timer_del(t);
}

// ════════════════════════════════════════════════════════════════════
//  Event callbacks — Sliders
// ════════════════════════════════════════════════════════════════════
static void cb_slider_brightness(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_brightness = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_brightness);
    lv_label_set_text(lbl_brt_val, buf);
    if (s_brightness_cb) s_brightness_cb(cfg_brightness);
}
static void cb_slider_volume(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_volume = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_volume);
    lv_label_set_text(lbl_vol_val, buf);
    if (s_volume_cb) s_volume_cb(cfg_volume);
}
static void cb_slider_sleep(lv_event_t *e) {
    lv_obj_t *sl = lv_event_get_target(e);
    cfg_sleep_min = (uint8_t)lv_slider_get_value(sl);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg_sleep_min);
    lv_label_set_text(lbl_slp_val, buf);
}

// ════════════════════════════════════════════════════════════════════
//  Event callbacks — Tests
// ════════════════════════════════════════════════════════════════════
static void cb_test_oled(lv_event_t *e) {
    (void)e;
    test_active = 0;
    lv_label_set_text(lbl_test_result, "OLED Test\nRunning color pattern...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0x88,0x33,0xCC), 0);
    if (s_test_oled_cb) s_test_oled_cb();
}
static void cb_test_mpu(lv_event_t *e) {
    (void)e;
    test_active = 1;
    lv_label_set_text(lbl_test_result, "MPU6050 Test\nReading IMU data...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0x33,0x88,0xCC), 0);
}
static void cb_test_flex(lv_event_t *e) {
    (void)e;
    test_active = 2;
    lv_label_set_text(lbl_test_result, "Flex Sensor Test\nReading channels...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0x00,0xBB,0xEE), 0);
}
static void cb_test_hall(lv_event_t *e) {
    (void)e;
    test_active = 3;
    lv_label_set_text(lbl_test_result, "Hall Effect Test\nReading channels...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0xFF,0x88,0x00), 0);
}
static void cb_test_battery(lv_event_t *e) {
    (void)e;
    test_active = 4;
    lv_label_set_text(lbl_test_result, "Battery Test\nReading voltage...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0x33,0x99,0x33), 0);
}
static void cb_test_speaker(lv_event_t *e) {
    (void)e;
    test_active = 5;
    lv_label_set_text(lbl_test_result, "Speaker Test\nPlaying tone...");
    lv_obj_set_style_text_color(lbl_test_result, lv_color_make(0xCC,0x33,0x55), 0);
    if (s_test_speaker_cb) s_test_speaker_cb();
}

// ════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════
void gui_init() {
    init_styles();
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

    lv_scr_load(scr_splash);
    lv_timer_create(cb_splash_timer, 2000, NULL);
}

void gui_set_mode(AppMode mode) {
    cur_gui_mode = mode;
    switch (mode) {
        case MODE_MENU:           lv_scr_load_anim(scr_menu,     LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_TRAIN:          lv_scr_load_anim(scr_train,    LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_WORDS:  lv_scr_load_anim(scr_words,    LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_SPEECH: lv_scr_load_anim(scr_speech,   LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_BOTH:   lv_scr_load_anim(scr_both,     LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_PREDICT_WEB:    lv_scr_load_anim(scr_web,      LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_SETTINGS:       lv_scr_load_anim(scr_settings, LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
        case MODE_TEST:           lv_scr_load_anim(scr_test,     LV_SCR_LOAD_ANIM_FADE_ON, ANIM_TIME, 0, false); break;
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
    if (cur_gui_mode != MODE_TEST || !lbl_test_result) return;
    char buf[200];
    switch (test_active) {
    case 0: // OLED — handled by callback, show "PASS"
        lv_label_set_text(lbl_test_result, "OLED Test\n" LV_SYMBOL_OK " Display OK");
        break;
    case 1: // MPU
        snprintf(buf, sizeof(buf),
            "MPU6050 Live Data\n"
            "Accel: %.1f  %.1f  %.1f m/s2\n"
            "Gyro:  %.1f  %.1f  %.1f d/s\n"
            "Pitch: %.1f  Roll: %.1f",
            d.accel_x, d.accel_y, d.accel_z,
            d.gyro_x, d.gyro_y, d.gyro_z,
            d.pitch, d.roll);
        lv_label_set_text(lbl_test_result, buf);
        break;
    case 2: // Flex
        snprintf(buf, sizeof(buf),
            "Flex Sensors (raw ADC)\n"
            "Thumb:  %u\n"
            "Index:  %u\n"
            "Middle: %u\n"
            "Ring:   %u\n"
            "Pinky:  %u",
            d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4]);
        lv_label_set_text(lbl_test_result, buf);
        break;
    case 3: // Hall
        snprintf(buf, sizeof(buf),
            "Hall Sensors (raw ADC)\n"
            "Thumb:  %u\n"
            "Index:  %u\n"
            "Middle: %u\n"
            "Ring:   %u\n"
            "Pinky:  %u",
            d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4]);
        lv_label_set_text(lbl_test_result, buf);
        break;
    case 4: // Battery — voltage is approximated from power module
        break;
    case 5: // Speaker — handled by callback
        lv_label_set_text(lbl_test_result, "Speaker Test\n" LV_SYMBOL_OK " Tone played");
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
    char buf[24];
    const char *icon;
    lv_color_t clr;

    if (pct > 80)      { icon = LV_SYMBOL_BATTERY_FULL;  clr = lv_color_make(0x66,0xFF,0x66); }
    else if (pct > 50) { icon = LV_SYMBOL_BATTERY_3;     clr = lv_color_make(0xBB,0xFF,0x66); }
    else if (pct > 25) { icon = LV_SYMBOL_BATTERY_2;     clr = lv_color_make(0xFF,0xCC,0x00); }
    else if (pct > 10) { icon = LV_SYMBOL_BATTERY_1;     clr = lv_color_make(0xFF,0x66,0x00); }
    else               { icon = LV_SYMBOL_BATTERY_EMPTY; clr = lv_color_make(0xFF,0x33,0x33); }

    snprintf(buf, sizeof(buf), "%s %d%%", icon, pct);

    for (int i = 0; i < SI_COUNT; i++) {
        if (bat_labels[i]) {
            lv_label_set_text(bat_labels[i], buf);
            lv_obj_set_style_text_color(bat_labels[i], clr, 0);
        }
    }

    // Also update test result if battery test is active
    if (cur_gui_mode == MODE_TEST && test_active == 4 && lbl_test_result) {
        char vbuf[80];
        snprintf(vbuf, sizeof(vbuf),
            "Battery Test\n"
            LV_SYMBOL_BATTERY_FULL " %d%%\n"
            "Status: %s",
            pct, pct > 20 ? "OK" : "LOW");
        lv_label_set_text(lbl_test_result, vbuf);
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

    lv_color_t clr;
    if (pct < 50)      clr = lv_color_make(0x66, 0xFF, 0x66);  // green
    else if (pct < 75) clr = lv_color_make(0xFF, 0xCC, 0x00);  // yellow
    else               clr = lv_color_make(0xFF, 0x55, 0x33);  // red

    for (int i = 0; i < SI_COUNT; i++) {
        if (cpu_labels[i]) {
            lv_label_set_text(cpu_labels[i], buf);
            lv_obj_set_style_text_color(cpu_labels[i], clr, 0);
        }
    }
}
