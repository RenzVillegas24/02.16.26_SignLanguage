/*
 * @file power.cpp
 * @brief Power management — button debounce, SY6970 charger (background task),
 *        light sleep, deep sleep (shutdown), restart
 *
 * SY6970 I2C reads run on Core 0 in a FreeRTOS task every 500 ms.
 * The main loop (Core 1) only reads mutex-protected cached values — no I2C blocking.
 *
 * Power button (PIN_POWER_BTN / GPIO 2):
 *   Short press  — consumed by main.cpp (general use)
 *   Long press   — consumed by main.cpp to show the power menu dialog
 *
 * Light sleep:  Display off → esp_light_sleep_start().
 *               Button press wakes and resumes exactly where it left off.
 * Deep sleep:   Display off → esp_deep_sleep_start().
 *               Button press resets the ESP32 (full restart).
 * Restart:      Calls esp_restart() immediately.
 */
#include "power.h"
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "Arduino_DriveBus_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Wire.h>

// ── SY6970 charger IC ──────────────────────────────────────────────
static std::shared_ptr<Arduino_IIC_DriveBus> pwr_iic_bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static std::unique_ptr<Arduino_IIC> sy6970(
    new Arduino_SY6970(pwr_iic_bus, SY6970_DEVICE_ADDRESS,
                       DRIVEBUS_DEFAULT_VALUE, DRIVEBUS_DEFAULT_VALUE));

static bool sy6970_ok = false;   // set once in power_init, read-only after

// ── Cached power state (updated by background task, read by main loop) ──
static SemaphoreHandle_t pwr_mutex          = nullptr;
static PowerInfo         s_info             = {};      // full SY6970 snapshot
static bool              usb_changed_flag   = false;   // edge-detect: USB in/out

// ── Background task control ────────────────────────────────────────
static TaskHandle_t      pwr_task_handle    = nullptr;
static volatile bool     pwr_task_active    = true;

// ── Button debounce state ──────────────────────────────────────────
static uint32_t btn_down_at     = 0;
static bool     btn_last        = HIGH;
static bool     btn_short       = false;
static bool     btn_long        = false;
static bool     btn_long_fired  = false;

// ── Idle tracking (auto-sleep logic lives in main.cpp) ─────────────
static uint32_t idle_start = 0;

// Flush serial with a hard timeout — ESP32-S3 USB CDC can block
// indefinitely on Serial.flush() when no USB host is connected.
static void serial_flush_safe(uint32_t timeout_ms = 50) {
    uint32_t t0 = millis();
    while ((millis() - t0) < timeout_ms) {
        if (Serial.availableForWrite() >= 128) break;   // TX buffer drained
        delay(1);
    }
}

// ── Deep-sleep wakeup detection ────────────────────────────────────
// Check the wakeup reason once at boot so startup code can fast-path.
static bool _deep_sleep_wake = false;

bool power_is_deep_sleep_wake() { return _deep_sleep_wake; }

// Helper: copy an Arduino String into a fixed char buffer.
static void str_copy(char *dst, size_t sz, const String &src) {
    strncpy(dst, src.c_str(), sz - 1);
    dst[sz - 1] = '\0';
}

// ── Background SY6970 read task (Core 0, 500 ms period) ───────────
// Uses i2c_mutex to coordinate Wire access with the sensor task and
// the touch controller.  All reads are batched under a single mutex
// hold (~10 ms) — the sensor task handles the brief delay gracefully.
static void pwr_read_task(void* /*arg*/) {
    for (;;) {
        if (!pwr_task_active) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (sy6970_ok && i2c_mutex) {
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) {
                // ── Values ──
                int   _bmv  = (int)sy6970->IIC_Read_Device_Value(
                    sy6970->Arduino_IIC_Power::Value_Information::POWER_BATTERY_VOLTAGE);
                int   _smv  = (int)sy6970->IIC_Read_Device_Value(
                    sy6970->Arduino_IIC_Power::Value_Information::POWER_SYSTEM_VOLTAGE);
                int   _imv  = (int)sy6970->IIC_Read_Device_Value(
                    sy6970->Arduino_IIC_Power::Value_Information::POWER_INPUT_VOLTAGE);
                int   _cma  = (int)sy6970->IIC_Read_Device_Value(
                    sy6970->Arduino_IIC_Power::Value_Information::POWER_CHARGING_CURRENT);
                float _ntc  = (float)sy6970->IIC_Read_Device_Value(
                    sy6970->Arduino_IIC_Power::Value_Information::POWER_NTC_VOLTAGE_PERCENTAGE) / 1000.0f;

                // ── Statuses ──
                String _cs  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_CHARGING_STATUS);
                String _bs  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_BUS_STATUS);
                String _bc  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_BUS_CONNECTION_STATUS);
                String _is  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_INPUT_SOURCE_STATUS);
                String _iu  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_INPUT_USB_STATUS);
                String _sv  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_SYSTEM_VOLTAGE_STATUS);
                String _tr  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_THERMAL_REGULATION_STATUS);

                // ── Faults ──
                String _cf  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_CHARGING_FAULT_STATUS);
                String _bf  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_BATTERY_FAULT_STATUS);
                String _nf  = sy6970->IIC_Read_Device_State(
                    sy6970->Arduino_IIC_Power::Status_Information::POWER_NTC_FAULT_STATUS);

                xSemaphoreGive(i2c_mutex);

                // ── Derive booleans ──
                bool cf = (_cs == "Pre Chargeing" || _cs == "Fast Charging");
                bool uc = (_bs != "No Input" && !_bs.startsWith("->"));

                // ── Copy into shared struct under pwr_mutex ──
                xSemaphoreTake(pwr_mutex, portMAX_DELAY);
                bool old_usb = s_info.usb_connected;
                s_info.is_charging   = cf;
                s_info.usb_connected = uc;
                if (uc != old_usb) usb_changed_flag = true;
                if (_bmv > 0) s_info.battery_mv = _bmv;
                s_info.system_mv  = _smv;
                s_info.input_mv   = _imv;
                s_info.charge_ma  = _cma;
                s_info.ntc_pct    = _ntc;

                str_copy(s_info.charge_status,     sizeof(s_info.charge_status),     _cs);
                str_copy(s_info.bus_status,         sizeof(s_info.bus_status),         _bs);
                str_copy(s_info.bus_connection,     sizeof(s_info.bus_connection),     _bc);
                str_copy(s_info.input_source,       sizeof(s_info.input_source),       _is);
                str_copy(s_info.input_usb,           sizeof(s_info.input_usb),           _iu);
                str_copy(s_info.sys_voltage_status, sizeof(s_info.sys_voltage_status), _sv);
                str_copy(s_info.thermal_reg_status, sizeof(s_info.thermal_reg_status), _tr);
                str_copy(s_info.charging_fault,     sizeof(s_info.charging_fault),     _cf);
                str_copy(s_info.battery_fault,      sizeof(s_info.battery_fault),      _bf);
                str_copy(s_info.ntc_fault,           sizeof(s_info.ntc_fault),           _nf);

                // Battery percentage (LiPo 3.0V = 0%, 4.2V = 100%)
                float pct = (s_info.battery_mv / 1000.0f - 3.0f) / (4.2f - 3.0f) * 100.0f;
                s_info.battery_pct = constrain((int)pct, 0, 100);

                xSemaphoreGive(pwr_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void power_init() {
    pwr_mutex  = xSemaphoreCreateMutex();
    idle_start = millis();

    pinMode(PIN_POWER_BTN, INPUT_PULLUP);

    // ── Detect deep-sleep wakeup ─────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    _deep_sleep_wake = (cause == ESP_SLEEP_WAKEUP_EXT0 ||
                        cause == ESP_SLEEP_WAKEUP_EXT1);
    if (_deep_sleep_wake)
        Serial.println("[POWER] Boot reason: deep-sleep wakeup");

    // ── SY6970 charger init ──────────────────────────────────────
    // All SY6970 I2C must go through i2c_mutex — but sensors_init()
    // may or may not have run yet.  Take the mutex if it exists;
    // otherwise proceed unguarded (safe during single-threaded setup).
    bool have_mutex = (i2c_mutex != nullptr);

    if (have_mutex) xSemaphoreTake(i2c_mutex, portMAX_DELAY);

    if (sy6970->begin()) {
        Serial.println("[POWER] SY6970 init OK");

        // Enable ADC measurement (required to read voltages/currents)
        if (sy6970->IIC_Write_Device_State(
                sy6970->Arduino_IIC_Power::Device::POWER_DEVICE_ADC_MEASURE,
                sy6970->Arduino_IIC_Power::Device_State::POWER_DEVICE_ON)) {
            Serial.println("[POWER] SY6970 ADC Measure ON");
        } else {
            Serial.println("[POWER] SY6970 ADC Measure FAILED");
        }

        // ── Full SY6970 configuration (per reference example) ──────
        // Disable watchdog timer (prevent auto-reset of settings)
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_WATCHDOG_TIMER, 0);
        // Thermal regulation threshold 60 °C
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_THERMAL_REGULATION_THRESHOLD, 60);
        // Charging target voltage 4224 mV
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_CHARGING_TARGET_VOLTAGE_LIMIT, 4224);
        // Minimum system voltage limit 3600 mV
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_MINIMUM_SYSTEM_VOLTAGE_LIMIT, 3600);
        // OTG voltage limit 5062 mV
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_OTG_VOLTAGE_LIMIT, 5062);
        // Input current limit 600 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_INPUT_CURRENT_LIMIT, 600);
        // Fast charging current limit 2112 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_FAST_CHARGING_CURRENT_LIMIT, 2112);
        // Precharge current limit 192 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_PRECHARGE_CHARGING_CURRENT_LIMIT, 192);
        // Termination charging current limit 320 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_TERMINATION_CHARGING_CURRENT_LIMIT, 320);
        // OTG current limit 500 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_OTG_CHARGING_LIMIT, 500);

        Serial.println("[POWER] SY6970 configuration applied");
        sy6970_ok = true;
    } else {
        Serial.println("[POWER] SY6970 init FAILED — charger monitoring disabled");
        sy6970_ok = false;
    }

    if (have_mutex) xSemaphoreGive(i2c_mutex);

    // Start background reader on Core 0 (stack 4 kB, priority 2)
    pwr_task_active = true;
    xTaskCreatePinnedToCore(pwr_read_task, "pwr_read", 4096, nullptr, 2, &pwr_task_handle, 0);
}

void power_update() {
    // Button debounce only — no I2C here
    bool now = digitalRead(PIN_POWER_BTN);
    btn_short = false;
    btn_long  = false;

    if (btn_last == HIGH && now == LOW) {          // pressed edge
        btn_down_at    = millis();
        btn_long_fired = false;
    }
    else if (now == LOW) {                          // held
        if (!btn_long_fired && (millis() - btn_down_at > 2000)) {
            btn_long       = true;
            btn_long_fired = true;
        }
    }
    else if (btn_last == LOW && now == HIGH) {      // released edge
        if (!btn_long_fired && (millis() - btn_down_at > 50))
            btn_short = true;
    }
    btn_last = now;

    // Reset idle on physical button press (auto-sleep managed by main.cpp)
    if (btn_short || btn_long) power_reset_idle_timer();
}

bool power_button_pressed()    { return btn_short; }
bool power_button_long_press() { return btn_long;  }

// ── Public accessors (mutex-protected reads of cached state) ──────

float power_battery_voltage() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    float v = s_info.battery_mv / 1000.0f;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_battery_percent() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int p = s_info.battery_pct;
    xSemaphoreGive(pwr_mutex);
    return p;
}

bool power_is_charging() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    bool v = s_info.is_charging;
    xSemaphoreGive(pwr_mutex);
    return v;
}

bool power_usb_connected() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    bool v = s_info.usb_connected;
    xSemaphoreGive(pwr_mutex);
    return v;
}

/** Returns true once per USB plug/unplug edge; resets the flag. */
bool power_usb_state_changed() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    bool v = usb_changed_flag;
    usb_changed_flag = false;
    xSemaphoreGive(pwr_mutex);
    return v;
}

const char *power_charging_status_str() {
    // Copy into a static buffer so the caller gets a stable pointer
    static char buf[32];
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    strncpy(buf, s_info.charge_status, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    xSemaphoreGive(pwr_mutex);
    return buf;
}

int power_battery_voltage_mv() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = s_info.battery_mv;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_system_voltage_mv() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = s_info.system_mv;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_input_voltage_mv() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = s_info.input_mv;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_charging_current_ma() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = s_info.charge_ma;
    xSemaphoreGive(pwr_mutex);
    return v;
}

/// Thread-safe snapshot of all SY6970 data — zero-copy struct return
PowerInfo power_get_info() {
    PowerInfo out;
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    out = s_info;
    xSemaphoreGive(pwr_mutex);
    return out;
}

// ── Light Sleep ────────────────────────────────────────────────────
// Puts the ESP32 into light sleep. Execution pauses and resumes here
// when the power button (PIN_POWER_BTN) is pressed.
// Display and touch are powered down before sleep and restored after.

// Shared helper: configure the wakeup GPIO for RTC domain.
// During any sleep mode the GPIO matrix and normal pull-ups are
// powered off, so we must use the RTC GPIO driver to keep the
// pull-up alive and to register the ext0 wakeup source.
static void _arm_wakeup_gpio() {
    const gpio_num_t pin = (gpio_num_t)PIN_POWER_BTN;
    // Route the GPIO pad into the RTC domain
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    // Enable RTC-domain pull-up so the pin stays HIGH when button is open
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
    // Wake on LOW (button connects pin → GND)
    esp_sleep_enable_ext0_wakeup(pin, 0);
}

void power_light_sleep() {
    Serial.println("[POWER] Entering light sleep...");
    serial_flush_safe();

    // 1. Power down display
    display_off();

    // 2. Put touch controller into low-power monitor mode
    if (touch_controller) {
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
            touch_controller->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_AUTOMATICALLY_MONITOR_MODE,
            touch_controller->Arduino_IIC_Touch::Device_State::TOUCH_DEVICE_ON);
        touch_controller->IIC_Write_Device_Value(
            touch_controller->Arduino_IIC_Touch::Device_Value::TOUCH_AUTOMATICALLY_MONITOR_TIME, 3);
    }

    // 3. Small settle delay — let I2C / QSPI finish
    delay(100);

    // 4. Arm wakeup GPIO (RTC pull-up + ext0 on LOW)
    _arm_wakeup_gpio();

    // 5. Enter light sleep — execution pauses here
    esp_light_sleep_start();

    // ── Woken up! ──────────────────────────────────────────────────
    // Immediately release the RTC GPIO back to the normal GPIO matrix
    // so digitalRead() works again.
    rtc_gpio_deinit((gpio_num_t)PIN_POWER_BTN);
    pinMode(PIN_POWER_BTN, INPUT_PULLUP);

    Serial.println("[POWER] Woke from light sleep");

    // 6. Wait for the wake-up button press to be fully released
    //    (up to 3 s), then flush the debounce state so that the
    //    physical press that woke us is NOT re-processed as a new
    //    short/long press in the main loop.
    uint32_t release_deadline = millis() + 3000;
    while (digitalRead(PIN_POWER_BTN) == LOW && millis() < release_deadline) {
        delay(10);
    }
    delay(50); // extra settle time

    // Reset debounce state — treat the button as already released
    btn_last       = HIGH;
    btn_down_at    = 0;
    btn_long_fired = false;
    btn_short      = false;
    btn_long       = false;

    // 7. Restore display (with settling delay for LCD power rail)
    delay(100);
    display_on();

    // 8. Restore touch controller to full active mode
    if (touch_controller) {
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
            touch_controller->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_ACTIVE);
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_AUTOMATICALLY_MONITOR_MODE,
            touch_controller->Arduino_IIC_Touch::Device_State::TOUCH_DEVICE_OFF);
    }

    // 9. Reset idle timer so we don't immediately re-sleep
    power_reset_idle_timer();
}

// ── Deep Sleep (Shutdown) ──────────────────────────────────────────
// Puts the ESP32 into deep sleep. This is effectively a shutdown.
// Pressing the power button triggers a full reset/restart.
//
// We aggressively shut down every peripheral and isolate every GPIO so
// that only the RTC domain + GPIO 2 (wakeup button) remain powered.
void power_deep_sleep() {
    Serial.println("[POWER] Entering deep sleep (shutdown)...");
    serial_flush_safe();

    // ── 1. Power down display ─────────────────────────────────────
    display_off();

    // ── 2. Put touch controller into lowest-power mode ────────────
    if (touch_controller) {
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
            touch_controller->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    }

    // ── 3. Stop power-monitoring background task ──────────────────
    //    Must happen BEFORE Wire.end() to prevent I2C access after
    //    the bus is torn down (was causing crash before deep sleep).
    pwr_task_active = false;
    if (pwr_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(60));   // let task reach idle loop
        vTaskDelete(pwr_task_handle);
        pwr_task_handle = nullptr;
    }

    // ── 4. Shut down sensors (MPU6050 → sleep, mux GPIOs → high-Z)
    sensors_shutdown();

    // ── 5. End I2C bus — releases SDA/SCL GPIOs (6, 7) ─────────────
    //    Both MPU6050 and FT3168 are now in low-power modes,
    //    so no further I2C traffic is needed.
    Wire.end();
    pinMode(IIC_SDA, INPUT);
    pinMode(IIC_SCL, INPUT);

    // ── 6. Isolate touch controller GPIOs ─────────────────────
    pinMode(TP_INT, INPUT);
    pinMode(TP_RST, INPUT);

    // ── 7. Isolate I2S / audio GPIOs ──────────────────────────
    //    audio_stop() was already called from main.cpp, but the I2S
    //    driver may leave the pins configured as output.  Float them
    //    so the MAX98357A amp doesn't see phantom clocks.
    pinMode(I2S_BCLK, INPUT);
    pinMode(I2S_LRCK, INPUT);
    pinMode(I2S_DOUT, INPUT);

    // ── 8. Isolate QSPI display bus GPIOs ─────────────────────
    //    display_off() cut LCD_EN (power) but the QSPI pins may
    //    still be configured as SPI outputs. Float them.
    pinMode(LCD_CS,    INPUT);
    pinMode(LCD_SCLK,  INPUT);
    pinMode(LCD_SDIO0, INPUT);
    pinMode(LCD_SDIO1, INPUT);
    pinMode(LCD_SDIO2, INPUT);
    pinMode(LCD_SDIO3, INPUT);
    pinMode(LCD_RST,   INPUT);
    pinMode(LCD_EN,    INPUT);     // already LOW from display_off()

    // ── 9. Small settle — let everything quiesce ──────────────────
    delay(100);

    // ── 10. Arm wakeup GPIO (RTC pull-up + ext0 on LOW) ─────────
    //     Only GPIO 2 stays active in the RTC domain.
    _arm_wakeup_gpio();

    // ── 11. Activate GPIO hold — locks every gpio_hold_en() pin LOW
    //     through deep sleep even after the digital domain powers down.
    //     Without this, held pins would revert to floating on power-down.
    gpio_deep_sleep_hold_en();

    Serial.println("[POWER] All peripherals shut down, entering deep sleep NOW");
    serial_flush_safe();

    // ── 12. Enter deep sleep — never returns; ESP resets on wakeup ─
    esp_deep_sleep_start();

    // Should never reach here — but if it does, restart as fallback
    Serial.println("[POWER] WARNING: esp_deep_sleep_start() returned! Restarting...");
    delay(100);
    esp_restart();
}

// ── Restart ────────────────────────────────────────────────────────
void power_restart() {
    Serial.println("[POWER] Restarting...");
    serial_flush_safe();
    delay(100);
    esp_restart();
}

void power_reset_idle_timer() { idle_start = millis(); }

uint32_t power_idle_elapsed_ms() { return millis() - idle_start; }
