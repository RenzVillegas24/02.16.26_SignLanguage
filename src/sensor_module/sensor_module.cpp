/*
 * @file sensor_module/sensor_module.cpp
 * @brief Sensor data processing — calibration, EMA smoothing, percentage
 *        mapping, serial / OLED output, and Edge Impulse prediction stub.
 */
#include "sensor_module.h"
#include <math.h>
#include <Preferences.h>

// ─────────────────────────────────────────────
//  Tunables
// ─────────────────────────────────────────────
#define CALIBRATION_TIME_MS    5000
#define BASELINE_ADAPT_ALPHA   0.002f  // Slow drift compensation inside deadzone

// Max calibration samples stored for stddev computation
#define MAX_CAL_SAMPLES        128

// Deadzone limits: stddev * DZ_SIGMA_MULT, clamped to [DZ_MIN, DZ_MAX]
#define DZ_SIGMA_MULT          2.5f
#define DZ_MIN                 4
#define DZ_MAX                 18

// ── Adaptive smoothing (One-Euro filter inspired) ──
//  Raw ADC → [Median(3)] → [Adaptive EMA] → calibrated %
//
//  The adaptive EMA adjusts α based on signal speed:
//    • Signal barely moving → low α (heavy smoothing, kills jitter)
//    • Signal moving fast   → high α (light smoothing, near-zero lag)
//  This gives rock-solid readings at rest AND instant response to gestures.
//
//  Latency budget at 20 Hz:
//    Median(3) ≈ 1 frame = 50 ms
//    Adaptive EMA during fast move (α≈0.7) ≈ 0.4 frames = 22 ms
//    Total ≈ 72 ms  (was ~640 ms with the old 3-stage pipeline)

#define MEDIAN_WINDOW       3       // minimal — just 1 frame of latency

// Adaptive-alpha EMA parameters
// α = clamp(ALPHA_MIN + speed * BETA, ALPHA_MIN, ALPHA_MAX)
#define FLEX_ALPHA_MIN      0.12f   // heavy smoothing at rest
#define FLEX_ALPHA_MAX      0.75f   // near pass-through during fast movement
#define FLEX_BETA           0.008f  // speed-to-alpha sensitivity

#define HALL_ALPHA_MIN      0.15f
#define HALL_ALPHA_MAX      0.80f
#define HALL_BETA           0.005f

// ─────────────────────────────────────────────
//  Mux channel tables (mirrors config.h defines)
// ─────────────────────────────────────────────
static const uint8_t flex_channels[NUM_FLEX_SENSORS] = {
    MUX_CH_FLEX_THUMB,  MUX_CH_FLEX_INDEX, MUX_CH_FLEX_MIDDLE,
    MUX_CH_FLEX_RING,   MUX_CH_FLEX_PINKY
};

static const uint8_t hall_channels[NUM_HALL_SENSORS] = {
    MUX_CH_HALL_THUMB,  MUX_CH_HALL_INDEX, MUX_CH_HALL_MIDDLE,
    MUX_CH_HALL_RING,   MUX_CH_HALL_PINKY
};

static const uint8_t hall_top_channels[NUM_HALL_TOP_SENSORS] = {
    MUX_CH_HALL_TOP_THUMB,  MUX_CH_HALL_TOP_INDEX, MUX_CH_HALL_TOP_MIDDLE,
    MUX_CH_HALL_TOP_RING,   MUX_CH_HALL_TOP_PINKY
};

static const char *finger_names[5] = {
    "Thumb", "Index", "Middle", "Ring", "Pinky"
};

// ─────────────────────────────────────────────
//  Flex calibration
// ─────────────────────────────────────────────
struct FlexCalibration {
    uint16_t flat_value;
    uint16_t upward_range;
    uint16_t downward_range;
    uint16_t noise_deadzone;
    bool     calibrated;
};

//                             flat  up   down  dz
static FlexCalibration flex_cal[NUM_FLEX_SENSORS] = {
    {0, 150, 110, 0, false},
    {0, 150, 110, 0, false},
    {0, 150, 110, 0, false},
    {0, 150, 110, 0, false},
    {0, 150, 110, 0, false}
};

// ─────────────────────────────────────────────
//  Hall (side) calibration
// ─────────────────────────────────────────────
struct HallCalibration {
    uint16_t normal;
    uint16_t front_range;
    uint16_t back_range;
    bool     calibrated;
};

//                              norm  front_r  back_r
static HallCalibration hall_cal[NUM_HALL_SENSORS] = {
    {0, 1145,  225, false},   // Thumb
    {0, 1185,  333, false},   // Index
    {0, 1160,  435, false},   // Middle
    {0, 1095,  395, false},   // Ring
    {0, 1160,  305, false},   // Pinky
};

// ─────────────────────────────────────────────
//  Hall TOP calibration (yet to be characterised)
//  Placeholder: range ≈ 1150 front, 400 back
// ─────────────────────────────────────────────
static HallCalibration hall_top_cal[NUM_HALL_TOP_SENSORS] = {
    {0, 1150, 400, false},   // Thumb  (CH 3)
    {0, 1150, 400, false},   // Index  (CH 4)
    {0, 1150, 400, false},   // Middle (CH 5)
    {0, 1150, 400, false},   // Ring   (CH 13)
    {0, 1150, 400, false},   // Pinky  (CH 14)
};

// ─────────────────────────────────────────────
//  Smoothing state
// ─────────────────────────────────────────────

// ── Median filter ring buffers (window=3, adds only 1 frame lag) ──
struct MedianFilter {
    uint16_t buf[MEDIAN_WINDOW];
    uint8_t  idx;
    uint8_t  count;
};

static MedianFilter flex_mf[NUM_FLEX_SENSORS];
static MedianFilter hall_mf[NUM_HALL_SENSORS];
static MedianFilter hall_top_mf[NUM_HALL_TOP_SENSORS];

// ── Adaptive EMA state (single float per channel) ──
static float    flex_ema[NUM_FLEX_SENSORS];
static float    hall_ema[NUM_HALL_SENSORS];
static float    hall_top_ema[NUM_HALL_TOP_SENSORS];
static bool     flex_ema_init     = false;
static bool     hall_ema_init     = false;
static bool     hall_top_ema_init = false;

static bool     s_calibrated = false;

// ─────────────────────────────────────────────
//  Local MUX read (oversampled, trimmed mean)
// ─────────────────────────────────────────────
#define ADC_OVERSAMPLE  16
#define ADC_TRIM         2

static void isort(uint16_t *arr, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        uint16_t key = arr[i];
        int8_t j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
}

static uint16_t mux_read_oversampled(uint8_t ch) {
    // Select channel (mux hardware already initialised by sensors_init)
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    digitalWrite(MUX_S3, (ch >> 3) & 1);
    delayMicroseconds(MUX_SETTLE_US);

    uint16_t samples[ADC_OVERSAMPLE];
    for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) {
        samples[i] = analogRead(MUX_SIG);
        delayMicroseconds(20);
    }
    isort(samples, ADC_OVERSAMPLE);

    uint32_t sum = 0;
    const uint8_t valid = ADC_OVERSAMPLE - 2 * ADC_TRIM;
    for (uint8_t i = ADC_TRIM; i < ADC_OVERSAMPLE - ADC_TRIM; i++)
        sum += samples[i];
    return (uint16_t)(sum / valid);
}

// ─────────────────────────────────────────────
//  Smoothing helpers
// ─────────────────────────────────────────────

/// Push a value into a median(3) ring buffer, return the median.
static uint16_t median_push(MedianFilter &mf, uint16_t val) {
    mf.buf[mf.idx] = val;
    mf.idx = (mf.idx + 1) % MEDIAN_WINDOW;
    if (mf.count < MEDIAN_WINDOW) mf.count++;

    uint16_t tmp[MEDIAN_WINDOW];
    uint8_t n = mf.count;
    for (uint8_t i = 0; i < n; i++) tmp[i] = mf.buf[i];
    isort(tmp, n);
    return tmp[n / 2];
}

/// Adaptive-alpha EMA (One-Euro filter inspired).
/// α scales linearly with |change speed|: still→heavy smooth, fast→pass-through.
static float adaptive_ema(float &state, float val,
                          float alpha_min, float alpha_max, float beta,
                          bool &init) {
    if (!init) {
        state = val;
        return val;
    }
    float speed = fabsf(val - state);
    float alpha = alpha_min + speed * beta;
    if (alpha > alpha_max) alpha = alpha_max;
    state += alpha * (val - state);
    return state;
}

// ─────────────────────────────────────────────
//  Percentage helpers
// ─────────────────────────────────────────────

/// Flex: median(3) → adaptive EMA → deadzone → linear map
static int8_t calc_flex_pct(int idx, uint16_t raw) {
    if (!flex_cal[idx].calibrated) return 0;

    // Stage 1 — Tiny median filter (just 1 frame lag, catches spikes)
    uint16_t denoised = median_push(flex_mf[idx], raw);

    // Stage 2 — Adaptive EMA (fast when moving, smooth when still)
    float smoothed_f = adaptive_ema(flex_ema[idx], (float)denoised,
                                    FLEX_ALPHA_MIN, FLEX_ALPHA_MAX, FLEX_BETA,
                                    flex_ema_init);
    int16_t smoothed = (int16_t)(smoothed_f + 0.5f);

    int16_t diff = smoothed - (int16_t)flex_cal[idx].flat_value;
    int16_t dz   = (int16_t)flex_cal[idx].noise_deadzone;

    // Inside deadzone → flat; very slowly adapt baseline for thermal drift
    if (abs(diff) <= dz) {
        float nf = (float)flex_cal[idx].flat_value +
                   BASELINE_ADAPT_ALPHA *
                   (smoothed_f - (float)flex_cal[idx].flat_value);
        flex_cal[idx].flat_value = (uint16_t)(nf + 0.5f);
        return 0;
    }

    if (diff > 0) {
        int16_t range = (int16_t)flex_cal[idx].upward_range;
        if (range <= 0) range = 1;
        return (int8_t)constrain((diff * 100) / range, 0, 100);
    } else {
        int16_t range = (int16_t)flex_cal[idx].downward_range;
        if (range <= 0) range = 1;
        return (int8_t)constrain((diff * 100) / range, -100, 0);
    }
}

/// Hall (side): median(3) → adaptive EMA → linear map
static int8_t calc_hall_side_pct(int idx, uint16_t raw) {
    if (!hall_cal[idx].calibrated) return 0;

    uint16_t denoised = median_push(hall_mf[idx], raw);
    float smoothed_f  = adaptive_ema(hall_ema[idx], (float)denoised,
                                     HALL_ALPHA_MIN, HALL_ALPHA_MAX, HALL_BETA,
                                     hall_ema_init);
    int16_t diff = (int16_t)(smoothed_f + 0.5f) - (int16_t)hall_cal[idx].normal;

    if (diff > 0) {
        if (hall_cal[idx].front_range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)hall_cal[idx].front_range, 0, 100);
    } else if (diff < 0) {
        if (hall_cal[idx].back_range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)hall_cal[idx].back_range, -100, 0);
    }
    return 0;
}

/// Hall (top): median(3) → adaptive EMA → linear map
static int8_t calc_hall_top_pct(int idx, uint16_t raw) {
    if (!hall_top_cal[idx].calibrated) return 0;

    uint16_t denoised = median_push(hall_top_mf[idx], raw);
    float smoothed_f  = adaptive_ema(hall_top_ema[idx], (float)denoised,
                                     HALL_ALPHA_MIN, HALL_ALPHA_MAX, HALL_BETA,
                                     hall_top_ema_init);
    int16_t diff = (int16_t)(smoothed_f + 0.5f) - (int16_t)hall_top_cal[idx].normal;

    if (diff > 0) {
        if (hall_top_cal[idx].front_range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)hall_top_cal[idx].front_range, 0, 100);
    } else if (diff < 0) {
        if (hall_top_cal[idx].back_range == 0) return 0;
        return (int8_t)constrain((diff * 100) / (int16_t)hall_top_cal[idx].back_range, -100, 0);
    }
    return 0;
}

/// Helper: zero all smoothing state for a fresh start.
static void reset_smoothing_state() {
    memset(flex_mf,          0, sizeof(flex_mf));
    memset(hall_mf,          0, sizeof(hall_mf));
    memset(hall_top_mf,      0, sizeof(hall_top_mf));
    memset(flex_ema,         0, sizeof(flex_ema));
    memset(hall_ema,         0, sizeof(hall_ema));
    memset(hall_top_ema,     0, sizeof(hall_top_ema));
    flex_ema_init     = false;
    hall_ema_init     = false;
    hall_top_ema_init = false;
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
void sensor_module_init() {
    s_calibrated = false;
    reset_smoothing_state();

    for (int i = 0; i < NUM_FLEX_SENSORS; i++)
        flex_cal[i].calibrated = false;
    for (int i = 0; i < NUM_HALL_SENSORS; i++)
        hall_cal[i].calibrated = false;
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++)
        hall_top_cal[i].calibrated = false;

    // Try to restore calibration from NVS
    if (sensor_module_load_calibration()) {
        Serial.println("[SENSOR_MODULE] Calibration restored from NVS");
    } else {
        Serial.println("[SENSOR_MODULE] No saved calibration found");
    }

    Serial.println("[SENSOR_MODULE] Initialised");
}

void sensor_module_calibrate(SensorCalibProgressCb progress_cb) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║        SENSOR CALIBRATION MODE         ║");
    Serial.println("╠════════════════════════════════════════╣");
    Serial.println("║  Keep hand FLAT and NO magnets near    ║");
    Serial.println("║  the sensors for 5 seconds...          ║");
    Serial.println("╚════════════════════════════════════════╝");

    uint32_t hall_acc[NUM_HALL_SENSORS]          = {0};
    uint32_t hall_top_acc[NUM_HALL_TOP_SENSORS]  = {0};

    // Per-sample buffers for flex stddev (capped at MAX_CAL_SAMPLES)
    uint16_t flex_samples[NUM_FLEX_SENSORS][MAX_CAL_SAMPLES];
    memset(flex_samples, 0, sizeof(flex_samples));

    uint32_t t0 = millis();
    uint16_t n  = 0;

    while (millis() - t0 < CALIBRATION_TIME_MS) {
        for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
            uint16_t v = mux_read_oversampled(flex_channels[i]);
            if (n < MAX_CAL_SAMPLES) flex_samples[i][n] = v;
        }
        for (int i = 0; i < NUM_HALL_SENSORS; i++)
            hall_acc[i] += mux_read_oversampled(hall_channels[i]);
        for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++)
            hall_top_acc[i] += mux_read_oversampled(hall_top_channels[i]);

        n++;

        int pct = (int)(((float)(millis() - t0) / CALIBRATION_TIME_MS) * 100);
        if (progress_cb) progress_cb(pct);
        if (n % 20 == 0) Serial.print(".");
        delay(50);
    }
    uint16_t n_flex = (n < MAX_CAL_SAMPLES) ? n : MAX_CAL_SAMPLES;

    // Store calibration
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        // Mean from stored samples (more accurate than running accumulator for stddev)
        uint32_t sum = 0;
        for (uint16_t s = 0; s < n_flex; s++) sum += flex_samples[i][s];
        flex_cal[i].flat_value = (uint16_t)(sum / n_flex);

        // Stddev-based deadzone — far tighter and more accurate than min/max * 2
        uint32_t var_sum = 0;
        for (uint16_t s = 0; s < n_flex; s++) {
            int32_t d = (int32_t)flex_samples[i][s] - (int32_t)flex_cal[i].flat_value;
            var_sum += (uint32_t)(d * d);
        }
        float stddev = sqrtf((float)var_sum / n_flex);
        flex_cal[i].noise_deadzone = (uint16_t)constrain(
            (int)(stddev * DZ_SIGMA_MULT + 0.5f), DZ_MIN, DZ_MAX);
        flex_cal[i].calibrated = true;
        flex_ema[i] = (float)flex_cal[i].flat_value;
    }
    flex_ema_init = true;

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        hall_cal[i].normal     = hall_acc[i] / n;
        hall_cal[i].calibrated = true;
        hall_ema[i] = (float)hall_cal[i].normal;
    }
    hall_ema_init = true;

    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        hall_top_cal[i].normal     = hall_top_acc[i] / n;
        hall_top_cal[i].calibrated = true;
        hall_top_ema[i] = (float)hall_top_cal[i].normal;
    }
    hall_top_ema_init = true;
    s_calibrated = true;

    // Persist to NVS
    sensor_module_save_calibration();

    // Print summary
    Serial.println("\n\nCalibration complete!");
    Serial.println("─── Flex ────────────────────────────────");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("  %-6s  Flat = %4d  (Up: +%d, Down: -%d, DZ: ±%d)\n",
                      finger_names[i], flex_cal[i].flat_value,
                      flex_cal[i].upward_range, flex_cal[i].downward_range,
                      flex_cal[i].noise_deadzone);
    }
    Serial.println("─── Hall (side) ─────────────────────────");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("  %-6s  Normal = %4d  (Front: +%d, Back: -%d)\n",
                      finger_names[i], hall_cal[i].normal,
                      hall_cal[i].front_range, hall_cal[i].back_range);
    }
    Serial.println("─── Hall (top) ──────────────────────────");
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        Serial.printf("  %-6s  Normal = %4d  (Front: +%d, Back: -%d)\n",
                      finger_names[i], hall_top_cal[i].normal,
                      hall_top_cal[i].front_range, hall_top_cal[i].back_range);
    }
    Serial.println("─────────────────────────────────────────\n");
}

bool sensor_module_is_calibrated() { return s_calibrated; }

void sensor_module_process(const SensorData &raw, ProcessedSensorData &out) {
    // Flex — median → dual EMA → deadzone → % → slew
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        out.flex_raw[i] = raw.flex[i];
        out.flex_pct[i] = calc_flex_pct(i, raw.flex[i]);
    }
    if (!flex_ema_init) flex_ema_init = true;   // first frame seeds done

    // Hall side — median → dual EMA → % → slew
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        out.hall_raw[i] = raw.hall[i];
        out.hall_pct[i] = calc_hall_side_pct(i, raw.hall[i]);
    }
    if (!hall_ema_init) hall_ema_init = true;

    // Hall top — median → dual EMA → % → slew
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        out.hall_top_raw[i] = raw.hall_top[i];
        out.hall_top_pct[i] = calc_hall_top_pct(i, raw.hall_top[i]);
    }
    if (!hall_top_ema_init) hall_top_ema_init = true;

    // IMU pass-through
    out.accel_x = raw.accel_x;  out.accel_y = raw.accel_y;  out.accel_z = raw.accel_z;
    out.gyro_x  = raw.gyro_x;   out.gyro_y  = raw.gyro_y;   out.gyro_z  = raw.gyro_z;
    out.pitch   = raw.pitch;     out.roll    = raw.roll;
}

void sensor_module_print_serial(const ProcessedSensorData &pd) {
    // Build entire output in a single buffer, then one Serial.write().
    // This avoids ~50 individual Serial.print() calls which block the main
    // loop on ESP32-S3 USB CDC when a terminal is connected.
    static char buf[1200];
    int o = 0;
    const int N = sizeof(buf);

    o += snprintf(buf + o, N - o, "─── Flex Sensors ───────────────────────\n");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        o += snprintf(buf + o, N - o, "  %-6s: %4d  ", finger_names[i], pd.flex_raw[i]);
        if (pd.flex_pct[i] > 0)
            o += snprintf(buf + o, N - o, "▲ %3d%% Up\n", (int)pd.flex_pct[i]);
        else if (pd.flex_pct[i] < 0)
            o += snprintf(buf + o, N - o, "▼ %3d%% Down\n", (int)abs(pd.flex_pct[i]));
        else
            o += snprintf(buf + o, N - o, "● Flat\n");
    }
    o += snprintf(buf + o, N - o, "─── Hall (side) [Back ◄──|──► Front] ───\n");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        o += snprintf(buf + o, N - o, "  %-6s: %4d  ", finger_names[i], pd.hall_raw[i]);
        if (pd.hall_pct[i] > 5)
            o += snprintf(buf + o, N - o, "▲ %3d%% Front\n", (int)pd.hall_pct[i]);
        else if (pd.hall_pct[i] < -5)
            o += snprintf(buf + o, N - o, "▼ %3d%% Back\n", (int)abs(pd.hall_pct[i]));
        else
            o += snprintf(buf + o, N - o, "● Normal\n");
    }
    o += snprintf(buf + o, N - o, "─── Hall (top) [Back ◄──|──► Front] ────\n");
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        o += snprintf(buf + o, N - o, "  %-6s: %4d  ", finger_names[i], pd.hall_top_raw[i]);
        if (pd.hall_top_pct[i] > 5)
            o += snprintf(buf + o, N - o, "▲ %3d%% Front\n", (int)pd.hall_top_pct[i]);
        else if (pd.hall_top_pct[i] < -5)
            o += snprintf(buf + o, N - o, "▼ %3d%% Back\n", (int)abs(pd.hall_top_pct[i]));
        else
            o += snprintf(buf + o, N - o, "● Normal\n");
    }
    if (pd.accel_x != 0 || pd.accel_y != 0 || pd.accel_z != 0) {
        o += snprintf(buf + o, N - o, "─── MPU6050 IMU ────────────────────────\n");
        o += snprintf(buf + o, N - o, "  Accel:  X=%7.3f  Y=%7.3f  Z=%7.3f  m/s²\n",
                      pd.accel_x, pd.accel_y, pd.accel_z);
        o += snprintf(buf + o, N - o, "  Gyro:   X=%7.2f  Y=%7.2f  Z=%7.2f  °/s\n",
                      pd.gyro_x, pd.gyro_y, pd.gyro_z);
        o += snprintf(buf + o, N - o, "  Angles: Pitch=%6.1f°  Roll=%6.1f°\n",
                      pd.pitch, pd.roll);
    }

    // CSV line
    o += snprintf(buf + o, N - o, "[CSV] ");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++)
        o += snprintf(buf + o, N - o, "F%d:%d,FP%d:%d,", i, pd.flex_raw[i], i, (int)pd.flex_pct[i]);
    for (int i = 0; i < NUM_HALL_SENSORS; i++)
        o += snprintf(buf + o, N - o, "H%d:%d,HP%d:%d,", i, pd.hall_raw[i], i, (int)pd.hall_pct[i]);
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++)
        o += snprintf(buf + o, N - o, "HT%d:%d,HTP%d:%d,", i, pd.hall_top_raw[i], i, (int)pd.hall_top_pct[i]);
    o += snprintf(buf + o, N - o, "AX:%.2f,AY:%.2f,AZ:%.2f,GX:%.2f,GY:%.2f,GZ:%.2f,P:%.1f,R:%.1f\n\n",
                  pd.accel_x, pd.accel_y, pd.accel_z,
                  pd.gyro_x, pd.gyro_y, pd.gyro_z,
                  pd.pitch, pd.roll);

    // One write — avoids per-call USB CDC overhead
    if (o > 0) Serial.write(buf, (o < N) ? o : N - 1);
}

void sensor_module_format_oled(const ProcessedSensorData &pd,
                               char *buf, size_t buf_len) {
    int off = 0;
    off += snprintf(buf + off, buf_len - off, "Flex: ");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++)
        off += snprintf(buf + off, buf_len - off, "%+4d ", (int)pd.flex_pct[i]);
    off += snprintf(buf + off, buf_len - off, "\nHall: ");
    for (int i = 0; i < NUM_HALL_SENSORS; i++)
        off += snprintf(buf + off, buf_len - off, "%+4d ", (int)pd.hall_pct[i]);
    off += snprintf(buf + off, buf_len - off, "\nHTop: ");
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++)
        off += snprintf(buf + off, buf_len - off, "%+4d ", (int)pd.hall_top_pct[i]);
    off += snprintf(buf + off, buf_len - off,
                    "\nIMU P:%.0f R:%.0f",
                    pd.pitch, pd.roll);
}

const char *sensor_module_predict(ProcessedSensorData &pd) {
    // ── Edge Impulse stub ──────────────────────────
    // TODO: Replace with actual EI classifier inference.
    //       1. Build feature array from pd (flex_pct, hall_pct, hall_top_pct, imu)
    //       2. Call ei_run_classifier(...)
    //       3. Copy top label into pd.predicted_label
    //
    // For now, simple rule-based placeholder:
    bool all_bent = true, all_open = true;
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        if (pd.flex_pct[i] < 50) all_bent = false;
        if (pd.flex_pct[i] > 10) all_open = false;
    }

    if (all_bent) {
        strncpy(pd.predicted_label, "FIST", sizeof(pd.predicted_label));
        pd.prediction_confidence = 0.8f;
    } else if (all_open) {
        strncpy(pd.predicted_label, "OPEN HAND", sizeof(pd.predicted_label));
        pd.prediction_confidence = 0.8f;
    } else {
        strncpy(pd.predicted_label, "---", sizeof(pd.predicted_label));
        pd.prediction_confidence = 0.0f;
    }

    return pd.predicted_label;
}

// ─────────────────────────────────────────────
//  NVS persistence
// ─────────────────────────────────────────────
#define CAL_NVS_NS   "s_cal"
#define CAL_NVS_MAGIC 0xCA1B

void sensor_module_save_calibration() {
    Preferences prefs;
    prefs.begin(CAL_NVS_NS, false);
    prefs.putUShort("magic", CAL_NVS_MAGIC);

    // Flex
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "ff%d", i);  prefs.putUShort(k, flex_cal[i].flat_value);
        snprintf(k, sizeof(k), "fu%d", i);  prefs.putUShort(k, flex_cal[i].upward_range);
        snprintf(k, sizeof(k), "fd%d", i);  prefs.putUShort(k, flex_cal[i].downward_range);
        snprintf(k, sizeof(k), "fz%d", i);  prefs.putUShort(k, flex_cal[i].noise_deadzone);
    }
    // Hall side
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "hn%d", i);  prefs.putUShort(k, hall_cal[i].normal);
        snprintf(k, sizeof(k), "hf%d", i);  prefs.putUShort(k, hall_cal[i].front_range);
        snprintf(k, sizeof(k), "hb%d", i);  prefs.putUShort(k, hall_cal[i].back_range);
    }
    // Hall top
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "tn%d", i);  prefs.putUShort(k, hall_top_cal[i].normal);
        snprintf(k, sizeof(k), "tf%d", i);  prefs.putUShort(k, hall_top_cal[i].front_range);
        snprintf(k, sizeof(k), "tb%d", i);  prefs.putUShort(k, hall_top_cal[i].back_range);
    }
    prefs.end();
    Serial.println("[SENSOR_MODULE] Calibration saved to NVS");
}

bool sensor_module_load_calibration() {
    Preferences prefs;
    prefs.begin(CAL_NVS_NS, true);
    if (prefs.getUShort("magic", 0) != CAL_NVS_MAGIC) {
        prefs.end();
        return false;
    }

    // Flex
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "ff%d", i);  flex_cal[i].flat_value     = prefs.getUShort(k, 0);
        snprintf(k, sizeof(k), "fu%d", i);  flex_cal[i].upward_range   = prefs.getUShort(k, 150);
        snprintf(k, sizeof(k), "fd%d", i);  flex_cal[i].downward_range = prefs.getUShort(k, 110);
        snprintf(k, sizeof(k), "fz%d", i);  flex_cal[i].noise_deadzone = prefs.getUShort(k, 0);
        flex_cal[i].calibrated = true;
        flex_ema[i] = (float)flex_cal[i].flat_value;
    }
    flex_ema_init = true;

    // Hall side
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "hn%d", i);  hall_cal[i].normal      = prefs.getUShort(k, 0);
        snprintf(k, sizeof(k), "hf%d", i);  hall_cal[i].front_range = prefs.getUShort(k, 1150);
        snprintf(k, sizeof(k), "hb%d", i);  hall_cal[i].back_range  = prefs.getUShort(k, 400);
        hall_cal[i].calibrated = true;
        hall_ema[i] = (float)hall_cal[i].normal;
    }
    hall_ema_init = true;

    // Hall top
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        char k[12];
        snprintf(k, sizeof(k), "tn%d", i);  hall_top_cal[i].normal      = prefs.getUShort(k, 0);
        snprintf(k, sizeof(k), "tf%d", i);  hall_top_cal[i].front_range = prefs.getUShort(k, 1150);
        snprintf(k, sizeof(k), "tb%d", i);  hall_top_cal[i].back_range  = prefs.getUShort(k, 400);
        hall_top_cal[i].calibrated = true;
        hall_top_ema[i] = (float)hall_top_cal[i].normal;
    }
    hall_top_ema_init = true;
    prefs.end();

    s_calibrated = true;
    return true;
}

// ─────────────────────────────────────────────
//  Calibration info getters (for GUI)
// ─────────────────────────────────────────────
void sensor_module_get_flex_cal(FlexCalibInfo out[NUM_FLEX_SENSORS]) {
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        out[i].flat_value     = flex_cal[i].flat_value;
        out[i].upward_range   = flex_cal[i].upward_range;
        out[i].downward_range = flex_cal[i].downward_range;
        out[i].noise_deadzone = flex_cal[i].noise_deadzone;
    }
}

void sensor_module_get_hall_cal(HallCalibInfo out[NUM_HALL_SENSORS]) {
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        out[i].normal      = hall_cal[i].normal;
        out[i].front_range = hall_cal[i].front_range;
        out[i].back_range  = hall_cal[i].back_range;
    }
}

void sensor_module_get_hall_top_cal(HallCalibInfo out[NUM_HALL_TOP_SENSORS]) {
    for (int i = 0; i < NUM_HALL_TOP_SENSORS; i++) {
        out[i].normal      = hall_top_cal[i].normal;
        out[i].front_range = hall_top_cal[i].front_range;
        out[i].back_range  = hall_top_cal[i].back_range;
    }
}
