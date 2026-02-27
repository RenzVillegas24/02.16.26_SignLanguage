/*
 * @file sensors.cpp
 * @brief CD74HC4067 multiplexer + MPU6050 IMU sensor reading
 */
#include "sensors.h"
#include <Wire.h>

// ── Lightweight MPU6050 driver (no heavy library) ──
// Register addresses
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C

static bool  mpu_ok = false;

// ── Mux helpers ──────────────────────────────
static void mux_init() {
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
    analogReadResolution(12);   // 0-4095
}

static void mux_select(uint8_t ch) {
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    digitalWrite(MUX_S3, (ch >> 3) & 1);
    delayMicroseconds(MUX_SETTLE_US);
}

static uint16_t mux_read(uint8_t ch) {
    mux_select(ch);
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

// ── Public API ───────────────────────────────
void sensors_init() {
    mux_init();

    // I2C already started by display/touch - just init MPU
    mpu_ok = mpu_init();
    if (mpu_ok) {
        Serial.println("[SENSORS] MPU6050 OK");
    } else {
        Serial.println("[SENSORS] MPU6050 not found (will work without IMU)");
    }
    Serial.println("[SENSORS] Multiplexer ready");
}

bool sensors_mpu_available() { return mpu_ok; }

void sensors_read(SensorData &d) {
    // Read flex sensors — non-sequential channels (0,1,2,6,7).
    // Ring and Pinky are on ch6/ch7; ch3-ch5 are Hall Top sensors.
    // Using the same lookup table as sensor_module to stay in sync.
    static const uint8_t flex_ch[NUM_FLEX_SENSORS] = {
        MUX_CH_FLEX_THUMB,  MUX_CH_FLEX_INDEX,  MUX_CH_FLEX_MIDDLE,
        MUX_CH_FLEX_RING,   MUX_CH_FLEX_PINKY
    };
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        d.flex[i] = mux_read(flex_ch[i]);
    }
    // Read hall sensors — side (C8-C12)
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        d.hall[i] = mux_read(MUX_CH_HALL_THUMB + i);
    }
    // Read hall sensors — top (C3,C4,C5,C13,C14)
    {
        static const uint8_t ht_ch[NUM_HALL_TOP_SENSORS] = {
            MUX_CH_HALL_TOP_THUMB,  MUX_CH_HALL_TOP_INDEX,
            MUX_CH_HALL_TOP_MIDDLE, MUX_CH_HALL_TOP_RING,
            MUX_CH_HALL_TOP_PINKY
        };
        for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
            d.hall_top[i] = mux_read(ht_ch[i]);
        }
    }

    // Read MPU6050
    if (mpu_ok) {
        int16_t ax, ay, az, gx, gy, gz;
        mpu_read_raw(&ax, &ay, &az, &gx, &gy, &gz);

        // Convert to real units
        d.accel_x = ax / 16384.0f * 9.81f;
        d.accel_y = ay / 16384.0f * 9.81f;
        d.accel_z = az / 16384.0f * 9.81f;
        d.gyro_x  = gx / 131.0f;
        d.gyro_y  = gy / 131.0f;
        d.gyro_z  = gz / 131.0f;

        // Simple pitch / roll from accel
        d.pitch = atan2f(d.accel_x, sqrtf(d.accel_y * d.accel_y + d.accel_z * d.accel_z)) * 180.0f / PI;
        d.roll  = atan2f(d.accel_y, sqrtf(d.accel_x * d.accel_x + d.accel_z * d.accel_z)) * 180.0f / PI;
    } else {
        d.accel_x = d.accel_y = d.accel_z = 0;
        d.gyro_x  = d.gyro_y  = d.gyro_z  = 0;
        d.pitch   = d.roll = 0;
    }
}
