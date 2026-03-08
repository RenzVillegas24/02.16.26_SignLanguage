/*
 * @file Test_EI_Training.cpp
 * @brief Edge Impulse training data-collection test
 *
 * Minimal standalone firmware that:
 *   1. Boots the AMOLED display + LVGL (touch-enabled)
 *   2. Initialises all sensors (MUX + ADS1115 + MPU6050)
 *   3. Runs the sensor_module processing pipeline (calibration, EMA, %)
 *   4. Shows live sensor readings on-screen (flex bars, hall bars, IMU values)
 *   5. Streams 18-feature CSV over USB Serial at 115200 baud for
 *      Edge Impulse Data Forwarder ingestion
 *
 * Usage:
 *   • Flash this env (Test_EI_Training) to the board
 *   • Open the Edge Impulse web interface → Data acquisition → Serial
 *   • Connect via USB — the device streams CSV continuously
 *   • The screen shows real-time sensor feedback while collecting
 *
 * CSV format (18 features per line):
 *   flex0,flex1,flex2,flex3,flex4,
 *   hall0,hall1,hall2,hall3,hall4,
 *   accel_x,accel_y,accel_z,
 *   gyro_x,gyro_y,gyro_z,
 *   pitch,roll
 */
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "sensor_module/sensor_module.h"

// ════════════════════════════════════════════════════════════════════
//  LVGL UI widgets
// ════════════════════════════════════════════════════════════════════
static lv_obj_t *scr_main      = NULL;

// Header
static lv_obj_t *lbl_title     = NULL;
static lv_obj_t *lbl_status    = NULL;

// Flex section
static lv_obj_t *lbl_flex_hdr  = NULL;
static lv_obj_t *bar_flex[NUM_FLEX_SENSORS];
static lv_obj_t *lbl_flex[NUM_FLEX_SENSORS];

// Hall section
static lv_obj_t *lbl_hall_hdr  = NULL;
static lv_obj_t *bar_hall[NUM_HALL_SENSORS];
static lv_obj_t *lbl_hall[NUM_HALL_SENSORS];

// IMU section
static lv_obj_t *lbl_imu_hdr  = NULL;
static lv_obj_t *lbl_imu_data = NULL;

// Sample counter
static lv_obj_t *lbl_counter  = NULL;

// ════════════════════════════════════════════════════════════════════
//  Layout constants
// ════════════════════════════════════════════════════════════════════
static const int SCR_W     = 280;
static const int SIDE_PAD  = 10;
static const int BAR_W     = SCR_W - 2 * SIDE_PAD - 90;  // bar width
static const int BAR_H     = 12;
static const int ROW_H     = 20;     // vertical step per sensor row
static const int SECT_GAP  = 4;      // gap between sections

static const char *finger_names[5] = {
    "Thm", "Idx", "Mid", "Rng", "Pnk"
};

// ════════════════════════════════════════════════════════════════════
//  Colour helpers
// ════════════════════════════════════════════════════════════════════
static lv_color_t col_bg()      { return lv_color_make(0x1A, 0x1A, 0x2E); }
static lv_color_t col_text()    { return lv_color_white(); }
static lv_color_t col_sub()     { return lv_color_make(0x88, 0x88, 0xAA); }
static lv_color_t col_accent()  { return lv_color_make(0x00, 0xE6, 0x76); }  // green
static lv_color_t col_bar_bg()  { return lv_color_make(0x33, 0x33, 0x55); }
static lv_color_t col_hall()    { return lv_color_make(0x00, 0xBB, 0xFF); }  // cyan
static lv_color_t col_neg()     { return lv_color_make(0xFF, 0x44, 0x44); }  // red for negative
static lv_color_t col_section() { return lv_color_make(0xFF, 0xCC, 0x00); }  // yellow headers

// ════════════════════════════════════════════════════════════════════
//  Helper: create a labelled progress bar row
// ════════════════════════════════════════════════════════════════════
static int build_bar_row(lv_obj_t *parent, int y,
                         const char *name,
                         lv_obj_t **bar_out, lv_obj_t **lbl_out,
                         lv_color_t bar_col) {
    // Label (left)
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, col_text(), 0);
    lv_obj_set_pos(lbl, SIDE_PAD, y);

    // Bar
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_bar_set_range(bar, -100, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_pos(bar, SIDE_PAD + 38, y + 1);
    lv_obj_set_style_bg_color(bar, col_bar_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, bar_col, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

    // Value label (right of bar)
    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "  0%");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(val, col_sub(), 0);
    lv_obj_set_pos(val, SCR_W - SIDE_PAD - 42, y + 1);

    *bar_out = bar;
    *lbl_out = val;
    return y + ROW_H;
}

// ════════════════════════════════════════════════════════════════════
//  Build the full-screen UI
// ════════════════════════════════════════════════════════════════════
static void build_ui() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, col_bg(), 0);
    lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    int y = 6;

    // ── Title ────────────────────────────────
    lbl_title = lv_label_create(scr_main);
    lv_label_set_text(lbl_title, LV_SYMBOL_UPLOAD " EI Training");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_title, col_accent(), 0);
    lv_obj_set_pos(lbl_title, SIDE_PAD, y);
    y += 22;

    // ── Status line ──────────────────────────
    lbl_status = lv_label_create(scr_main);
    lv_label_set_text(lbl_status, "Streaming to USB...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, col_sub(), 0);
    lv_obj_set_pos(lbl_status, SIDE_PAD, y);
    y += 16;

    // ── Flex sensors ─────────────────────────
    lbl_flex_hdr = lv_label_create(scr_main);
    lv_label_set_text(lbl_flex_hdr, "FLEX");
    lv_obj_set_style_text_font(lbl_flex_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_flex_hdr, col_section(), 0);
    lv_obj_set_pos(lbl_flex_hdr, SIDE_PAD, y);
    y += 16;

    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        y = build_bar_row(scr_main, y, finger_names[i],
                          &bar_flex[i], &lbl_flex[i], col_accent());
    }
    y += SECT_GAP;

    // ── Hall sensors ─────────────────────────
    lbl_hall_hdr = lv_label_create(scr_main);
    lv_label_set_text(lbl_hall_hdr, "HALL");
    lv_obj_set_style_text_font(lbl_hall_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_hall_hdr, col_section(), 0);
    lv_obj_set_pos(lbl_hall_hdr, SIDE_PAD, y);
    y += 16;

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        y = build_bar_row(scr_main, y, finger_names[i],
                          &bar_hall[i], &lbl_hall[i], col_hall());
    }
    y += SECT_GAP;

    // ── IMU section ──────────────────────────
    lbl_imu_hdr = lv_label_create(scr_main);
    lv_label_set_text(lbl_imu_hdr, "IMU");
    lv_obj_set_style_text_font(lbl_imu_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_imu_hdr, col_section(), 0);
    lv_obj_set_pos(lbl_imu_hdr, SIDE_PAD, y);
    y += 16;

    lbl_imu_data = lv_label_create(scr_main);
    lv_label_set_text(lbl_imu_data,
        "Ax:  0.00  Ay:  0.00  Az:  0.00\n"
        "Gx:  0.00  Gy:  0.00  Gz:  0.00\n"
        "Pitch:  0.0   Roll:  0.0");
    lv_obj_set_style_text_font(lbl_imu_data, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_imu_data, col_text(), 0);
    lv_obj_set_pos(lbl_imu_data, SIDE_PAD, y);
    y += 42;

    // ── Sample counter ───────────────────────
    y += SECT_GAP;
    lbl_counter = lv_label_create(scr_main);
    lv_label_set_text(lbl_counter, "Samples: 0");
    lv_obj_set_style_text_font(lbl_counter, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_counter, col_accent(), 0);
    lv_obj_set_pos(lbl_counter, SIDE_PAD, y);

    lv_scr_load(scr_main);
}

// ════════════════════════════════════════════════════════════════════
//  Update UI with latest processed data
// ════════════════════════════════════════════════════════════════════
static void ui_update(const ProcessedSensorData &pd, uint32_t sample_count) {
    char tmp[16];

    // Flex bars + labels
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        int8_t pct = pd.flex_pct[i];
        lv_bar_set_value(bar_flex[i], pct, LV_ANIM_OFF);
        snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pct);
        lv_label_set_text(lbl_flex[i], tmp);
        // Colour indicator: green=positive, red=negative
        lv_obj_set_style_bg_color(bar_flex[i],
            pct >= 0 ? col_accent() : col_neg(), LV_PART_INDICATOR);
    }

    // Hall bars + labels
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int8_t pct = pd.hall_pct[i];
        lv_bar_set_value(bar_hall[i], pct, LV_ANIM_OFF);
        snprintf(tmp, sizeof(tmp), "%+4d%%", (int)pct);
        lv_label_set_text(lbl_hall[i], tmp);
        lv_obj_set_style_bg_color(bar_hall[i],
            pct >= 0 ? col_hall() : col_neg(), LV_PART_INDICATOR);
    }

    // IMU text
    char imu_buf[160];
    snprintf(imu_buf, sizeof(imu_buf),
        "Ax:%6.2f  Ay:%6.2f  Az:%6.2f\n"
        "Gx:%6.1f  Gy:%6.1f  Gz:%6.1f\n"
        "Pitch:%5.1f   Roll:%5.1f",
        pd.accel_x, pd.accel_y, pd.accel_z,
        pd.gyro_x,  pd.gyro_y,  pd.gyro_z,
        pd.pitch,   pd.roll);
    lv_label_set_text(lbl_imu_data, imu_buf);

    // Sample counter
    snprintf(tmp, sizeof(tmp), "Samples: %lu", (unsigned long)sample_count);
    lv_label_set_text(lbl_counter, tmp);
}

// ════════════════════════════════════════════════════════════════════
//  Serial CSV output for Edge Impulse Data Forwarder
// ════════════════════════════════════════════════════════════════════
static void serial_csv_output(const SensorData &raw) {
    // 18 features: 5 flex + 5 hall + 3 accel + 3 gyro + pitch + roll
    Serial.printf("%u,%u,%u,%u,%u,"
                  "%u,%u,%u,%u,%u,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f\n",
        raw.flex[0], raw.flex[1], raw.flex[2],
        raw.flex[3], raw.flex[4],
        raw.hall[0], raw.hall[1], raw.hall[2],
        raw.hall[3], raw.hall[4],
        raw.accel_x, raw.accel_y, raw.accel_z,
        raw.gyro_x,  raw.gyro_y,  raw.gyro_z,
        raw.pitch,   raw.roll);
}

// ════════════════════════════════════════════════════════════════════
//  State
// ════════════════════════════════════════════════════════════════════
static SensorData        s_raw  = {};
static ProcessedSensorData s_pd = {};
static uint32_t sample_count    = 0;
static uint32_t last_sensor_ms  = 0;
static uint32_t last_display_ms = 0;
static uint32_t last_serial_ms  = 0;

// ════════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(EI_SERIAL_BAUD);
    Serial.setTxTimeoutMs(0);   // non-blocking Serial when no USB host
    delay(200);

    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║  Test_EI_Training — Edge Impulse Collect   ║");
    Serial.println("║  Sensors + LVGL Display + USB CSV Stream   ║");
    Serial.println("╚════════════════════════════════════════════╝\n");

    // 1. Display + LVGL
    display_init();
    Serial.println("[EI_TRAIN] Display ready");

    // 2. Build LVGL UI
    build_ui();
    Serial.println("[EI_TRAIN] UI ready");

    // 3. Sensors (MUX + ADS1115 + MPU6050)
    sensors_init();
    sensors_set_active(true);
    Serial.printf("[EI_TRAIN] Sensors ready (ADS1115: %s, MPU6050: %s)\n",
                  sensors_ads_available() ? "OK" : "N/A",
                  sensors_mpu_available() ? "OK" : "N/A");

    // 4. Sensor processing module (calibration, percentages)
    sensor_module_init();
    Serial.println("[EI_TRAIN] Sensor module ready");

    // Print CSV header for Edge Impulse Data Forwarder reference
    Serial.println("\n[EI_TRAIN] CSV Format (18 features):");
    Serial.println("flex0,flex1,flex2,flex3,flex4,"
                   "hall0,hall1,hall2,hall3,hall4,"
                   "ax,ay,az,gx,gy,gz,pitch,roll\n");

    Serial.println("[EI_TRAIN] Streaming started — connect Edge Impulse Data Forwarder\n");
}

// ════════════════════════════════════════════════════════════════════
//  loop
// ════════════════════════════════════════════════════════════════════
void loop() {
    uint32_t now = millis();

    // ── LVGL tick ──────────────────────────────────────────────────
    lv_timer_handler();

    // ── Read sensors (from background task) ────────────────────────
    if (now - last_sensor_ms >= SENSOR_READ_INTERVAL_MS) {
        last_sensor_ms = now;
        sensors_read(s_raw);
        sensor_module_process(s_raw, s_pd);
    }

    // ── Stream CSV over Serial for Edge Impulse ────────────────────
    if (now - last_serial_ms >= TRAIN_SERIAL_INTERVAL_MS) {
        last_serial_ms = now;
        serial_csv_output(s_raw);
        sample_count++;
    }

    // ── Update display ─────────────────────────────────────────────
    if (now - last_display_ms >= DISPLAY_UPDATE_INTERVAL_MS) {
        last_display_ms = now;
        ui_update(s_pd, sample_count);
    }

    // ── Yield to FreeRTOS ──────────────────────────────────────────
    delay(1);
}
