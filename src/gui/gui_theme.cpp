/*
 * @file gui/gui_theme.cpp
 * @brief Theme palettes, LVGL styles, init_styles(), apply_theme()
 */
#include "gui_internal.h"

// ════════════════════════════════════════════════════════════════════
//  Dark palette
// ════════════════════════════════════════════════════════════════════
const ThemeColors TC_DARK = {
    /* scr_bg        */ lv_color_black(),
    /* scr_text      */ lv_color_white(),
    /* hdr_bg        */ lv_color_make(0x22,0x22,0x26),
    /* hdr_text      */ lv_color_white(),
    /* sep           */ lv_color_make(0x33,0x33,0x33),
    /* card_bg       */ lv_color_make(0x1E,0x1E,0x1E),
    /* card_text     */ lv_color_make(0xCC,0xCC,0xCC),
    /* section_text  */ lv_color_make(0x66,0x66,0x66),
    /* sub_text      */ lv_color_make(0xBB,0xBB,0xBB),
    /* about_bg      */ lv_color_make(0x12,0x12,0x12),
    /* about_text    */ lv_color_make(0x99,0x99,0x99),
    /* diag_bg       */ lv_color_make(0x1A,0x1A,0x1A),
    /* diag_text     */ lv_color_make(0x88,0x88,0x88),
    /* back_btn_bg   */ lv_color_make(0x33,0x33,0x33),
    /* slider_track  */ lv_color_make(0x33,0x33,0x33),
    /* dd_bg         */ lv_color_make(0x33,0x33,0x33),
    /* dd_list_bg    */ lv_color_make(0x2A,0x2A,0x2A),
    /* dd_list_text  */ lv_color_white(),
    /* bar_bg        */ lv_color_make(0x22,0x22,0x22),
    /* bar_label     */ lv_color_make(0xAA,0xAA,0xAA),
    /* sw_bg         */ lv_color_make(0x44,0x44,0x44),
};

// ════════════════════════════════════════════════════════════════════
//  Light palette
// ════════════════════════════════════════════════════════════════════
const ThemeColors TC_LIGHT = {
    /* scr_bg        */ lv_color_white(),
    /* scr_text      */ lv_color_black(),
    /* hdr_bg        */ lv_color_make(0xE0,0xE0,0xE0),
    /* hdr_text      */ lv_color_make(0x22,0x22,0x22),
    /* sep           */ lv_color_make(0xCC,0xCC,0xCC),
    /* card_bg       */ lv_color_make(0xF0,0xF0,0xF0),
    /* card_text     */ lv_color_make(0x33,0x33,0x33),
    /* section_text  */ lv_color_make(0x88,0x88,0x88),
    /* sub_text      */ lv_color_make(0x55,0x55,0x55),
    /* about_bg      */ lv_color_make(0xF5,0xF5,0xF5),
    /* about_text    */ lv_color_make(0x66,0x66,0x66),
    /* diag_bg       */ lv_color_make(0xEE,0xEE,0xEE),
    /* diag_text     */ lv_color_make(0x55,0x55,0x55),
    /* back_btn_bg   */ lv_color_make(0xDD,0xDD,0xDD),
    /* slider_track  */ lv_color_make(0xCC,0xCC,0xCC),
    /* dd_bg         */ lv_color_make(0xDD,0xDD,0xDD),
    /* dd_list_bg    */ lv_color_make(0xF0,0xF0,0xF0),
    /* dd_list_text  */ lv_color_make(0x22,0x22,0x22),
    /* bar_bg        */ lv_color_make(0xDD,0xDD,0xDD),
    /* bar_label     */ lv_color_make(0x55,0x55,0x55),
    /* sw_bg         */ lv_color_make(0xBB,0xBB,0xBB),
};

// Active palette pointer  (defaults to dark)
const ThemeColors *tc = &TC_DARK;

// ════════════════════════════════════════════════════════════════════
//  LVGL Styles
// ════════════════════════════════════════════════════════════════════
lv_style_t sty_scr;
lv_style_t sty_btn;
lv_style_t sty_btn_pr;
lv_style_t sty_hdr;

// ════════════════════════════════════════════════════════════════════
//  init_styles — called once at boot
// ════════════════════════════════════════════════════════════════════
void init_styles() {
    // Screen base
    lv_style_init(&sty_scr);
    lv_style_set_bg_color(&sty_scr, tc->scr_bg);
    lv_style_set_bg_opa(&sty_scr, LV_OPA_COVER);
    lv_style_set_text_color(&sty_scr, tc->scr_text);
    lv_style_set_text_font(&sty_scr, &lv_font_montserrat_20);

    // Default button
    lv_style_init(&sty_btn);
    lv_style_set_bg_color(&sty_btn, lv_color_make(0x00,0x55,0x88));
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, BTN_RAD);
    lv_style_set_text_color(&sty_btn, lv_color_white());
    lv_style_set_text_font(&sty_btn, &lv_font_montserrat_20);
    lv_style_set_pad_ver(&sty_btn, 12);
    lv_style_set_border_width(&sty_btn, 0);

    // Button pressed
    lv_style_init(&sty_btn_pr);
    lv_style_set_bg_color(&sty_btn_pr, lv_color_make(0x00,0x33,0x55));

    // Header bar
    lv_style_init(&sty_hdr);
    lv_style_set_bg_color(&sty_hdr, tc->hdr_bg);
    lv_style_set_bg_opa(&sty_hdr, LV_OPA_COVER);
}

// ════════════════════════════════════════════════════════════════════
//  apply_theme — rebuild all screens with the current palette
//
//  Deferred via lv_timer so it never runs inside an LVGL event.
// ════════════════════════════════════════════════════════════════════
static void del_scr(lv_obj_t *&s) { if (s) { lv_obj_del(s); s = nullptr; } }

void apply_theme() {
    tc = cfg_dark_mode ? &TC_DARK : &TC_LIGHT;

    // Update base styles
    lv_style_set_bg_color(&sty_scr, tc->scr_bg);
    lv_style_set_text_color(&sty_scr, tc->scr_text);
    lv_style_set_bg_color(&sty_hdr, tc->hdr_bg);

    // Temporary blank screen so we can delete the old ones safely
    lv_obj_t *blank = lv_obj_create(NULL);
    lv_obj_add_style(blank, &sty_scr, 0);
    lv_scr_load(blank);

    // Delete all screens
    del_scr(scr_menu);
    del_scr(scr_predict);
    del_scr(scr_local);
    del_scr(scr_train);
    del_scr(scr_web);
    del_scr(scr_settings);
    del_scr(scr_test);
    del_scr(scr_test_detail);

    // Rebuild every screen with new palette
    build_menu();
    build_predict_menu();
    build_train();
    build_local();
    build_web();
    build_settings();
    build_test();
    build_test_detail();

    // Return to the settings screen (where the toggle lives)
    lv_scr_load(scr_settings);
    cur_gui_mode = MODE_SETTINGS;

    // Clean up the temporary screen
    lv_obj_del(blank);
}
