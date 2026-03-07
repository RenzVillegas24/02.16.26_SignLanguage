/*
 * @file display.cpp
 * @brief AMOLED + FT3168 touch + LVGL driver initialization
 *        Based on LILYGO T-Display S3 AMOLED 1.64 examples
 */
#include "display.h"
#include "power.h"
#include "sensors.h"        // for i2c_mutex

// ── Hardware objects ──────────────────────────
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_GFX *gfx = new Arduino_CO5300(
    bus, LCD_RST, 0 /* rotation */, false /* IPS */,
    LCD_WIDTH, LCD_HEIGHT,
    20 /* col offset 1 */, 0, 0, 0);

static std::shared_ptr<Arduino_IIC_DriveBus> iic_bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static void touch_isr();

std::unique_ptr<Arduino_IIC> touch_controller(
    new Arduino_FT3x68(iic_bus, FT3168_DEVICE_ADDRESS,
                       TP_RST, TP_INT, touch_isr));

static void touch_isr() {
    touch_controller->IIC_Interrupt_Flag = true;
}

// ── LVGL internals ───────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t      disp_drv;

static void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
    lv_disp_flush_ready(disp);
}

// ── Touch state cache ────────────────────────
// LVGL defaults data->state to LV_INDEV_STATE_RELEASED before each read_cb call.
// The FT3168 fires interrupts at ~60 Hz but LVGL polls at 66 Hz (15 ms), so
// there *will* be read cycles with no new hardware data.  Without caching, those
// cycles report RELEASED mid-swipe, which resets the back-gesture tracker.
// We also must not leave state=RELEASED when the mutex was just temporarily busy.
static lv_indev_state_t s_touch_state = LV_INDEV_STATE_REL;
static lv_coord_t       s_touch_x     = 0;
static lv_coord_t       s_touch_y     = 0;
// Stale-press guard: if no interrupt arrives within this many ms while pressed,
// force a release so a stuck state can never happen.
static uint32_t         s_last_irq_ms = 0;
static const uint32_t   TOUCH_STALE_MS = 250;

static void lvgl_touch_cb(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    uint32_t now = millis();

    if (touch_controller->IIC_Interrupt_Flag) {
        // New hardware data is ready — try to read it.
        // Try-lock: if sensor task holds I2C, skip the HW read this cycle
        // but keep reporting the last known state (NOT a spurious release).
        if (i2c_mutex && xSemaphoreTake(i2c_mutex, 0) != pdTRUE) {
            // Bus busy — carry forward last state unchanged.
            // Leave IIC_Interrupt_Flag set so we retry next cycle.
            data->state   = s_touch_state;
            data->point.x = s_touch_x;
            data->point.y = s_touch_y;
            return;
        }

        touch_controller->IIC_Interrupt_Flag = false;
        s_last_irq_ms = now;

        int32_t x = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        int32_t y = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        uint8_t n = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

        if (i2c_mutex) xSemaphoreGive(i2c_mutex);

        if (n > 0) {
            s_touch_state = LV_INDEV_STATE_PR;
            s_touch_x     = (lv_coord_t)x;
            s_touch_y     = (lv_coord_t)y;
        } else {
            s_touch_state = LV_INDEV_STATE_REL;
        }
    } else if (s_touch_state == LV_INDEV_STATE_PR
               && (now - s_last_irq_ms) > TOUCH_STALE_MS) {
        // No interrupt for too long while pressed — force release so we never
        // get stuck in a phantom-pressed state (e.g. if lift interrupt was lost).
        s_touch_state = LV_INDEV_STATE_REL;
    }

    data->state   = s_touch_state;
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
}

static void lvgl_rounder_cb(lv_disp_drv_t * /*drv*/, lv_area_t *area) {
    // CO5300 has artefacts (1 px black line on left edge) with partial-width
    // RAM writes.  Force every dirty band to span the full screen width so
    // CASET always covers columns 0…LCD_WIDTH-1 (hw 20…299).
    // Height stays partial for efficiency; rows are rounded to even.
    area->x1 = 0;
    area->x2 = LCD_WIDTH - 1;        // always full width  (even 280)
    area->y1 = area->y1 & ~1;        // round down to even
    area->y2 = area->y2 | 1;         // round up  to odd  → height is even
}

// ── Brightness tracking ──────────────────────
static uint8_t current_brightness = 200;   // remembers last-set value

// ── Public API ───────────────────────────────
void display_init() {
    // Enable display power
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);

    // Init touch
    if (!touch_controller->begin()) {
        Serial.println("[DISPLAY] FT3168 init FAILED");
    } else {
        Serial.println("[DISPLAY] FT3168 OK");
    }

    // Init GFX  — 80 MHz QSPI (default was 8 MHz; CO5300 supports up to 50 MHz)
    gfx->begin(80000000);
    gfx->fillScreen(BLACK);

    // ── LVGL init ────────────────────────────
    lv_init();

    // Full-screen double-buffer in PSRAM for maximum throughput
    // 280×456×2 bytes ≈ 250 KB per buffer — fits easily in 8 MB PSRAM
    size_t buf_px = LCD_WIDTH * LCD_HEIGHT;
    
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        sizeof(lv_color_t) * buf_px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        sizeof(lv_color_t) * buf_px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // Fallback: if PSRAM unavailable, use smaller internal buffers
    if (!buf1 || !buf2) {
        Serial.println("[DISPLAY] PSRAM alloc failed — falling back to internal RAM");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        buf_px = LCD_WIDTH * 40;
        buf1 = (lv_color_t *)heap_caps_malloc(
            sizeof(lv_color_t) * buf_px, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        buf2 = (lv_color_t *)heap_caps_malloc(
            sizeof(lv_color_t) * buf_px, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    } else {
        Serial.printf("[DISPLAY] PSRAM buffers OK — %u px × 2\n", (unsigned)buf_px);
    }

    if (!buf1 || !buf2) {
        Serial.println("[DISPLAY] LVGL buffer alloc FAILED!");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

    // Display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_WIDTH;
    disp_drv.ver_res      = LCD_HEIGHT;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.rounder_cb   = lvgl_rounder_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh  = 0;    // partial refresh — only redraw dirty areas
    lv_disp_drv_register(&disp_drv);

    // Touch driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    // Fade in — fast path for deep-sleep wakeup
    if (power_is_deep_sleep_wake()) {
        gfx->Display_Brightness(200);       // instant on
    } else {
        for (int i = 0; i <= 255; i++) {
            gfx->Display_Brightness(i);
            delay(2);
        }
    }
    Serial.println("[DISPLAY] LVGL ready");
}

void display_set_brightness(uint8_t level) {
    current_brightness = level;
    gfx->Display_Brightness(level);
}

void display_off() {
    gfx->Display_Brightness(0);
    gfx->displayOff();

    // For light sleep: just power down the display, don't hardware reset it.
    // MIPI DCS SLEEP IN (0x10) puts the CO5300 in sleep mode, preserving all
    // configuration (pixel format, addressing, etc.). The IC draws minimal power.
    // On light sleep wake, displayOn() just sends SLEEP OUT and we're back.
    //
    // For deep sleep / shutdown: the entire system will power-cycle anyway,
    // so the hardware reset is a bonus. But it's not strictly necessary.
    
    // Cut display power to minimise standby current during light sleep
    digitalWrite(LCD_EN, LOW);
}

void display_on() {
    // Restore display power and wake from sleep
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);
    delay(100);   // let LCD power rail stabilize

    // Send MIPI DCS SLEEP OUT (0x11) to wake the CO5300 from sleep mode.
    // All configuration (pixel format, addressing, etc.) is preserved.
    gfx->displayOn();
    gfx->Display_Brightness(current_brightness);
}
