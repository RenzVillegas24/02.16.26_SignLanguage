/*
 * @file gui/gui_helpers.cpp
 * @brief Reusable widget factory helpers + back-gesture helper
 */
#include "gui_internal.h"

// ════════════════════════════════════════════════════════════════════
//  mk_scr
// ════════════════════════════════════════════════════════════════════
lv_obj_t *mk_scr() {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_add_style(s, &sty_scr, 0);
    return s;
}

// ════════════════════════════════════════════════════════════════════
//  mk_btn — accent-coloured action button
// ════════════════════════════════════════════════════════════════════
lv_obj_t *mk_btn(lv_obj_t *parent, const char *text,
                  int w, int h, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &sty_btn, 0);
    lv_obj_add_style(btn, &sty_btn_pr, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

// ════════════════════════════════════════════════════════════════════
//  mk_nav_btn — navigation card button (text left, ">" right)
// ════════════════════════════════════════════════════════════════════
lv_obj_t *mk_nav_btn(lv_obj_t *parent, const char *text,
                      lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(btn, tc->card_bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, BTN_RAD, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 14, 0);
    lv_obj_set_style_pad_right(btn, 14, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(accent_primary(), tc->card_bg, 40), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, tc->card_text, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *arrow = lv_label_create(btn);
    lv_label_set_text(arrow, ">");
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(arrow, tc->section_text, 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    return btn;
}

// ════════════════════════════════════════════════════════════════════
//  mk_header — nav bar + separator  (status bar is on lv_layer_top)
// ════════════════════════════════════════════════════════════════════
int mk_header(lv_obj_t *scr, const char *title,
              lv_event_cb_t back_cb, lv_obj_t **title_out) {

    // ── Nav bar ──
    lv_obj_t *nav = lv_obj_create(scr);
    lv_obj_set_size(nav, SCR_W, NAV_H);
    lv_obj_set_pos(nav, 0, STAT_H);
    lv_obj_add_style(nav, &sty_hdr, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_left(nav, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(nav, SIDE_PAD, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    if (back_cb) {
        lv_obj_t *bb = lv_btn_create(nav);
        lv_obj_set_size(bb, BACK_SZ, BACK_SZ);
        lv_obj_align(bb, LV_ALIGN_LEFT_MID, -4, 0);
        // Back button gets accent tint like header
        lv_obj_set_style_bg_color(bb, lv_color_mix(accent_primary(), tc->back_btn_bg, 30), 0);
        lv_obj_set_style_bg_opa(bb, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bb, 10, 0);
        lv_obj_set_style_border_width(bb, 0, 0);
        lv_obj_set_style_shadow_width(bb, 0, 0);
        lv_obj_add_event_cb(bb, back_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *ba = lv_label_create(bb);
        lv_label_set_text(ba, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(ba, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(ba, tc->hdr_text, 0);
        lv_obj_center(ba);
    }

    lv_obj_t *tt = lv_label_create(nav);
    lv_label_set_text(tt, title);
    lv_obj_set_style_text_font(tt, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(tt, tc->hdr_text, 0);
    if (back_cb)
        lv_obj_align(tt, LV_ALIGN_LEFT_MID, BACK_SZ + 2, 0);
    else
        lv_obj_align(tt, LV_ALIGN_LEFT_MID, 0, 0);

    if (title_out) *title_out = tt;

    // ── Separator ──
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, SCR_W, SEP_H);
    lv_obj_set_pos(sep, 0, STAT_H + NAV_H);
    lv_obj_set_style_bg_color(sep, tc->sep, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    return STAT_H + NAV_H + SEP_H;
}

// ════════════════════════════════════════════════════════════════════
//  mk_content — scrollable flex-column below the header
// ════════════════════════════════════════════════════════════════════
lv_obj_t *mk_content(lv_obj_t *scr, int header_h) {
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, SCR_W, SCR_H - header_h);
    lv_obj_set_pos(cont, 0, header_h);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_top(cont, 10, 0);
    lv_obj_set_style_pad_bottom(cont, 10, 0);
    lv_obj_set_style_pad_left(cont, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(cont, SIDE_PAD, 0);
    lv_obj_set_style_pad_row(cont, BTN_GAP, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return cont;
}

// ════════════════════════════════════════════════════════════════════
//  build_status_bar — persistent bar on lv_layer_top()
//  Always visible above all screens & screen-transition animations.
// ════════════════════════════════════════════════════════════════════
void build_status_bar() {
    lv_obj_t *top = lv_layer_top();

    stat_bar = lv_obj_create(top);
    lv_obj_set_size(stat_bar, SCR_W, STAT_H);
    lv_obj_set_pos(stat_bar, 0, 0);
    lv_obj_add_style(stat_bar, &sty_hdr, 0);
    lv_obj_set_style_radius(stat_bar, 0, 0);
    lv_obj_set_style_border_width(stat_bar, 0, 0);
    lv_obj_set_style_pad_left(stat_bar, SIDE_PAD, 0);
    lv_obj_set_style_pad_right(stat_bar, SIDE_PAD, 0);
    lv_obj_clear_flag(stat_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(stat_bar, LV_OBJ_FLAG_CLICKABLE);

    cpu_label = lv_label_create(stat_bar);
    lv_label_set_text(cpu_label, "CPU 0%");
    lv_obj_set_style_text_font(cpu_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cpu_label, tc->hdr_text, 0);
    lv_obj_align(cpu_label, LV_ALIGN_LEFT_MID, 0, 0);

    bat_label = lv_label_create(stat_bar);
    lv_label_set_text(bat_label, LV_SYMBOL_BATTERY_FULL " 100%");
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bat_label, tc->hdr_text, 0);
    lv_obj_align(bat_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Charging bolt indicator — centered in status bar, uses theme text color (white/black)
    charge_label = lv_label_create(stat_bar);
    lv_label_set_text(charge_label, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(charge_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(charge_label, tc->hdr_text, 0);  // white (dark) or black (light)
    lv_obj_align(charge_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(charge_label, LV_OBJ_FLAG_HIDDEN);  // hidden by default
}

// ════════════════════════════════════════════════════════════════════
//  create_bars — 5 flex + 5 hall side + 5 hall top sensor bars (accent-coloured indicator)
// ════════════════════════════════════════════════════════════════════
void create_bars(lv_obj_t *scr, lv_obj_t *flex[], lv_obj_t *hall[],
                 lv_obj_t *hall_top[], int y_start) {
    const int BAR_W  = 180;
    const int BAR_H  = 14;
    const int ROW_H  = 20;
    const int LBL_W  = 56;
    const int X_LBL  = SIDE_PAD;
    const int X_BAR  = X_LBL + LBL_W + 4;

    auto make_bar = [&](const char *name, int row, lv_obj_t *&out) {
        int y = y_start + row * ROW_H;

        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, name);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, tc->bar_label, 0);
        lv_obj_set_pos(l, X_LBL, y);

        lv_obj_t *b = lv_bar_create(scr);
        lv_obj_set_size(b, BAR_W, BAR_H);
        lv_obj_set_pos(b, X_BAR, y + 2);
        lv_bar_set_range(b, 0, 4095);
        lv_bar_set_value(b, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, tc->bar_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, accent_primary(), LV_PART_INDICATOR);
        out = b;
    };

    const char *fn[]  = {"Flex 1","Flex 2","Flex 3","Flex 4","Flex 5"};
    const char *hn[]  = {"Hall 1","Hall 2","Hall 3","Hall 4","Hall 5"};
    const char *htn[] = {"HTop 1","HTop 2","HTop 3","HTop 4","HTop 5"};
    for (int i = 0; i < 5; i++) make_bar(fn[i],  i,      flex[i]);
    for (int i = 0; i < 5; i++) make_bar(hn[i],  i + 5,  hall[i]);
    for (int i = 0; i < 5; i++) make_bar(htn[i], i + 10, hall_top[i]);
}

// ════════════════════════════════════════════════════════════════════
//  add_slider_row — accent-coloured slider
// ════════════════════════════════════════════════════════════════════
lv_obj_t *add_slider_row(lv_obj_t *par, const char *icon,
                         const char *label, int32_t min_v, int32_t max_v,
                         int32_t cur, lv_event_cb_t cb,
                         lv_obj_t **val_lbl_out) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 64);
    lv_obj_set_style_bg_color(row, tc->card_bg, 0);
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
    lv_obj_set_style_text_color(lbl, tc->card_text, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *vl = lv_label_create(row);
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%d", (int)cur);
    lv_label_set_text(vl, vbuf);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vl, accent_primary(), 0);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, 0, 0);
    *val_lbl_out = vl;

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, BTN_W - 28, 8);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(sl, min_v, max_v);
    lv_slider_set_value(sl, cur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, tc->slider_track, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, accent_dark(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, accent_light(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}

// ════════════════════════════════════════════════════════════════════
//  mk_section
// ════════════════════════════════════════════════════════════════════
void mk_section(lv_obj_t *par, const char *txt) {
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, tc->section_text, 0);
    lv_obj_set_width(l, BTN_W);
}

// ════════════════════════════════════════════════════════════════════
//  add_switch_row — accent-coloured checked state, entire row clickable
// ════════════════════════════════════════════════════════════════════

// Internal: when the row itself is tapped, toggle its switch child
static void _switch_row_click_cb(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(e);
    if (!sw) return;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_event_send(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *add_switch_row(lv_obj_t *par, const char *icon,
                         const char *label, bool initial,
                         lv_event_cb_t cb) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 44);
    lv_obj_set_style_bg_color(row, tc->card_bg, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Make the whole row clickable with accent tint feedback
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, lv_color_mix(accent_primary(), tc->card_bg, 40),
                              LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(row);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s", icon, label);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, tc->card_text, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 22);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, tc->sw_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, accent_dark(),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Row click toggles the switch
    lv_obj_add_event_cb(row, _switch_row_click_cb, LV_EVENT_CLICKED, sw);

    return sw;
}

// ════════════════════════════════════════════════════════════════════
//  add_dropdown_row — accent-coloured text
// ════════════════════════════════════════════════════════════════════
lv_obj_t *add_dropdown_row(lv_obj_t *par, const char *icon,
                           const char *label, const char *options,
                           uint16_t sel, lv_event_cb_t cb) {
    lv_obj_t *row = lv_obj_create(par);
    lv_obj_set_size(row, BTN_W, 44);
    lv_obj_set_style_bg_color(row, tc->card_bg, 0);
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
    lv_obj_set_style_text_color(lbl, tc->card_text, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_size(dd, 100, 28);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(dd, tc->dd_bg, 0);
    lv_obj_set_style_text_color(dd, accent_primary(), 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_obj_set_style_pad_hor(dd, 8, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
        lv_obj_set_style_bg_color(list, tc->dd_list_bg, 0);
        lv_obj_set_style_text_color(list, tc->dd_list_text, 0);
    }
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
}

// ════════════════════════════════════════════════════════════════════
//  Back-gesture — timer-polled swipe detection (bypasses LVGL events)
//
//  Polls the touch input directly every 20 ms so no events can be
//  swallowed by scroll-containers or child widgets.
//
//  Trigger rules:
//   • Touch-down must start within the left 1/4 of the screen.
//   • Finger must travel ≥ 1/4 screen-width to the right.
//   • Gesture fires immediately when the threshold is crossed.
//
//  The registry is cleared by clear_back_gestures() before each
//  apply_theme() rebuild, so stale screen pointers never accumulate.
// ════════════════════════════════════════════════════════════════════

static const lv_coord_t SWIPE_ZONE = SCR_W / 4;   // start zone (left 70 px)
static const lv_coord_t SWIPE_DIST = SCR_W / 4;   // required travel distance

#define BACK_REG_MAX 12
struct BackEntry { lv_obj_t *scr; lv_event_cb_t cb; };
static BackEntry _back_reg[BACK_REG_MAX];
static int       _back_reg_n = 0;

// Swipe runtime state
static lv_coord_t  _sw_start_x  = -1;
static bool        _sw_active   = false;
static bool        _sw_prev_down= false;
static lv_timer_t *_sw_timer    = nullptr;

static lv_event_cb_t _find_back_cb() {
    lv_obj_t *act = lv_scr_act();
    for (int i = 0; i < _back_reg_n; i++) {
        if (_back_reg[i].scr == act) return _back_reg[i].cb;
    }
    return nullptr;
}

static void _swipe_poll_cb(lv_timer_t *t) {
    (void)t;
    if (!cfg_back_gesture) return;             // globally disabled
    if (is_bench_running()) return;            // block during benchmark

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) return;

    bool down = (indev->proc.state == LV_INDEV_STATE_PRESSED);
    lv_coord_t px = indev->proc.types.pointer.act_point.x;

    if (down && !_sw_prev_down) {
        _sw_start_x = px;
        _sw_active  = (px >= 0 && px <= SWIPE_ZONE);
    }
    else if (down && _sw_prev_down && _sw_active) {
        if (px - _sw_start_x >= SWIPE_DIST) {
            _sw_active = false;
            lv_event_cb_t cb = _find_back_cb();
            if (cb) {
                lv_event_t ev;
                lv_memset_00(&ev, sizeof(ev));
                cb(&ev);
            }
        }
    }
    else if (!down) {
        _sw_start_x = -1;
        _sw_active  = false;
    }
    _sw_prev_down = down;
}

void clear_back_gestures() {
    _back_reg_n  = 0;
    _sw_start_x  = -1;
    _sw_active   = false;
    _sw_prev_down= false;
    // Timer survives — no need to recreate
}

void add_back_gesture(lv_obj_t *scr, lv_event_cb_t back_cb) {
    if (_back_reg_n < BACK_REG_MAX) {
        _back_reg[_back_reg_n++] = { scr, back_cb };
    }
    if (!_sw_timer) {
        _sw_timer = lv_timer_create(_swipe_poll_cb, 20, NULL);
    }
}
