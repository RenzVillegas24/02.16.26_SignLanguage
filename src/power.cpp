/*
 * @file power.cpp
 * @brief Power management — button debounce, battery ADC, SY6970 charger, deep sleep
 */
#include "power.h"
#include "config.h"
#include "esp_sleep.h"
#include "Arduino_DriveBus_Library.h"

// ── SY6970 charger IC ──────────────────────────────────────────────
static std::shared_ptr<Arduino_IIC_DriveBus> pwr_iic_bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

static std::unique_ptr<Arduino_IIC> sy6970(
    new Arduino_SY6970(pwr_iic_bus, SY6970_DEVICE_ADDRESS,
                       DRIVEBUS_DEFAULT_VALUE, DRIVEBUS_DEFAULT_VALUE));

static bool     sy6970_ok       = false;   // init succeeded?
static bool     charging_flag   = false;   // cached: is charging?
static bool     usb_connected   = false;   // cached: input source detected?
static String   charge_status   = "Unknown";
static int      batt_mv         = 0;       // battery mV from SY6970
static int      input_mv        = 0;       // input  mV from SY6970
static int      charge_ma       = 0;       // charging current mA
static uint32_t sy6970_last_read = 0;
#define SY6970_READ_INTERVAL_MS 2000

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

    /* --- SY6970 charger status --- */
    if (sy6970_ok && (millis() - sy6970_last_read > SY6970_READ_INTERVAL_MS)) {
        sy6970_last_read = millis();

        // Charging status string
        charge_status = sy6970->IIC_Read_Device_State(
            sy6970->Arduino_IIC_Power::Status_Information::POWER_CHARGING_STATUS);

        // Bus status (input source type)
        String bus_status = sy6970->IIC_Read_Device_State(
            sy6970->Arduino_IIC_Power::Status_Information::POWER_BUS_STATUS);

        // Determine flags from status strings
        charging_flag = (charge_status == "Pre Chargeing" ||
                         charge_status == "Fast Charging");
        usb_connected = (bus_status != "No Input" &&
                         !bus_status.startsWith("->"));

        // Read voltage / current values
        batt_mv   = (int)sy6970->IIC_Read_Device_Value(
            sy6970->Arduino_IIC_Power::Value_Information::POWER_BATTERY_VOLTAGE);
        input_mv  = (int)sy6970->IIC_Read_Device_Value(
            sy6970->Arduino_IIC_Power::Value_Information::POWER_INPUT_VOLTAGE);
        charge_ma = (int)sy6970->IIC_Read_Device_Value(
            sy6970->Arduino_IIC_Power::Value_Information::POWER_CHARGING_CURRENT);
    }

    /* --- idle auto-sleep --- */
    if (btn_short || btn_long) power_reset_idle_timer();
    if (millis() - idle_start > IDLE_SLEEP_MS)
        power_deep_sleep();
}

bool  power_button_pressed()    { return btn_short; }
bool  power_button_long_press() { return btn_long;  }

float power_battery_voltage() {
    // Prefer the accurate SY6970 reading; fall back to ADC divider
    if (sy6970_ok && batt_mv > 0)
        return batt_mv / 1000.0f;
    return batt_voltage;
}

int power_battery_percent() {
    // Use SY6970 voltage when available for accuracy; fall back to ADC
    float v = (sy6970_ok && batt_mv > 0) ? (batt_mv / 1000.0f) : batt_voltage;
    // LiPo curve: 3.0 V = 0 %, 4.2 V = 100 %
    float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0)    pct = 0;
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

// ── SY6970 public accessors ───────────────────────────────────────
bool power_is_charging()             { return charging_flag; }
bool power_usb_connected()           { return usb_connected; }
const char *power_charging_status_str() { return charge_status.c_str(); }
int  power_battery_voltage_mv()      { return batt_mv; }
int  power_input_voltage_mv()        { return input_mv; }
int  power_charging_current_ma()     { return charge_ma; }
