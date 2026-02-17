/*
 * @file display.cpp
 * @brief AMOLED + FT3168 touch + LVGL driver initialization
 *        Based on LILYGO T-Display S3 AMOLED 1.64 examples
 */
#include "display.h"

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

static void lvgl_touch_cb(lv_indev_drv_t * /*drv*/, lv_indev_data_t *data) {
    if (touch_controller->IIC_Interrupt_Flag) {
        touch_controller->IIC_Interrupt_Flag = false;

        int32_t x = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        int32_t y = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        uint8_t n = touch_controller->IIC_Read_Device_Value(
            Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

        if (n > 0) {
            data->state   = LV_INDEV_STATE_PR;
            data->point.x = x;
            data->point.y = y;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
    }
}

static void lvgl_rounder_cb(lv_disp_drv_t * /*drv*/, lv_area_t *area) {
    // CO5300 requires even-aligned coordinates
    if (area->x1 % 2 != 0) area->x1 += 1;
    if (area->y1 % 2 != 0) area->y1 += 1;
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    if (w % 2 != 0) area->x2 -= 1;
    if (h % 2 != 0) area->y2 -= 1;
}

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

    // Init GFX
    gfx->begin();
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
    disp_drv.full_refresh  = 1;
    lv_disp_drv_register(&disp_drv);

    // Touch driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    // Fade in
    for (int i = 0; i <= 255; i++) {
        gfx->Display_Brightness(i);
        delay(2);
    }
    Serial.println("[DISPLAY] LVGL ready");
}

void display_set_brightness(uint8_t level) {
    gfx->Display_Brightness(level);
}

void display_off() {
    gfx->Display_Brightness(0);
    gfx->displayOff();
    digitalWrite(LCD_EN, LOW);
}

void display_on() {
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);
    gfx->displayOn();
    gfx->Display_Brightness(200);
}
