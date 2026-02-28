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
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "Arduino_DriveBus_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ── SY6970 charger IC ──────────────────────────────────────────────
static std::shared_ptr<Arduino_IIC_DriveBus> pwr_iic_bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static std::unique_ptr<Arduino_IIC> sy6970(
    new Arduino_SY6970(pwr_iic_bus, SY6970_DEVICE_ADDRESS,
                       DRIVEBUS_DEFAULT_VALUE, DRIVEBUS_DEFAULT_VALUE));

static bool sy6970_ok = false;   // set once in power_init, read-only after

// ── Cached power state (updated by background task, read by main loop) ──
static SemaphoreHandle_t pwr_mutex          = nullptr;
static bool              charging_flag      = false;
static bool              usb_connected      = false;
static bool              usb_changed_flag   = false;   // edge-detect: USB in/out
static String            charge_status      = "Unknown";
static int               batt_mv            = 0;
static int               input_mv           = 0;
static int               charge_ma          = 0;

// ── Button debounce state ──────────────────────────────────────────
static uint32_t btn_down_at     = 0;
static bool     btn_last        = HIGH;
static bool     btn_short       = false;
static bool     btn_long        = false;
static bool     btn_long_fired  = false;

// ── Idle / auto-sleep ──────────────────────────────────────────────
static uint32_t idle_start = 0;
#define IDLE_SLEEP_MS (5UL * 60 * 1000)   // 5 minutes

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

// ── Background SY6970 read task (Core 0, 500 ms period) ───────────
static void pwr_read_task(void* /*arg*/) {
    for (;;) {
        if (sy6970_ok) {
            // Perform all I2C reads OUTSIDE the mutex so the main loop
            // is never blocked by I2C.  Wire is internally mutex-protected
            // so concurrent access with sensors.cpp (MPU6050) is safe.
            String cs = sy6970->IIC_Read_Device_State(
                sy6970->Arduino_IIC_Power::Status_Information::POWER_CHARGING_STATUS);
            String bs = sy6970->IIC_Read_Device_State(
                sy6970->Arduino_IIC_Power::Status_Information::POWER_BUS_STATUS);

            bool cf = (cs == "Pre Chargeing" || cs == "Fast Charging");
            bool uc = (bs != "No Input" && !bs.startsWith("->"));

            int bmv = (int)sy6970->IIC_Read_Device_Value(
                sy6970->Arduino_IIC_Power::Value_Information::POWER_BATTERY_VOLTAGE);
            int imv = (int)sy6970->IIC_Read_Device_Value(
                sy6970->Arduino_IIC_Power::Value_Information::POWER_INPUT_VOLTAGE);
            int cma = (int)sy6970->IIC_Read_Device_Value(
                sy6970->Arduino_IIC_Power::Value_Information::POWER_CHARGING_CURRENT);

            // Short critical section: just copy the results into shared state
            xSemaphoreTake(pwr_mutex, portMAX_DELAY);
            bool old_usb   = usb_connected;
            charging_flag  = cf;
            usb_connected  = uc;
            if (uc != old_usb) usb_changed_flag = true;   // edge detected
            if (bmv > 0) batt_mv = bmv;
            input_mv      = imv;
            charge_ma     = cma;
            charge_status = cs;
            xSemaphoreGive(pwr_mutex);
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

        // Disable watchdog timer (prevent auto-reset of settings)
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_WATCHDOG_TIMER, 0);

        // Thermal regulation threshold 60 °C
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_THERMAL_REGULATION_THRESHOLD, 60);

        // Charging target voltage 4208 mV (standard LiPo)
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_CHARGING_TARGET_VOLTAGE_LIMIT, 4208);

        // Input current limit 500 mA
        sy6970->IIC_Write_Device_Value(
            sy6970->Arduino_IIC_Power::Device_Value::POWER_DEVICE_INPUT_CURRENT_LIMIT, 500);

        sy6970_ok = true;
    } else {
        Serial.println("[POWER] SY6970 init FAILED — charger monitoring disabled");
        sy6970_ok = false;
    }

    // Start background reader on Core 0 (stack 4 kB, priority 2)
    xTaskCreatePinnedToCore(pwr_read_task, "pwr_read", 4096, nullptr, 2, nullptr, 0);
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

    // Idle auto-sleep (light sleep after inactivity)
    if (btn_short || btn_long) power_reset_idle_timer();
    if (millis() - idle_start > IDLE_SLEEP_MS)
        power_light_sleep();
}

bool power_button_pressed()    { return btn_short; }
bool power_button_long_press() { return btn_long;  }

// ── Public accessors (mutex-protected reads of cached state) ──────

float power_battery_voltage() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    float v = batt_mv / 1000.0f;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_battery_percent() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int mv = batt_mv;
    xSemaphoreGive(pwr_mutex);
    if (mv <= 0) return 0;
    // LiPo curve: 3.0 V = 0 %, 4.2 V = 100 %
    float pct = (mv / 1000.0f - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (int)pct;
}

bool power_is_charging() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    bool v = charging_flag;
    xSemaphoreGive(pwr_mutex);
    return v;
}

bool power_usb_connected() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    bool v = usb_connected;
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
    strncpy(buf, charge_status.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    xSemaphoreGive(pwr_mutex);
    return buf;
}

int power_battery_voltage_mv() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = batt_mv;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_input_voltage_mv() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = input_mv;
    xSemaphoreGive(pwr_mutex);
    return v;
}

int power_charging_current_ma() {
    xSemaphoreTake(pwr_mutex, portMAX_DELAY);
    int v = charge_ma;
    xSemaphoreGive(pwr_mutex);
    return v;
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
void power_deep_sleep() {
    Serial.println("[POWER] Entering deep sleep (shutdown)...");
    serial_flush_safe();

    // 1. Power down display
    display_off();

    // 2. Put touch controller into low-power monitor mode
    if (touch_controller) {
        touch_controller->IIC_Write_Device_State(
            touch_controller->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
            touch_controller->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    }

    // 3. Small settle delay — let I2C / QSPI transactions complete
    delay(300);

    // 4. Arm wakeup GPIO (RTC pull-up + ext0 on LOW)
    //    Without rtc_gpio_pullup_en the pin floats during deep sleep
    //    and the wakeup never fires.
    _arm_wakeup_gpio();

    Serial.println("[POWER] GPIO armed, entering deep sleep NOW");
    serial_flush_safe();

    // 5. Enter deep sleep — never returns; ESP resets on wakeup
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
