/*
 * @file Test_Sensors.cpp
 * @brief Test flex sensors, hall-effect sensors, and MPU6050 IMU via CD74HC4067 multiplexer
 *        Continuously reads and prints all sensor values over Serial.
 *
 * Pin config (from config.h):
 *   MUX_S0  = 21    MUX_S1  = 47    MUX_S2  = 48    MUX_S3  = 45
 *   MUX_SIG = 1     (ADC1_CH0)
 *
 * Mux channels:
 *   CH 0 = Flex Thumb      CH 8 = Hall Thumb
 *   CH 1 = Flex Index      CH 9 = Hall Index
 *   CH 2 = Flex Middle     CH 10 = Hall Middle
 *   CH 6 = Flex Ring       CH 11 = Hall Ring
 *   CH 7 = Flex Pinky      CH 12 = Hall Pinky
 */
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ── MPU6050 Registers ────────────────────────
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C

static bool mpu_ok = false;

// ── Finger labels ────────────────────────────
static const char* finger_names[] = {
    "Thumb", "Index", "Middle", "Ring", "Pinky"
};

// ── Multiplexer channel mapping (non-sequential) ──
static const uint8_t flex_channels[] = {
    MUX_CH_FLEX_THUMB,   // 0
    MUX_CH_FLEX_INDEX,   // 1
    MUX_CH_FLEX_MIDDLE,  // 2
    MUX_CH_FLEX_RING,    // 6
    MUX_CH_FLEX_PINKY    // 7
};

static const uint8_t hall_channels[] = {
    MUX_CH_HALL_THUMB,   // 8
    MUX_CH_HALL_INDEX,   // 9
    MUX_CH_HALL_MIDDLE,  // 10
    MUX_CH_HALL_RING,    // 11
    MUX_CH_HALL_PINKY    // 12
};

// ── Flex sensor calibration ──────────────────
#define CALIBRATION_TIME_MS  5000

struct FlexCalibration {
    uint16_t flat_value;      // Measured flat hand position (set at runtime)
    uint16_t upward_range;    // ADC counts from flat → fully flexed up
    uint16_t downward_range;  // ADC counts from flat → fully flexed down
    bool     calibrated;
};

//                         flat  up   down  (Thumb & Pinky ranges TBD)
static FlexCalibration flex_cal[NUM_FLEX_SENSORS] = {
    {0, 100, 70, false},
    {0, 220, 110, false},   
    {0, 220, 110, false},   
    {0, 220, 110, false},  
    {0, 100, 70, false}
};

static bool calibration_complete = false;

// ── Hall sensor calibration (fixed from measurement) ─────────────
struct HallCalibration {
    uint16_t normal;    // No magnet present
    uint16_t front;     // Magnet on front face  → ADC goes HIGH
    uint16_t back;      // Magnet on back face   → ADC goes LOW
};

//                          normal  front   back
static const HallCalibration hall_cal[NUM_HALL_SENSORS] = {
    {1915, 3060, 1690},   // Thumb
    {1875, 3060, 1542},   // Index
    {1910, 3070, 1475},   // Middle
    {1970, 3065, 1575},   // Ring
    {1910, 3070, 1605},   // Pinky
};

// ── Local mux helpers (standalone, no dependency on sensors.cpp) ──

// ADC noise reduction (per Espressif ADC noise minimization guide)
// Take ADC_OVERSAMPLE readings, sort them, trim ADC_TRIM outliers on each end,
// then average the remaining centre samples.
#define ADC_OVERSAMPLE  16   // samples per reading
#define ADC_TRIM         2   // outliers to drop from each end

static void mux_init() {
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
    analogReadResolution(12);    // 0–4095
    analogSetAttenuation(ADC_11db); // full 0–3.3 V range on MUX_SIG
}

static void mux_select(uint8_t ch) {
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    digitalWrite(MUX_S3, (ch >> 3) & 1);
    delayMicroseconds(MUX_SETTLE_US);   // let mux output settle
}

// Insertion sort (tiny array, no heap needed)
static void isort(uint16_t *arr, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        uint16_t key = arr[i];
        int8_t j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static uint16_t mux_read(uint8_t ch) {
    mux_select(ch);

    uint16_t samples[ADC_OVERSAMPLE];
    for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) {
        samples[i] = analogRead(MUX_SIG);
        delayMicroseconds(20);   // short inter-sample settle
    }

    isort(samples, ADC_OVERSAMPLE);  // sort ascending

    // Average the centre samples (drop ADC_TRIM from each end)
    uint32_t sum = 0;
    const uint8_t valid = ADC_OVERSAMPLE - 2 * ADC_TRIM;  // 12 of 16
    for (uint8_t i = ADC_TRIM; i < ADC_OVERSAMPLE - ADC_TRIM; i++) {
        sum += samples[i];
    }
    return (uint16_t)(sum / valid);
}

// ── MPU6050 helpers ──────────────────────────
static void mpu_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static bool mpu_init() {
    Wire.begin(IIC_SDA, IIC_SCL);  // Initialize I2C
    Wire.setClock(400000);          // 400kHz

    // Check WHO_AM_I
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_WHO_AM_I);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t id = Wire.read();
        if (id != 0x68 && id != 0x72) {
            Serial.printf("[MPU] WHO_AM_I = 0x%02X (unexpected, should be 0x68 or 0x72)\n", id);
            return false;
        }
        Serial.printf("[MPU] WHO_AM_I = 0x%02X (OK)\n", id);
    } else {
        Serial.println("[MPU] No response from MPU6050");
        return false;
    }

    mpu_write(MPU6050_PWR_MGMT_1, 0x00);  // Wake up
    mpu_write(MPU6050_CONFIG, 0x03);       // DLPF ~44Hz
    mpu_write(MPU6050_GYRO_CONFIG, 0x00);  // ±250°/s
    mpu_write(MPU6050_ACCEL_CONFIG, 0x00); // ±2g
    delay(100);
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

static void mpu_read_converted(float *ax, float *ay, float *az,
                                float *gx, float *gy, float *gz,
                                float *pitch, float *roll) {
    int16_t ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    mpu_read_raw(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw, &gz_raw);

    // Convert to real units
    *ax = ax_raw / 16384.0f * 9.81f;  // m/s²
    *ay = ay_raw / 16384.0f * 9.81f;
    *az = az_raw / 16384.0f * 9.81f;
    *gx = gx_raw / 131.0f;            // °/s
    *gy = gy_raw / 131.0f;
    *gz = gz_raw / 131.0f;

    // Calculate pitch and roll from accelerometer
    *pitch = atan2f(*ax, sqrtf((*ay) * (*ay) + (*az) * (*az))) * 180.0f / PI;
    *roll  = atan2f(*ay, sqrtf((*ax) * (*ax) + (*az) * (*az))) * 180.0f / PI;
}

// ── Flex sensor calibration ──────────────────
static void calibrate_flex_sensors() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   FLEX SENSOR CALIBRATION MODE         ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println("Please keep your hand FLAT for 5 seconds...");
    Serial.println("Calibrating");
    
    uint32_t accumulator[NUM_FLEX_SENSORS] = {0};
    uint32_t start_time = millis();
    uint16_t sample_count = 0;
    
    // Collect samples for 5 seconds
    while (millis() - start_time < CALIBRATION_TIME_MS) {
        // Read all flex sensors
        for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
            uint16_t value = mux_read(flex_channels[i]);
            accumulator[i] += value;
        }
        sample_count++;
        
        // Print progress dots
        if (sample_count % 20 == 0) {
            Serial.print(".");
        }
        
        delay(50);  // Sample every 50ms
    }
    
    Serial.println("\n");
    Serial.println("Calibration complete!");
    Serial.println("─────────────────────────────────────────");
    
    // Calculate mean values and store as flat position
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        flex_cal[i].flat_value = accumulator[i] / sample_count;
        flex_cal[i].calibrated = true;
        
        Serial.printf("  %s: Flat = %4d", finger_names[i], flex_cal[i].flat_value);
        
        if (flex_cal[i].upward_range > 0 || flex_cal[i].downward_range > 0) {
            Serial.printf("  (Up: +%d, Down: -%d)", 
                         flex_cal[i].upward_range, 
                         flex_cal[i].downward_range);
        } else {
            Serial.print("  (Range not configured)");
        }
        Serial.println();
    }
    
    Serial.println("─────────────────────────────────────────\n");
    calibration_complete = true;
}

static int8_t get_flex_percentage(int finger_index, uint16_t raw_value) {
    if (!flex_cal[finger_index].calibrated) return 0;

    int16_t diff = raw_value - flex_cal[finger_index].flat_value;

    if (diff > 0) {
        if (flex_cal[finger_index].upward_range == 0) return 0;
        return (int8_t)constrain((diff * 100) / flex_cal[finger_index].upward_range, 0, 100);
    } else {
        if (flex_cal[finger_index].downward_range == 0) return 0;
        return (int8_t)constrain((abs(diff) * 100) / flex_cal[finger_index].downward_range * -1, -100, 0);
    }
}

static int8_t get_hall_percentage(int idx, uint16_t raw) {
    int16_t diff  = (int16_t)raw - (int16_t)hall_cal[idx].normal;
    if (diff > 0) {
        uint16_t range = hall_cal[idx].front - hall_cal[idx].normal;
        if (range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)range, 0, 100);
    } else if (diff < 0) {
        uint16_t range = hall_cal[idx].normal - hall_cal[idx].back;
        if (range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)range, -100, 0);
    }
    return 0;
}

// ── Bar graph helpers ────────────────────────
// Standard left-fill bar (flex / raw ADC)
static void print_bar(uint16_t value, uint16_t max_val = 4095, uint8_t width = 20) {
    uint8_t filled = (uint8_t)((uint32_t)value * width / max_val);
    Serial.print('[');
    for (uint8_t i = 0; i < width; i++)
        Serial.print(i < filled ? '#' : ' ');
    Serial.print(']');
}

// Centred bar (hall effect): Back ◄──|──► Front
static void print_centered_bar(int8_t percent, uint8_t half_width = 10) {
    uint8_t left_fill  = (percent < 0) ? (uint8_t)(abs(percent) * half_width / 100) : 0;
    uint8_t right_fill = (percent > 0) ? (uint8_t)(percent      * half_width / 100) : 0;
    Serial.print('[');
    for (int8_t i = half_width - 1; i >= 0; i--)
        Serial.print((uint8_t)i < left_fill  ? '#' : ' ');
    Serial.print('|');
    for (uint8_t i = 0; i < half_width; i++)
        Serial.print(i < right_fill ? '#' : ' ');
    Serial.print(']');
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("========================================");
    Serial.println("  Test_Sensors - Comprehensive Sensor Test");
    Serial.println("========================================");
    Serial.println();
    Serial.println("Multiplexer pin configuration:");
    Serial.printf("  MUX_S0  = %d\n", MUX_S0);
    Serial.printf("  MUX_S1  = %d\n", MUX_S1);
    Serial.printf("  MUX_S2  = %d\n", MUX_S2);
    Serial.printf("  MUX_S3  = %d\n", MUX_S3);
    Serial.printf("  MUX_SIG = %d  (ADC input)\n", MUX_SIG);
    Serial.println();
    Serial.println("Channel mapping:");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("  CH %2d = Flex %-6s    CH %2d = Hall %s\n",
                      flex_channels[i], finger_names[i],
                      hall_channels[i], finger_names[i]);
    }
    Serial.println();

    mux_init();
    Serial.println("[TEST] Multiplexer initialised.");
    
    mpu_ok = mpu_init();
    if (mpu_ok) {
        Serial.println("[TEST] MPU6050 initialised successfully!");
    } else {
        Serial.println("[TEST] MPU6050 NOT detected (will continue without IMU)");
    }
    
    Serial.println();
    
    // Calibrate flex sensors
    calibrate_flex_sensors();
    
    Serial.println("[TEST] Starting sensor readings...\n");
}

void loop() {
    uint16_t flex[NUM_FLEX_SENSORS];
    uint16_t hall[NUM_HALL_SENSORS];
    float accel_x = 0, accel_y = 0, accel_z = 0;
    float gyro_x = 0, gyro_y = 0, gyro_z = 0;
    float pitch = 0, roll = 0;

    // ── Read all sensors ──
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        flex[i] = mux_read(flex_channels[i]);
    }
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        hall[i] = mux_read(hall_channels[i]);
    }
    
    // Read MPU6050 if available
    if (mpu_ok) {
        mpu_read_converted(&accel_x, &accel_y, &accel_z,
                          &gyro_x, &gyro_y, &gyro_z,
                          &pitch, &roll);
    }

    // ── Print flex sensors ──
    Serial.println("─── Flex Sensors ───────────────────────");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        int8_t flex_percent = get_flex_percentage(i, flex[i]);
        Serial.printf("  %-6s (CH%2d): %4d  ", finger_names[i], flex_channels[i], flex[i]);
        print_bar(flex[i]);
        
        if (calibration_complete) {
            if (flex_percent > 0) {
                Serial.printf("  ▲ %3d%% Up", flex_percent);
            } else if (flex_percent < 0) {
                Serial.printf("  ▼ %3d%% Down", abs(flex_percent));
            } else {
                Serial.print("  ● Flat");
            }
        }
        Serial.println();
    }

    // ── Print hall sensors ──
    Serial.println("─── Hall Sensors  [Back ◄──|──► Front] ─");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int8_t hall_pct = get_hall_percentage(i, hall[i]);
        Serial.printf("  %-6s (CH%2d): %4d  ", finger_names[i], hall_channels[i], hall[i]);
        print_centered_bar(hall_pct);
        if (hall_pct > 5) {
            Serial.printf("  ▲ %3d%% Front", (int)hall_pct);
        } else if (hall_pct < -5) {
            Serial.printf("  ▼ %3d%% Back", (int)abs(hall_pct));
        } else {
            Serial.print("  ● Normal");
        }
        Serial.println();
    }

    // ── Print MPU6050 IMU ──
    if (mpu_ok) {
        Serial.println("─── MPU6050 IMU ────────────────────────");
        Serial.printf("  Accel:  X=%7.3f  Y=%7.3f  Z=%7.3f  m/s²\n", accel_x, accel_y, accel_z);
        Serial.printf("  Gyro:   X=%7.2f  Y=%7.2f  Z=%7.2f  °/s\n", gyro_x, gyro_y, gyro_z);
        Serial.printf("  Angles: Pitch=%6.1f°  Roll=%6.1f°\n", pitch, roll);
    } else {
        Serial.println("─── MPU6050 IMU ────────────────────────");
        Serial.println("  [NOT DETECTED]");
    }

    // ── CSV-style compact line (for Serial Plotter) ──
    Serial.print("[CSV] ");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("F%d:%d", i, flex[i]);
        Serial.print(',');
    }
    if (calibration_complete) {
        for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
            Serial.printf("FP%d:%d,", i, (int)get_flex_percentage(i, flex[i]));
        }
    }
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("H%d:%d,", i, hall[i]);
    }
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("HP%d:%d", i, (int)get_hall_percentage(i, hall[i]));
        if (i < NUM_HALL_SENSORS - 1) Serial.print(',');
    }
    if (mpu_ok) {
        Serial.printf(",AX:%.2f,AY:%.2f,AZ:%.2f", accel_x, accel_y, accel_z);
        Serial.printf(",GX:%.2f,GY:%.2f,GZ:%.2f", gyro_x, gyro_y, gyro_z);
        Serial.printf(",Pitch:%.1f,Roll:%.1f", pitch, roll);
    }
    Serial.println();

    Serial.println();
    delay(250);
}
