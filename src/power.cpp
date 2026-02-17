/*
 * @file power.cpp
 * @brief Power management — button debounce, battery ADC, deep sleep
 */
#include "power.h"
#include "config.h"
#include "esp_sleep.h"

// ── Button debounce state ──────────────────────────────────────────
static uint32_t btn_down_at     = 0;
static bool     btn_last        = HIGH;
static bool     btn_short       = false;
static bool     btn_long        = false;
static bool     btn_long_fired  = false;

// ── Battery smoothing ──────────────────────────────────────────────
static float   batt_voltage     = 0.0f;
static uint32_t batt_last_read  = 0;
#define BATT_READ_INTERVAL_MS   2000

// ── Idle / auto-sleep ──────────────────────────────────────────────
static uint32_t idle_start      = 0;
#define IDLE_SLEEP_MS           (5UL * 60 * 1000)     // 5 minutes

void power_init() {
    pinMode(PIN_POWER_BTN, INPUT_PULLUP);
    analogReadResolution(12);
    idle_start  = millis();
    batt_voltage = 0;
}

void power_update() {
    /* --- button sampling --- */
    bool now = digitalRead(PIN_POWER_BTN);
    btn_short = false;
    btn_long  = false;

    if (btn_last == HIGH && now == LOW) {           // pressed edge
        btn_down_at    = millis();
        btn_long_fired = false;
    }
    else if (now == LOW) {                           // held
        if (!btn_long_fired && (millis() - btn_down_at > 2000)) {
            btn_long       = true;
            btn_long_fired = true;
        }
    }
    else if (btn_last == LOW && now == HIGH) {       // released edge
        if (!btn_long_fired && (millis() - btn_down_at > 50))
            btn_short = true;
    }
    btn_last = now;

    /* --- battery ADC --- */
    if (millis() - batt_last_read > BATT_READ_INTERVAL_MS) {
        batt_last_read = millis();
        int raw = analogRead(PIN_BAT_ADC);
        float v = raw * 3.3f / 4095.0f * 2.0f;      // voltage divider ×2
        if (batt_voltage < 0.1f) batt_voltage = v;   // first reading
        else batt_voltage = batt_voltage * 0.8f + v * 0.2f;  // EMA
    }

    /* --- idle auto-sleep --- */
    if (btn_short || btn_long) power_reset_idle_timer();
    if (millis() - idle_start > IDLE_SLEEP_MS)
        power_deep_sleep();
}

bool  power_button_pressed()    { return btn_short; }
bool  power_button_long_press() { return btn_long;  }

float power_battery_voltage() { return batt_voltage; }

int power_battery_percent() {
    // Simple LiPo approximation  3.0 V = 0 %,  4.2 V = 100 %
    float pct = (batt_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0)   pct = 0;
    if (pct > 100)  pct = 100;
    return (int)pct;
}

void power_deep_sleep() {
    Serial.println("[POWER] Entering deep sleep");
    Serial.flush();
    // Turn off display
    digitalWrite(LCD_EN, LOW);
    // Wake on power-button press (GPIO 38 LOW)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_POWER_BTN, 0);
    esp_deep_sleep_start();
}

void power_reset_idle_timer() { idle_start = millis(); }
