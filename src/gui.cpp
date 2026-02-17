/*
 * @file gui.cpp
 * @brief Complete LVGL GUI for Sign Language Translator Glove v4.0
 *
 * Screens:  Splash → Main Menu → (TRAIN | PREDICT sub-menu)
 *           Predict sub-menu → WORDS | SPEECH | BOTH | WEB
 *
 * Display: 280 × 456 px AMOLED, dark theme for power saving.
 */
#include "gui.h"
#include "lvgl.h"
#include "config.h"

// ════════════════════════════════════════════════════════════════════
//  Forward declarations for event callbacks
// ════════════════════════════════════════════════════════════════════
static void cb_btn_train(lv_event_t *e);
static void cb_btn_predict(lv_event_t *e);
static void cb_btn_words(lv_event_t *e);
static void cb_btn_speech(lv_event_t *e);
static void cb_btn_both(lv_event_t *e);
static void cb_btn_web(lv_event_t *e);
static void cb_btn_back_menu(lv_event_t *e);
static void cb_btn_back_predict(lv_event_t *e);
static void cb_splash_timer(lv_timer_t *t);

// ════════════════════════════════════════════════════════════════════
//  External mode-change hook (set by main.cpp)
// ════════════════════════════════════════════════════════════════════
static void (*s_mode_cb)(AppMode) = nullptr;
void gui_register_mode_callback(void (*cb)(AppMode)) { s_mode_cb = cb; }

// Helper to fire mode change
static void fire_mode(AppMode m) { if (s_mode_cb) s_mode_cb(m); }

// ════════════════════════════════════════════════════════════════════
//  Screen objects
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *scr_splash     = nullptr;
static lv_obj_t *scr_menu       = nullptr;
static lv_obj_t *scr_predict    = nullptr;   // sub-menu
static lv_obj_t *scr_train      = nullptr;
static lv_obj_t *scr_words      = nullptr;
static lv_obj_t *scr_speech     = nullptr;
static lv_obj_t *scr_both       = nullptr;
static lv_obj_t *scr_web        = nullptr;

// Dynamic labels / widgets updated at runtime
static lv_obj_t *lbl_battery    = nullptr;   // shared top-bar battery %
static lv_obj_t *lbl_gesture_w  = nullptr;   // WORDS mode gesture text
static lv_obj_t *lbl_gesture_b  = nullptr;   // BOTH  mode gesture text
static lv_obj_t *lbl_train_stat = nullptr;   // TRAIN status text
static lv_obj_t *lbl_web_stat   = nullptr;   // WEB   status text
static lv_obj_t *qr_web        = nullptr;    // QR code widget

// Sensor bars (WORDS screen)
static lv_obj_t *bar_flex[5]    = {};
static lv_obj_t *bar_hall[5]    = {};

// Sensor bars (BOTH screen copies)
static lv_obj_t *bar_flex_b[5]  = {};
static lv_obj_t *bar_hall_b[5]  = {};

// Battery labels on each screen
static lv_obj_t *lbl_bat_menu   = nullptr;
static lv_obj_t *lbl_bat_pred   = nullptr;
static lv_obj_t *lbl_bat_train  = nullptr;
static lv_obj_t *lbl_bat_words  = nullptr;
static lv_obj_t *lbl_bat_speech = nullptr;
static lv_obj_t *lbl_bat_both   = nullptr;
static lv_obj_t *lbl_bat_web    = nullptr;

// Current active screen pointer (for efficient update routing)
static AppMode cur_gui_mode = MODE_MENU;

// ════════════════════════════════════════════════════════════════════
//  Style definitions
// ════════════════════════════════════════════════════════════════════
static lv_style_t sty_screen;           // dark background
static lv_style_t sty_btn;              // rounded accent button
static lv_style_t sty_btn_pressed;
static lv_style_t sty_topbar;           // semi-transparent top strip
static lv_style_t sty_bar_flex;
static lv_style_t sty_bar_hall;

static bool styles_ready = false;

static void init_styles() {
    if (styles_ready) return;
    styles_ready = true;

    /* Screen background — pure black for AMOLED power saving */
    lv_style_init(&sty_screen);
    lv_style_set_bg_color(&sty_screen, lv_color_black());
    lv_style_set_bg_opa(&sty_screen, LV_OPA_COVER);
    lv_style_set_text_color(&sty_screen, lv_color_white());
    lv_style_set_pad_all(&sty_screen, 0);

    /* Accent button */
    lv_style_init(&sty_btn);
    lv_style_set_bg_color(&sty_btn, lv_color_make(0x00, 0x7A, 0xCC));  // blue
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, 14);
    lv_style_set_text_color(&sty_btn, lv_color_white());
    lv_style_set_text_font(&sty_btn, &lv_font_montserrat_20);
    lv_style_set_pad_ver(&sty_btn, 14);
    lv_style_set_pad_hor(&sty_btn, 20);
    lv_style_set_shadow_width(&sty_btn, 0);
    lv_style_set_border_width(&sty_btn, 0);

    lv_style_init(&sty_btn_pressed);
    lv_style_set_bg_color(&sty_btn_pressed, lv_color_make(0x00, 0x55, 0x99));

    /* Top bar (transparent strip) */
    lv_style_init(&sty_topbar);
    lv_style_set_bg_color(&sty_topbar, lv_color_make(0x1A, 0x1A, 0x1A));
    lv_style_set_bg_opa(&sty_topbar, LV_OPA_COVER);
    lv_style_set_radius(&sty_topbar, 0);
    lv_style_set_pad_all(&sty_topbar, 4);
    lv_style_set_border_width(&sty_topbar, 0);

    /* Bar styles */
    lv_style_init(&sty_bar_flex);
    lv_style_set_bg_color(&sty_bar_flex, lv_color_make(0x33, 0x33, 0x33));
    lv_style_set_radius(&sty_bar_flex, 4);

    lv_style_init(&sty_bar_hall);
    lv_style_set_bg_color(&sty_bar_hall, lv_color_make(0x33, 0x33, 0x33));
    lv_style_set_radius(&sty_bar_hall, 4);
}

// ════════════════════════════════════════════════════════════════════
//  Helper: dark screen base
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *make_screen() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &sty_screen, 0);
    return scr;
}

// ════════════════════════════════════════════════════════════════════
//  Helper: accent button
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt,
                           lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &sty_btn, 0);
    lv_obj_add_style(btn, &sty_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
    return btn;
}

// ════════════════════════════════════════════════════════════════════
//  Helper: battery label at top-right of a screen
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *add_battery_label(lv_obj_t *scr) {
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LCD_WIDTH, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(bar, &sty_topbar, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, LV_SYMBOL_BATTERY_FULL " 100%");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0x66, 0xFF, 0x66), 0);
    lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -6, 0);
    return lbl;
}

// ════════════════════════════════════════════════════════════════════
//  Helper: back button at bottom of a screen
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *add_back_btn(lv_obj_t *scr, lv_event_cb_t cb) {
    lv_obj_t *btn = make_btn(scr, LV_SYMBOL_LEFT " Back", 120, 42, cb);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x44, 0x44, 0x44), 0);
    return btn;
}

// ════════════════════════════════════════════════════════════════════
//  Helper: title label
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *add_title(lv_obj_t *scr, const char *txt, lv_coord_t y) {
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
    return lbl;
}

// ════════════════════════════════════════════════════════════════════
//  Helper: create flex + hall bar pairs with labels
// ════════════════════════════════════════════════════════════════════
static void create_sensor_bars(lv_obj_t *parent,
                               lv_obj_t *flex_out[5],
                               lv_obj_t *hall_out[5],
                               lv_coord_t start_y)
{
    const char *names[] = {"T", "I", "M", "R", "P"};
    lv_coord_t bar_w = 36;
    lv_coord_t gap   = 8;
    lv_coord_t total = 5 * bar_w + 4 * gap;
    lv_coord_t x0    = (LCD_WIDTH - total) / 2;

    // Section label — Flex
    lv_obj_t *lbl_f = lv_label_create(parent);
    lv_label_set_text(lbl_f, "FLEX");
    lv_obj_set_style_text_font(lbl_f, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_f, lv_color_make(0x88, 0xCC, 0xFF), 0);
    lv_obj_align(lbl_f, LV_ALIGN_TOP_LEFT, 8, start_y - 18);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i * (bar_w + gap);

        // Finger label
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xAA), 0);
        lv_obj_set_pos(lbl, x + bar_w / 2 - 4, start_y + 82);

        // Flex bar (vertical)
        lv_obj_t *b = lv_bar_create(parent);
        lv_obj_set_size(b, bar_w, 75);
        lv_obj_set_pos(b, x, start_y);
        lv_bar_set_range(b, 0, 4095);
        lv_bar_set_value(b, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_make(0x00, 0xCC, 0xFF), LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
        flex_out[i] = b;
    }

    // Section label — Hall
    lv_coord_t hall_y = start_y + 110;
    lv_obj_t *lbl_h = lv_label_create(parent);
    lv_label_set_text(lbl_h, "HALL");
    lv_obj_set_style_text_font(lbl_h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_h, lv_color_make(0xFF, 0xAA, 0x55), 0);
    lv_obj_align(lbl_h, LV_ALIGN_TOP_LEFT, 8, hall_y - 18);

    for (int i = 0; i < 5; i++) {
        lv_coord_t x = x0 + i * (bar_w + gap);

        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xAA), 0);
        lv_obj_set_pos(lbl, x + bar_w / 2 - 4, hall_y + 82);

        lv_obj_t *b = lv_bar_create(parent);
        lv_obj_set_size(b, bar_w, 75);
        lv_obj_set_pos(b, x, hall_y);
        lv_bar_set_range(b, 0, 4095);
        lv_bar_set_value(b, 2048, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_make(0xFF, 0x88, 0x00), LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
        hall_out[i] = b;
    }
}

// ════════════════════════════════════════════════════════════════════
//  Build screens
// ════════════════════════════════════════════════════════════════════

// ─── Splash ────────────────────────────────────────────────────────
static void build_splash() {
    scr_splash = make_screen();

    lv_obj_t *title = lv_label_create(scr_splash);
    lv_label_set_text(title, LV_SYMBOL_OK "\nHybrid-Sense");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_make(0x00, 0xCC, 0xFF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *sub = lv_label_create(scr_splash);
    lv_label_set_text(sub, "Sign Language Glove v4.0");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_make(0x99, 0x99, 0x99), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *dot = lv_label_create(scr_splash);
    lv_label_set_text(dot, "Initializing...");
    lv_obj_set_style_text_font(dot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dot, lv_color_make(0x66, 0x66, 0x66), 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ─── Main Menu ─────────────────────────────────────────────────────
static void build_menu() {
    scr_menu = make_screen();
    lbl_bat_menu = add_battery_label(scr_menu);
    add_title(scr_menu, "MAIN MENU", 44);

    make_btn(scr_menu, LV_SYMBOL_UPLOAD " TRAIN",
             220, 70, cb_btn_train);
    lv_obj_align(lv_obj_get_child(scr_menu, -1), LV_ALIGN_CENTER, 0, -40);

    make_btn(scr_menu, LV_SYMBOL_EYE_OPEN " PREDICT",
             220, 70, cb_btn_predict);
    lv_obj_align(lv_obj_get_child(scr_menu, -1), LV_ALIGN_CENTER, 0, 50);
}

// ─── Predict Sub-Menu ──────────────────────────────────────────────
static void build_predict_menu() {
    scr_predict = make_screen();
    lbl_bat_pred = add_battery_label(scr_predict);
    add_title(scr_predict, "PREDICT MODE", 44);

    lv_coord_t bw = 120, bh = 60;
    lv_coord_t cx = LCD_WIDTH / 2;

    // 2×2 grid
    lv_obj_t *b1 = make_btn(scr_predict, "WORDS",  bw, bh, cb_btn_words);
    lv_obj_align(b1, LV_ALIGN_CENTER, -(bw/2 + 6), -60);

    lv_obj_t *b2 = make_btn(scr_predict, "SPEECH", bw, bh, cb_btn_speech);
    lv_obj_align(b2, LV_ALIGN_CENTER,  (bw/2 + 6), -60);

    lv_obj_t *b3 = make_btn(scr_predict, "BOTH",   bw, bh, cb_btn_both);
    lv_obj_align(b3, LV_ALIGN_CENTER, -(bw/2 + 6),  20);

    lv_obj_t *b4 = make_btn(scr_predict, "WEB",    bw, bh, cb_btn_web);
    lv_obj_align(b4, LV_ALIGN_CENTER,  (bw/2 + 6),  20);
    // Change WEB button color to green accent
    lv_obj_set_style_bg_color(b4, lv_color_make(0x00, 0x99, 0x55), 0);

    add_back_btn(scr_predict, cb_btn_back_menu);
}

// ─── TRAIN screen ──────────────────────────────────────────────────
static void build_train() {
    scr_train = make_screen();
    lbl_bat_train = add_battery_label(scr_train);
    add_title(scr_train, LV_SYMBOL_UPLOAD " TRAIN MODE", 44);

    lv_obj_t *info = lv_label_create(scr_train);
    lv_label_set_text(info,
        "Connect Edge Impulse\n"
        "Data Forwarder via USB.\n\n"
        "Sensor data is streamed\n"
        "over Serial at " LV_SYMBOL_RIGHT " 115200 baud.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info, 260);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -30);

    lbl_train_stat = lv_label_create(scr_train);
    lv_label_set_text(lbl_train_stat, "Status: Streaming...");
    lv_obj_set_style_text_font(lbl_train_stat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_train_stat, lv_color_make(0x66, 0xFF, 0x66), 0);
    lv_obj_align(lbl_train_stat, LV_ALIGN_CENTER, 0, 60);

    add_back_btn(scr_train, cb_btn_back_menu);
}

// ─── WORDS screen ──────────────────────────────────────────────────
static void build_words() {
    scr_words = make_screen();
    lbl_bat_words = add_battery_label(scr_words);

    // Large gesture text
    lbl_gesture_w = lv_label_create(scr_words);
    lv_label_set_text(lbl_gesture_w, "---");
    lv_obj_set_style_text_font(lbl_gesture_w, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_w, lv_color_make(0x00, 0xFF, 0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_w, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_w, 260);
    lv_obj_align(lbl_gesture_w, LV_ALIGN_TOP_MID, 0, 38);

    // Sensor bars (flex + hall)
    create_sensor_bars(scr_words, bar_flex, bar_hall, 80);

    add_back_btn(scr_words, cb_btn_back_predict);
}

// ─── SPEECH screen ─────────────────────────────────────────────────
static void build_speech() {
    scr_speech = make_screen();
    lbl_bat_speech = add_battery_label(scr_speech);

    lv_obj_t *icon = lv_label_create(scr_speech);
    lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_make(0x00, 0xCC, 0xFF), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *lbl = lv_label_create(scr_speech);
    lv_label_set_text(lbl, "SPEECH MODE\nAudio output active");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    add_back_btn(scr_speech, cb_btn_back_predict);
}

// ─── BOTH screen ───────────────────────────────────────────────────
static void build_both() {
    scr_both = make_screen();
    lbl_bat_both = add_battery_label(scr_both);

    // Gesture + speaker icon
    lbl_gesture_b = lv_label_create(scr_both);
    lv_label_set_text(lbl_gesture_b, "---");
    lv_obj_set_style_text_font(lbl_gesture_b, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gesture_b, lv_color_make(0x00, 0xFF, 0xAA), 0);
    lv_obj_set_style_text_align(lbl_gesture_b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_gesture_b, 260);
    lv_obj_align(lbl_gesture_b, LV_ALIGN_TOP_MID, 0, 38);

    // Sensor bars
    create_sensor_bars(scr_both, bar_flex_b, bar_hall_b, 80);

    // Small speaker indicator
    lv_obj_t *spk = lv_label_create(scr_both);
    lv_label_set_text(spk, LV_SYMBOL_VOLUME_MAX " Audio ON");
    lv_obj_set_style_text_font(spk, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spk, lv_color_make(0xFF, 0xCC, 0x00), 0);
    lv_obj_align(spk, LV_ALIGN_BOTTOM_MID, 0, -60);

    add_back_btn(scr_both, cb_btn_back_predict);
}

// ─── WEB screen ────────────────────────────────────────────────────
static void build_web() {
    scr_web = make_screen();
    lbl_bat_web = add_battery_label(scr_web);
    add_title(scr_web, LV_SYMBOL_WIFI " WEB MODE", 44);

    // QR code (will be set later via gui_show_web_qr)
    qr_web = lv_qrcode_create(scr_web, 150,
                               lv_color_white(), lv_color_black());
    lv_obj_align(qr_web, LV_ALIGN_CENTER, 0, -30);
    // Set a placeholder
    const char *placeholder = "http://192.168.4.1";
    lv_qrcode_update(qr_web, placeholder, strlen(placeholder));
    // Add border around QR
    lv_obj_set_style_border_color(qr_web, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr_web, 6, 0);

    // URL label
    lbl_web_stat = lv_label_create(scr_web);
    lv_label_set_text(lbl_web_stat,
        "WiFi: " WIFI_AP_SSID "\n"
        "http://192.168.4.1");
    lv_obj_set_style_text_font(lbl_web_stat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_web_stat, lv_color_make(0xAA, 0xAA, 0xAA), 0);
    lv_obj_set_style_text_align(lbl_web_stat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_web_stat, 260);
    lv_obj_align(lbl_web_stat, LV_ALIGN_CENTER, 0, 80);

    add_back_btn(scr_web, cb_btn_back_predict);
}

// ════════════════════════════════════════════════════════════════════
//  Event callbacks
// ════════════════════════════════════════════════════════════════════
static void cb_btn_train(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_train, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    cur_gui_mode = MODE_TRAIN;
    fire_mode(MODE_TRAIN);
}
static void cb_btn_predict(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_predict, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
static void cb_btn_words(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_words, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    cur_gui_mode = MODE_PREDICT_WORDS;
    fire_mode(MODE_PREDICT_WORDS);
}
static void cb_btn_speech(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_speech, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    cur_gui_mode = MODE_PREDICT_SPEECH;
    fire_mode(MODE_PREDICT_SPEECH);
}
static void cb_btn_both(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_both, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    cur_gui_mode = MODE_PREDICT_BOTH;
    fire_mode(MODE_PREDICT_BOTH);
}
static void cb_btn_web(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_web, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    cur_gui_mode = MODE_PREDICT_WEB;
    fire_mode(MODE_PREDICT_WEB);
}
static void cb_btn_back_menu(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    cur_gui_mode = MODE_MENU;
    fire_mode(MODE_MENU);
}
static void cb_btn_back_predict(lv_event_t *e) {
    (void)e;
    lv_scr_load_anim(scr_predict, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    cur_gui_mode = MODE_MENU;  // sub-menu is logically "menu"
    fire_mode(MODE_MENU);
}

// Splash auto-transition timer
static void cb_splash_timer(lv_timer_t *t) {
    (void)t;
    lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
    cur_gui_mode = MODE_MENU;
    lv_timer_del(t);
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

    // Show splash, auto-transition after 2 s
    lv_scr_load(scr_splash);
    lv_timer_create(cb_splash_timer, 2000, NULL);
}

void gui_set_mode(AppMode mode) {
    cur_gui_mode = mode;
    switch (mode) {
        case MODE_MENU:           lv_scr_load_anim(scr_menu,    LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
        case MODE_TRAIN:          lv_scr_load_anim(scr_train,   LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
        case MODE_PREDICT_WORDS:  lv_scr_load_anim(scr_words,   LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
        case MODE_PREDICT_SPEECH: lv_scr_load_anim(scr_speech,  LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
        case MODE_PREDICT_BOTH:   lv_scr_load_anim(scr_both,    LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
        case MODE_PREDICT_WEB:    lv_scr_load_anim(scr_web,     LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false); break;
    }
}

void gui_update(const SensorData &d) {
    // Only update sensor bars on screens that have them
    if (cur_gui_mode == MODE_PREDICT_WORDS) {
        for (int i = 0; i < 5; i++) {
            lv_bar_set_value(bar_flex[i], d.flex[i], LV_ANIM_ON);
            lv_bar_set_value(bar_hall[i], d.hall[i], LV_ANIM_ON);
        }
    }
    else if (cur_gui_mode == MODE_PREDICT_BOTH) {
        for (int i = 0; i < 5; i++) {
            lv_bar_set_value(bar_flex_b[i], d.flex[i], LV_ANIM_ON);
            lv_bar_set_value(bar_hall_b[i], d.hall[i], LV_ANIM_ON);
        }
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

    if (pct > 80)      { icon = LV_SYMBOL_BATTERY_FULL;  clr = lv_color_make(0x66, 0xFF, 0x66); }
    else if (pct > 50) { icon = LV_SYMBOL_BATTERY_3;     clr = lv_color_make(0xBB, 0xFF, 0x66); }
    else if (pct > 25) { icon = LV_SYMBOL_BATTERY_2;     clr = lv_color_make(0xFF, 0xCC, 0x00); }
    else if (pct > 10) { icon = LV_SYMBOL_BATTERY_1;     clr = lv_color_make(0xFF, 0x66, 0x00); }
    else               { icon = LV_SYMBOL_BATTERY_EMPTY; clr = lv_color_make(0xFF, 0x33, 0x33); }

    snprintf(buf, sizeof(buf), "%s %d%%", icon, pct);

    // Update all screen battery labels
    lv_obj_t *labels[] = {
        lbl_bat_menu, lbl_bat_pred, lbl_bat_train,
        lbl_bat_words, lbl_bat_speech, lbl_bat_both, lbl_bat_web
    };
    for (auto *l : labels) {
        if (l) {
            lv_label_set_text(l, buf);
            lv_obj_set_style_text_color(l, clr, 0);
        }
    }
}

void gui_show_web_qr(const char *url) {
    if (qr_web && url) {
        lv_qrcode_update(qr_web, url, strlen(url));
    }
    if (lbl_web_stat && url) {
        char buf[128];
        snprintf(buf, sizeof(buf), "WiFi: %s\n%s", WIFI_AP_SSID, url);
        lv_label_set_text(lbl_web_stat, buf);
    }
}

void gui_set_train_status(const char *msg) {
    if (lbl_train_stat) lv_label_set_text(lbl_train_stat, msg);
}
