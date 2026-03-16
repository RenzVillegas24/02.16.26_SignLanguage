/*
 * @file sensors.cpp
 * @brief CD74HC4067 multiplexer + ADS1115 16-bit ADC + MPU6050 IMU
 *
 * Sensor reading runs on a FreeRTOS task pinned to Core 0 with
 * vTaskDelayUntil() for precise 30 Hz pacing.  I2C bus runs at
 * 400 kHz Fast-mode; the full read cycle (~16 ms) fits comfortably
 * inside the 33 ms period.
 *
 * An I2C mutex (i2c_mutex) coordinates Wire access between:
 *   • Sensor background task   — holds mutex during each read batch
 *   • FT3168 touch controller  — try-locks in the LVGL input callback
 */
#include "sensors.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "driver/gpio.h"

// ── Lightweight MPU6050 driver (no heavy library) ──
// Register addresses
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C

static bool  mpu_ok = false;
static Adafruit_ADS1115 ads;
static bool  ads_ok = false;

// ── I2C bus mutex (shared with touch controller in display.cpp) ──
SemaphoreHandle_t i2c_mutex = NULL;

// ── Background sensor task ──────────────────
static TaskHandle_t  s_task       = NULL;
static volatile bool s_active     = true;       // false → task idles (menu modes)
static SensorData    s_shared     = {};         // latest data (written by task, read by main)
static portMUX_TYPE  s_spin       = portMUX_INITIALIZER_UNLOCKED;

// Optional 30 Hz streaming callback (invoked from Core 0 sensor task)
static volatile SensorStreamCallback s_stream_cb = NULL;

// Configurable task period (default from SENSOR_READ_INTERVAL_MS)
static volatile uint32_t s_period_ms = SENSOR_READ_INTERVAL_MS;

// ── I2C bus scanner ──────────────────────────
static void i2c_scan() {
    Serial.println("[SENSORS] I2C bus scan:");
    uint8_t count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[SENSORS]   0x%02X", addr);
            if (addr == MPU6050_ADDR)      Serial.print(" (MPU6050)");
            else if (addr == ADS1115_ADDR) Serial.print(" (ADS1115)");
            else if (addr == 0x38)         Serial.print(" (FT3168 touch)");
            Serial.println();
            count++;
        }
    }
    if (count == 0)
        Serial.println("[SENSORS]   ** No devices found — check wiring! **");
    else
        Serial.printf("[SENSORS]   %d device(s) on bus\n", count);
}

// ── Mux helpers ──────────────────────────────
static void mux_init() {
    gpio_hold_dis((gpio_num_t)MUX_S0);
    gpio_hold_dis((gpio_num_t)MUX_S1);
    gpio_hold_dis((gpio_num_t)MUX_S2);
    gpio_hold_dis((gpio_num_t)MUX_S3);

    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
}

static void mux_select(uint8_t ch) {
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    digitalWrite(MUX_S3, (ch >> 3) & 1);
    delayMicroseconds(MUX_SETTLE_US);
}

/// Read ADS1115 A0 (or fall back to ESP32 ADC).  Caller must hold i2c_mutex.
static uint16_t ads_or_adc_read() {
    if (ads_ok) {
        int16_t raw = ads.readADC_SingleEnded(0);
        return (raw < 0) ? 0 : (uint16_t)raw;
    }
    return analogRead(MUX_SIG);
}

// ── MPU6050 low-level ────────────────────────
static void mpu_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static bool mpu_init() {
    // Check WHO_AM_I
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_WHO_AM_I);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t id = Wire.read();
        if (id != 0x68 && id != 0x72) {
            Serial.printf("[SENSORS] MPU6050 WHO_AM_I = 0x%02X (unexpected)\n", id);
            return false;
        }
    } else {
        return false;
    }

    mpu_write(MPU6050_PWR_MGMT_1, 0x00);  // Wake up
    mpu_write(MPU6050_CONFIG, 0x03);       // DLPF ~44Hz
    mpu_write(MPU6050_GYRO_CONFIG, 0x00);  // ±250°/s
    mpu_write(MPU6050_ACCEL_CONFIG, 0x00); // ±2g
    return true;
}

static void mpu_read_raw(int16_t *ax, int16_t *ay, int16_t *az,
                          int16_t *gx, int16_t *gy, int16_t *gz) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_ACCEL_XOUT_H);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);

    *ax = (Wire.read() << 8) | Wire.read();
    *ay = (Wire.read() << 8) | Wire.read();
    *az = (Wire.read() << 8) | Wire.read();
    Wire.read(); Wire.read(); // Temperature (skip)
    *gx = (Wire.read() << 8) | Wire.read();
    *gy = (Wire.read() << 8) | Wire.read();
    *gz = (Wire.read() << 8) | Wire.read();
}

// ── Background sensor task ───────────────────
// Reads 5 flex + 5 hall channels + MPU6050 in three short I2C batches
// with 1-tick mutex-free gaps between them so the touch controller on
// Core 1 can reliably read the FT3168.  vTaskDelayUntil() absorbs the
// inter-batch delays — the 30 Hz rate is maintained precisely.
static void sensor_task_fn(void *) {
    static const uint8_t flex_ch[NUM_FLEX_SENSORS] = {
        MUX_CH_FLEX_THUMB,  MUX_CH_FLEX_INDEX,  MUX_CH_FLEX_MIDDLE,
        MUX_CH_FLEX_RING,   MUX_CH_FLEX_PINKY
    };

    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        if (!s_active) {
            vTaskDelay(pdMS_TO_TICKS(100));   // idle — check again in 100 ms
            xLastWake = xTaskGetTickCount();   // reset epoch after idle
            continue;
        }

        const TickType_t xPeriod = pdMS_TO_TICKS(s_period_ms);

        SensorData d = {};

        // ── Flex sensors (5 channels, ~7 ms at 400 kHz) ──
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(30))) {
            for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
                mux_select(flex_ch[i]);
                d.flex[i] = ads_or_adc_read();
            }
            xSemaphoreGive(i2c_mutex);
        }

        // 1-tick window for the touch controller to grab the I2C bus.
        // Absorbed by vTaskDelayUntil — does NOT add to the 33 ms period.
        vTaskDelay(1);

        // ── Hall sensors (5 channels, ~7 ms at 400 kHz) ──
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(30))) {
            for (int i = 0; i < NUM_HALL_SENSORS; i++) {
                mux_select(MUX_CH_HALL_THUMB + i);
                d.hall[i] = ads_or_adc_read();
            }
            xSemaphoreGive(i2c_mutex);
        }

        // Another 1-tick window before the MPU read.
        vTaskDelay(1);

        // ── MPU6050 (single burst read, ~0.5 ms at 400 kHz) ──
        if (mpu_ok) {
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10))) {
                int16_t ax, ay, az, gx, gy, gz;
                mpu_read_raw(&ax, &ay, &az, &gx, &gy, &gz);
                xSemaphoreGive(i2c_mutex);

                d.ax = ax / 16384.0f * 9.81f;
                d.ay = ay / 16384.0f * 9.81f;
                d.az = az / 16384.0f * 9.81f;
                d.gx = gx / 131.0f;
                d.gy = gy / 131.0f;
                d.gz = gz / 131.0f;
                d.pitch = atan2f(d.ax, sqrtf(d.ay * d.ay + d.az * d.az)) * 180.0f / PI;
                d.roll  = atan2f(d.ay, sqrtf(d.ax * d.ax + d.az * d.az)) * 180.0f / PI;
            }
        }

        // ── Publish to shared buffer (atomic copy via spinlock) ──
        taskENTER_CRITICAL(&s_spin);
        s_shared = d;
        taskEXIT_CRITICAL(&s_spin);

        // ── Invoke streaming callback (serial CSV output etc.) ──
        // Runs on Core 0 at a guaranteed 30 Hz, fully decoupled from
        // LVGL rendering on Core 1.
        SensorStreamCallback cb = s_stream_cb;
        if (cb) cb(d);

        // Precise 30 Hz pacing — sleeps exactly the remaining time in the
        // 33 ms period.  If the cycle overruns, the next iteration starts
        // immediately (no drift accumulation).
        vTaskDelayUntil(&xLastWake, xPeriod);
    }
}

// ── Public API ───────────────────────────────
void sensors_init() {
    mux_init();

    // Create I2C mutex FIRST — touch callback checks for non-NULL before locking
    i2c_mutex = xSemaphoreCreateMutex();

    // NOTE: Wire.setClock(I2C_FAST_MODE_HZ) is called in setup() (main.cpp)
    // *after* power_init(), which is the last driver to call Wire.begin().
    // Setting it here would be overridden by sy6970->begin() → Wire.begin().

    // ── I2C bus scan (mutex-protected) ──
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    i2c_scan();
    xSemaphoreGive(i2c_mutex);

    // ── ADS1115 16-bit ADC ──
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    ads_ok = ads.begin(ADS1115_ADDR, &Wire);
    if (ads_ok) {
        ads.setGain(GAIN_ONE);
        ads.setDataRate(RATE_ADS1115_860SPS);
        Serial.println("[SENSORS] ADS1115 OK (16-bit, 860 SPS, GAIN_ONE)");
    } else {
        Serial.println("[SENSORS] ADS1115 not found — falling back to internal ADC");
        analogReadResolution(12);
    }
    xSemaphoreGive(i2c_mutex);

    // ── MPU6050 IMU ──
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    mpu_ok = mpu_init();
    xSemaphoreGive(i2c_mutex);
    Serial.printf("[SENSORS] MPU6050 %s\n", mpu_ok ? "OK" : "not found");

    // ── Start background reading task on Core 0 ──
    xTaskCreatePinnedToCore(sensor_task_fn, "sensors", 4096,
                            NULL, 2, &s_task, 0);
    Serial.println("[SENSORS] Background task started (Core 0)");
}

bool sensors_mpu_available() { return mpu_ok; }
bool sensors_ads_available() { return ads_ok; }

void sensors_set_active(bool active) {
    s_active = active;
}

void sensors_set_stream_cb(SensorStreamCallback cb) {
    s_stream_cb = cb;
}

void sensors_set_rate_hz(int hz) {
    if (hz < 1)   hz = 1;
    if (hz > 100) hz = 100;
    s_period_ms = 1000 / hz;
    Serial.printf("[SENSORS] Task rate → %d Hz (%lu ms)\n", hz, (unsigned long)s_period_ms);
}

void sensors_read(SensorData &d) {
    // Non-blocking: just copy the latest data published by the background task
    taskENTER_CRITICAL(&s_spin);
    d = s_shared;
    taskEXIT_CRITICAL(&s_spin);
}

uint16_t sensors_mux_read(uint8_t ch) {
    // Public single-channel read for calibration etc.
    // Takes the mutex internally — caller must NOT already hold it.
    uint16_t val = 0;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50))) {
        mux_select(ch);
        val = ads_or_adc_read();
        xSemaphoreGive(i2c_mutex);
    }
    return val;
}

// ── Shutdown — called before deep sleep ──────
void sensors_shutdown() {
    // 1. Stop background task
    s_active = false;
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(20));  // let task reach idle
        vTaskDelete(s_task);
        s_task = NULL;
    }

    // 2. Put MPU6050 into hardware sleep mode
    if (mpu_ok && i2c_mutex) {
        xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100));
        mpu_write(MPU6050_PWR_MGMT_1, 0x40);
        xSemaphoreGive(i2c_mutex);
        Serial.println("[SENSORS] MPU6050 → sleep mode");
        mpu_ok = false;
    }

    // 3. Lock mux GPIOs LOW for deep sleep
    digitalWrite(MUX_S0, LOW);
    digitalWrite(MUX_S1, LOW);
    digitalWrite(MUX_S2, LOW);
    digitalWrite(MUX_S3, LOW);
    gpio_hold_en((gpio_num_t)MUX_S0);
    gpio_hold_en((gpio_num_t)MUX_S1);
    gpio_hold_en((gpio_num_t)MUX_S2);
    gpio_hold_en((gpio_num_t)MUX_S3);

    if (!ads_ok) {
        pinMode(MUX_SIG, INPUT_PULLDOWN);
    }

    Serial.println("[SENSORS] Mux GPIOs locked LOW for deep sleep");
}
