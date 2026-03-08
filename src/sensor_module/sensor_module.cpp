/*
 * @file sensor_module/sensor_module.cpp
 * @brief Sensor data processing — calibration, EMA smoothing, percentage
 *        mapping, serial / OLED output, and Edge Impulse inference.
 */
#include "sensor_module.h"
#include "sensors.h"
#include <math.h>
#include <Preferences.h>

// Edge Impulse inference
#include <SignGlove_inferencing.h>

// ─────────────────────────────────────────────
//  Tunables
// ─────────────────────────────────────────────
#define CALIBRATION_TIME_MS    5000
#define PHASE_SAMPLE_TIME_MS   3000   // Per-phase sampling time for new multi-phase calibration
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

// ── Adaptive EMA state (single float per channel) ──
static float    flex_ema[NUM_FLEX_SENSORS];
static float    hall_ema[NUM_HALL_SENSORS];
static bool     flex_ema_init     = false;
static bool     hall_ema_init     = false;

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
    // ── Prefer ADS1115 16-bit ADC when available ──
    // With 16-bit resolution, 4 samples is plenty (vs 16 for the 12-bit ESP ADC).
    // sensors_mux_read() handles I2C mutex + mux select internally.
    if (sensors_ads_available()) {
        const int N = 4;
        uint32_t sum = 0;
        for (int i = 0; i < N; i++)
            sum += sensors_mux_read(ch);
        return (uint16_t)(sum / N);
    }

    // ── Fallback: ESP32 internal ADC — oversampled trimmed mean ──
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

/// Helper: zero all smoothing state for a fresh start.
static void reset_smoothing_state() {
    memset(flex_mf,          0, sizeof(flex_mf));
    memset(hall_mf,          0, sizeof(hall_mf));
    memset(flex_ema,         0, sizeof(flex_ema));
    memset(hall_ema,         0, sizeof(hall_ema));
    flex_ema_init     = false;
    hall_ema_init     = false;
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
    Serial.println("─────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────
//  Multi-phase calibration
// ─────────────────────────────────────────────
//
//  Phase 0 — FLAT HAND (fingers extended):
//    • Flex: set flat_value + noise deadzone
//    • Hall (side/knuckle): magnets CLOSEST → record close_avg
//
//  Phase 1 — CLOSED FIST, THUMBS ON TOP (punching):
//    • Flex Index..Pinky: max-bent → compute range
//    • Hall (side) Index..Pinky: magnets FARTHEST → record far_avg
//    (Thumb excluded — not properly bent in this pose)
//
//  Phase 2 — CLOSED FIST, THUMBS TUCKED INSIDE:
//    • Flex Thumb: max-bent → compute range
//    • Hall (side) Thumb: magnets FARTHEST → record far_avg
//
//  Finalize: compute ranges from sampled extremes.

// Per-phase raw averages (populated by calibrate_phase, consumed by finalize)
static uint16_t phase_flex_flat[NUM_FLEX_SENSORS];       // Phase 0
static uint16_t phase_flex_fist[NUM_FLEX_SENSORS];       // Phase 1 (idx..pky), Phase 2 (thumb)
static uint16_t phase_flex_dz[NUM_FLEX_SENSORS];         // Phase 0 noise deadzone

static uint16_t phase_hall_close[NUM_HALL_SENSORS];      // Phase 0 (flat = close)
static uint16_t phase_hall_far[NUM_HALL_SENSORS];         // Phase 1 (idx..pky), Phase 2 (thumb)

static bool phase_done[CALIB_PHASE_COUNT] = {false, false, false};

const char *sensor_calib_phase_title(CalibPhase phase) {
    switch (phase) {
        case CALIB_PHASE_FLAT_HAND:     return "Step 1/3: Flat Hand";
        case CALIB_PHASE_FIST_THUMB_UP: return "Step 2/3: Fist (Thumbs Up)";
        case CALIB_PHASE_FIST_THUMB_IN: return "Step 3/3: Fist (Thumb Inside)";
        default: return "Calibration";
    }
}

const char *sensor_calib_phase_instruction(CalibPhase phase) {
    switch (phase) {
        case CALIB_PHASE_FLAT_HAND:
            return "Lay your hand completely flat\n"
                   "with fingers extended.";
        case CALIB_PHASE_FIST_THUMB_UP:
            return "Close your fist with thumb\n"
                   "on TOP of the fingers\n"
                   "(like a punching pose).";
        case CALIB_PHASE_FIST_THUMB_IN:
            return "Close your fist with thumb\n"
                   "tucked INSIDE (below)\n"
                   "the fingers.";
        default: return "";
    }
}

void sensor_module_calibrate_phase(CalibPhase phase,
                                   SensorCalibProgressCb progress_cb) {
    Serial.printf("\n[CAL] Phase %d: %s\n", (int)phase,
                  sensor_calib_phase_title(phase));

    uint32_t flex_acc[NUM_FLEX_SENSORS]         = {0};
    uint32_t hall_acc[NUM_HALL_SENSORS]          = {0};

    // For phase 0 flex deadzone: store samples for stddev
    uint16_t flex_samples[NUM_FLEX_SENSORS][MAX_CAL_SAMPLES];
    if (phase == CALIB_PHASE_FLAT_HAND)
        memset(flex_samples, 0, sizeof(flex_samples));

    uint32_t t0 = millis();
    uint16_t n  = 0;

    while (millis() - t0 < PHASE_SAMPLE_TIME_MS) {
        // Dummy MUX_0 read to settle ADC before real readings
        mux_read_oversampled(0);

        for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
            uint16_t v = mux_read_oversampled(flex_channels[i]);
            flex_acc[i] += v;
            if (phase == CALIB_PHASE_FLAT_HAND && n < MAX_CAL_SAMPLES)
                flex_samples[i][n] = v;
        }
        for (int i = 0; i < NUM_HALL_SENSORS; i++)
            hall_acc[i] += mux_read_oversampled(hall_channels[i]);

        n++;

        int pct = (int)(((float)(millis() - t0) / PHASE_SAMPLE_TIME_MS) * 100);
        if (pct > 100) pct = 100;
        if (progress_cb) progress_cb(pct);
        if (n % 15 == 0) Serial.print(".");
        delay(50);
    }
    if (n == 0) n = 1;  // safety
    uint16_t n_flex = (n < MAX_CAL_SAMPLES) ? n : MAX_CAL_SAMPLES;

    // Store phase-specific averages
    switch (phase) {
        case CALIB_PHASE_FLAT_HAND: {
            // Flex: flat value + noise deadzone
            for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
                uint32_t sum = 0;
                for (uint16_t s = 0; s < n_flex; s++) sum += flex_samples[i][s];
                phase_flex_flat[i] = (uint16_t)(sum / n_flex);

                // Stddev-based deadzone
                uint32_t var_sum = 0;
                for (uint16_t s = 0; s < n_flex; s++) {
                    int32_t d = (int32_t)flex_samples[i][s] - (int32_t)phase_flex_flat[i];
                    var_sum += (uint32_t)(d * d);
                }
                float stddev = sqrtf((float)var_sum / n_flex);
                phase_flex_dz[i] = (uint16_t)constrain(
                    (int)(stddev * DZ_SIGMA_MULT + 0.5f), DZ_MIN, DZ_MAX);
            }
            // Hall (side): magnets closest → close value
            for (int i = 0; i < NUM_HALL_SENSORS; i++)
                phase_hall_close[i] = (uint16_t)(hall_acc[i] / n);
            break;
        }
        case CALIB_PHASE_FIST_THUMB_UP: {
            // Flex: fist bent values for Index..Pinky only (thumb not bent here)
            for (int i = 1; i < NUM_FLEX_SENSORS; i++)  // skip i=0 (thumb)
                phase_flex_fist[i] = (uint16_t)(flex_acc[i] / n);
            // Hall (side): magnets farthest → far value for Index..Pinky only
            for (int i = 1; i < NUM_HALL_SENSORS; i++)  // skip i=0 (thumb)
                phase_hall_far[i] = (uint16_t)(hall_acc[i] / n);
            break;
        }
        case CALIB_PHASE_FIST_THUMB_IN: {
            // Thumb flex fist value (thumb is properly bent here)
            phase_flex_fist[0] = (uint16_t)(flex_acc[0] / n);
            // Thumb hall far value (thumb magnet is farthest here)
            phase_hall_far[0] = (uint16_t)(hall_acc[0] / n);
            break;
        }
        default: break;
    }

    phase_done[(int)phase] = true;

    Serial.printf("\n[CAL] Phase %d done (%d samples)\n", (int)phase, n);
}

bool sensor_module_calibrate_finalize() {
    // Verify all phases were sampled
    for (int i = 0; i < CALIB_PHASE_COUNT; i++) {
        if (!phase_done[i]) {
            Serial.printf("[CAL] ERROR: Phase %d not completed!\n", i);
            return false;
        }
    }

    Serial.println("\n[CAL] Finalizing multi-phase calibration...");

    // ── Flex ──
    // flat_value from phase 0, range from difference to phase 1 (fist)
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        flex_cal[i].flat_value     = phase_flex_flat[i];
        flex_cal[i].noise_deadzone = phase_flex_dz[i];

        int16_t diff = (int16_t)phase_flex_fist[i] - (int16_t)phase_flex_flat[i];
        // diff > 0 means bending increases ADC → upward range
        // diff < 0 means bending decreases ADC → downward range
        if (diff > 0) {
            flex_cal[i].upward_range   = (uint16_t)diff;
            flex_cal[i].downward_range = (uint16_t)(diff * 3 / 4); // estimate opposite
        } else if (diff < 0) {
            flex_cal[i].downward_range = (uint16_t)(-diff);
            flex_cal[i].upward_range   = (uint16_t)((-diff) * 3 / 4);
        } else {
            // No change detected — keep defaults
            flex_cal[i].upward_range   = 150;
            flex_cal[i].downward_range = 110;
        }
        flex_cal[i].calibrated = true;
        flex_ema[i] = (float)flex_cal[i].flat_value;
    }
    flex_ema_init = true;

    // ── Hall (side/knuckle) ──
    // close_val from phase 0 (flat), far_val from phase 1 (fist)
    // normal = midpoint, front_range = |close - normal|, back_range = |normal - far|
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        uint16_t close_v = phase_hall_close[i];
        uint16_t far_v   = phase_hall_far[i];
        uint16_t mid     = (close_v + far_v) / 2;

        hall_cal[i].normal = mid;

        if (close_v >= far_v) {
            hall_cal[i].front_range = close_v - mid;
            hall_cal[i].back_range  = mid - far_v;
        } else {
            hall_cal[i].front_range = mid - close_v;
            hall_cal[i].back_range  = far_v - mid;
        }
        // Ensure non-zero ranges
        if (hall_cal[i].front_range == 0) hall_cal[i].front_range = 1;
        if (hall_cal[i].back_range  == 0) hall_cal[i].back_range  = 1;

        hall_cal[i].calibrated = true;
        hall_ema[i] = (float)hall_cal[i].normal;
    }
    hall_ema_init = true;

    s_calibrated = true;
    reset_smoothing_state();

    // Persist to NVS
    sensor_module_save_calibration();

    // Print summary
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   MULTI-PHASE CALIBRATION COMPLETE     ║");
    Serial.println("╚════════════════════════════════════════╝");

    Serial.println("─── Flex ────────────────────────────────");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("  %-6s  Flat=%4d  Fist=%4d  (Up:+%d Down:-%d DZ:±%d)\n",
                      finger_names[i], flex_cal[i].flat_value,
                      phase_flex_fist[i],
                      flex_cal[i].upward_range, flex_cal[i].downward_range,
                      flex_cal[i].noise_deadzone);
    }
    Serial.println("─── Hall (side) ─────────────────────────");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("  %-6s  Close=%4d  Far=%4d  -> Norm=%4d (+%d -%d)\n",
                      finger_names[i], phase_hall_close[i], phase_hall_far[i],
                      hall_cal[i].normal,
                      hall_cal[i].front_range, hall_cal[i].back_range);
    }
    Serial.println("─────────────────────────────────────────\n");

    // Reset phase flags for next calibration run
    for (int i = 0; i < CALIB_PHASE_COUNT; i++) phase_done[i] = false;

    return true;
}

bool sensor_module_is_calibrated() { return s_calibrated; }

void sensor_module_process(const SensorData &raw, ProcessedSensorData &out) {
    // Flex — median → adaptive EMA → deadzone → %
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        out.flex_raw[i] = raw.flex[i];
        out.flex_pct[i] = calc_flex_pct(i, raw.flex[i]);
    }
    if (!flex_ema_init) flex_ema_init = true;   // first frame seeds done

    // Hall side — median → adaptive EMA → %
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        out.hall_raw[i] = raw.hall[i];
        out.hall_pct[i] = calc_hall_side_pct(i, raw.hall[i]);
    }
    if (!hall_ema_init) hall_ema_init = true;

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
    off += snprintf(buf + off, buf_len - off,
                    "\nIMU P:%.0f R:%.0f",
                    pd.pitch, pd.roll);
}

// ─────────────────────────────────────────────
//  Edge Impulse sliding window buffer
// ─────────────────────────────────────────────
// The model expects EI_CLASSIFIER_RAW_SAMPLE_COUNT (27) frames,
// each with EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME (18) features.
// Total DSP input = 27 * 18 = 486 floats.

static float ei_buf[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
static int   ei_buf_idx   = 0;
static bool  ei_buf_full  = false;
static uint32_t ei_last_push_ms = 0;

// ei_printf implementation required by EI SDK
void ei_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/// Callback used by EI to read raw features from our buffer
static int ei_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, ei_buf + offset, length * sizeof(float));
    return 0;
}

void sensor_module_ei_push(const SensorData &raw) {
    // Write one frame (18 features) into the sliding window buffer.
    // Order must match the training data CSV:
    //   flex0..flex4, hall0..hall4, ax,ay,az, gx,gy,gz, pitch, roll
    float frame[EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME];
    frame[0]  = (float)raw.flex[0];
    frame[1]  = (float)raw.flex[1];
    frame[2]  = (float)raw.flex[2];
    frame[3]  = (float)raw.flex[3];
    frame[4]  = (float)raw.flex[4];
    frame[5]  = (float)raw.hall[0];
    frame[6]  = (float)raw.hall[1];
    frame[7]  = (float)raw.hall[2];
    frame[8]  = (float)raw.hall[3];
    frame[9]  = (float)raw.hall[4];
    frame[10] = raw.accel_x;
    frame[11] = raw.accel_y;
    frame[12] = raw.accel_z;
    frame[13] = raw.gyro_x;
    frame[14] = raw.gyro_y;
    frame[15] = raw.gyro_z;
    frame[16] = raw.pitch;
    frame[17] = raw.roll;

    // Slide the buffer left by one frame and append the new frame at the end
    if (ei_buf_idx >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        // Buffer is full — shift left by one frame
        memmove(ei_buf,
                ei_buf + EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
                (EI_CLASSIFIER_RAW_SAMPLE_COUNT - 1) * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME * sizeof(float));
        ei_buf_idx = EI_CLASSIFIER_RAW_SAMPLE_COUNT - 1;
    }

    memcpy(ei_buf + ei_buf_idx * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
           frame,
           EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME * sizeof(float));
    ei_buf_idx++;

    if (ei_buf_idx >= EI_CLASSIFIER_RAW_SAMPLE_COUNT)
        ei_buf_full = true;
}

bool sensor_module_ei_ready() {
    return ei_buf_full;
}

const char *sensor_module_predict(ProcessedSensorData &pd) {
    if (!ei_buf_full) {
        strncpy(pd.predicted_label, "---", sizeof(pd.predicted_label));
        pd.prediction_confidence = 0.0f;
        return pd.predicted_label;
    }

    // Set up the signal structure for EI
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &ei_get_data;

    // Run the classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false /* debug */);

    if (err != EI_IMPULSE_OK) {
        Serial.printf("[EI] Classifier error: %d\n", (int)err);
        strncpy(pd.predicted_label, "ERROR", sizeof(pd.predicted_label));
        pd.prediction_confidence = 0.0f;
        return pd.predicted_label;
    }

    // Find the label with highest confidence
    float max_conf = 0.0f;
    int   max_idx  = -1;
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > max_conf) {
            max_conf = result.classification[i].value;
            max_idx  = (int)i;
        }
    }

    if (max_idx >= 0 && max_conf > 0.5f) {
        strncpy(pd.predicted_label,
                result.classification[max_idx].label,
                sizeof(pd.predicted_label));
        pd.prediction_confidence = max_conf;
    } else {
        strncpy(pd.predicted_label, "---", sizeof(pd.predicted_label));
        pd.prediction_confidence = max_conf;
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
