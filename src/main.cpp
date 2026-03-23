/*
 * @file main.cpp
 * @brief Entry point — Signa – Sign Language Translator v4.0
 *
 * Wires together:
 *   display  →  LVGL driver + AMOLED
 *   gui      →  LVGL screens (menu / train / predict / settings / tests)
 *   sensors  →  Multiplexer + MPU6050
 *   audio    →  I2S + MAX98357A
 *   power    →  Button + battery + deep sleep
 *   web      →  WiFi AP + SSE server (WEB mode only)
 */
#include <Arduino.h>
#include <esp_sleep.h>
#include <lvgl.h>
#include "config.h"
#include "display.h"
#include "gui/gui.h"
#include "sensors.h"
#include "audio.h"
#include "power.h"
#include "web_server.h"
#include "system_info.h"
#include "sensor_module/sensor_module.h"
#include "test_sensors_module.h"
#include "test_sound_module.h"

// ════════════════════════════════════════════════════════════════════
//  State
// ════════════════════════════════════════════════════════════════════
static AppMode   current_mode  = MODE_MENU;
static SensorData sensor_data  = {};
static ProcessedSensorData processed_data = {};
static char      gesture_text[32] = "---";
static uint32_t  last_sensor   = 0;
static uint32_t  last_display  = 0;
static uint32_t  last_bat_read = 0;
static uint32_t  train_sample_count = 0;

// EI inference timing
static uint32_t  last_ei_push = 0;
static uint32_t  last_ei_infer = 0;
static uint32_t  last_motion_ms = 0;
static uint32_t  current_infer_interval_ms = EI_INFER_INTERVAL_MS;

// Motion tracking for adaptive fast/slow inference cadence
static bool      infer_motion_seeded = false;
static int8_t    prev_flex_pct[NUM_FLEX_SENSORS] = {0};
static int8_t    prev_hall_pct[NUM_HALL_SENSORS] = {0};
static float     prev_pitch = 0.0f;
static float     prev_roll  = 0.0f;

// Prediction handover guard (reduces flicker on near-tie classes)
static char      stable_pred_label[32] = "---";
static float     stable_pred_conf = 0.0f;
static char      pending_pred_label[32] = "---";
static uint8_t   pending_pred_count = 0;
static uint32_t  pending_pred_since_ms = 0;
static uint8_t   uncertain_pred_count = 0;
static char      last_spoken_label[32] = "";  // track last spoken label to avoid repeat

// CPU usage tracking
static uint32_t  cpu_busy_us   = 0;     // accumulated busy time (µs)
static uint32_t  cpu_window_us = 0;     // accumulated total time (µs)
static uint32_t  last_loop_us  = 0;     // micros() at start of previous loop
static uint32_t  last_cpu_upd  = 0;     // millis() of last CPU% push
static int       cpu_pct       = 0;     // smoothed CPU %

// Auto-sleep / lock-screen state
static bool      sleep_warn_active = false;  // is warning dialog currently showing?
static bool      lock_active       = false;  // is lock screen currently active?
static uint32_t  last_lock_bat     = 0;      // millis() of last battery update on lock screen

// Double-tap detection for lock screen dismiss
static uint32_t  lock_prev_idle_ms = UINT32_MAX; // idle_ms from previous loop iteration
static uint32_t  lock_last_tap_ms  = 0;           // millis() of most recent tap while locked
static int       lock_tap_count    = 0;            // consecutive taps within window

// Forward declaration — defined further below, used in on_mode_change
#ifdef SERIAL_COMMAND
static void stream_serial_cb(const SensorData &d);
#endif

static bool prediction_motion_detected(const ProcessedSensorData &pd) {
    if (!infer_motion_seeded) {
        for (int i = 0; i < NUM_FLEX_SENSORS; i++) prev_flex_pct[i] = pd.flex_pct[i];
        for (int i = 0; i < NUM_HALL_SENSORS; i++) prev_hall_pct[i] = pd.hall_pct[i];
        prev_pitch = pd.pitch;
        prev_roll  = pd.roll;
        infer_motion_seeded = true;
        return false;
    }

    int max_flex_delta = 0;
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        int d = abs((int)pd.flex_pct[i] - (int)prev_flex_pct[i]);
        if (d > max_flex_delta) max_flex_delta = d;
        prev_flex_pct[i] = pd.flex_pct[i];
    }

    int max_hall_delta = 0;
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int d = abs((int)pd.hall_pct[i] - (int)prev_hall_pct[i]);
        if (d > max_hall_delta) max_hall_delta = d;
        prev_hall_pct[i] = pd.hall_pct[i];
    }

    float pitch_delta = fabsf(pd.pitch - prev_pitch);
    float roll_delta  = fabsf(pd.roll  - prev_roll);
    prev_pitch = pd.pitch;
    prev_roll  = pd.roll;

    return (max_flex_delta >= EI_MOTION_FLEX_DELTA) ||
           (max_hall_delta >= EI_MOTION_HALL_DELTA) ||
           (pitch_delta >= EI_MOTION_ANGLE_DELTA) ||
           (roll_delta  >= EI_MOTION_ANGLE_DELTA);
}

// ════════════════════════════════════════════════════════════════════
//  GUI → main mode-change callback
// ════════════════════════════════════════════════════════════════════
static void on_mode_change(AppMode m) {
    AppMode prev = current_mode;
    current_mode = m;

    // Activate/deactivate background sensor reading based on mode.
    // Menu & Settings don't need live sensor data.
    bool need_sensors = (m == MODE_TRAIN || m == MODE_PREDICT_LOCAL
                      || m == MODE_PREDICT_WEB
#ifdef SERIAL_COMMAND
                      || m == MODE_DATA_COLLECT
#endif
                      || m == MODE_TEST);
    sensors_set_active(need_sensors);

#ifdef SERIAL_COMMAND
    // Enable/disable 30 Hz serial streaming callback on Core 0.
    // Only active in modes that pipe data to Edge Impulse / Web Serial.
    bool need_stream = (m == MODE_TRAIN || m == MODE_DATA_COLLECT);
    sensors_set_stream_cb(need_stream ? stream_serial_cb : NULL);
#endif

    // Start / stop web server as needed
    if (m == MODE_PREDICT_WEB && !web_server_is_running()) {
        web_server_start();
        gui_show_web_qr(web_server_get_url().c_str());
    }
    if (prev == MODE_PREDICT_WEB && m != MODE_PREDICT_WEB) {
        web_server_stop();
    }

    Serial.printf("[MAIN] Mode → %d\n", (int)m);

    // Reset adaptive inference state when mode changes
    last_ei_push = 0;
    last_ei_infer = 0;
    last_motion_ms = 0;
    current_infer_interval_ms = EI_INFER_INTERVAL_MS;
    infer_motion_seeded = false;
    stable_pred_conf = 0.0f;
    strncpy(stable_pred_label, "---", sizeof(stable_pred_label));
    strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
    pending_pred_count = 0;
    pending_pred_since_ms = 0;
    uncertain_pred_count = 0;

    power_reset_idle_timer();
}

// ════════════════════════════════════════════════════════════════════
//  Power menu callback (wired from GUI)
// ════════════════════════════════════════════════════════════════════
// IMPORTANT: The LVGL button callback fires INSIDE lv_timer_handler().
// Calling esp_deep_sleep_start() / esp_light_sleep_start() or heavy
// I/O (display_off, I2C) from within LVGL's event chain is unsafe —
// LVGL holds internal state that can deadlock or corrupt.  Instead we
// store the requested action and execute it in loop() AFTER
// lv_timer_handler() has returned.
static volatile PowerAction pending_power_action = PWR_NONE;

static void on_power_action(PowerAction action) {
    if (action == PWR_CANCEL) {
        Serial.println("[MAIN] Power menu → Cancel");
        return;   // nothing to defer
    }
    // Just record the action; execution happens in loop()
    pending_power_action = action;
}

// Called from loop() outside LVGL to safely execute the power action
static void execute_pending_power_action() {
    PowerAction act = pending_power_action;
    if (act == PWR_NONE) return;
    pending_power_action = PWR_NONE;

    switch (act) {
    case PWR_SLEEP:
        Serial.println("[MAIN] Power menu → Sleep (light sleep)");
        // Clear any active auto-sleep UI before entering sleep
        if (sleep_warn_active) { gui_hide_sleep_warning(); sleep_warn_active = false; }
        if (lock_active)       { gui_hide_lock_screen();   lock_active = false; }
        audio_stop();
        power_light_sleep();
        // Execution resumes here after wakeup from light sleep
        lv_disp_trig_activity(NULL);   // reset LVGL inactivity so auto-sleep doesn't re-trigger
        Serial.println("[MAIN] Resumed from light sleep");
        // Drain any USB state-change flag that accumulated while sleeping
        // so the popup is NOT triggered on wake (only on live plug/unplug).
        power_usb_state_changed();
        break;

    case PWR_SHUTDOWN:
        Serial.println("[MAIN] Power menu → Shutdown (deep sleep)");
        audio_stop();
        if (web_server_is_running()) web_server_stop();
        power_deep_sleep();
        // Never reaches here — ESP resets on deep sleep wakeup
        break;

    case PWR_RESTART:
        Serial.println("[MAIN] Power menu → Restart");
        audio_stop();
        if (web_server_is_running()) web_server_stop();
        power_restart();
        // Never reaches here
        break;

    default:
        break;
    }
}

// ════════════════════════════════════════════════════════════════════
//  Settings / test callbacks (wired from GUI)
// ════════════════════════════════════════════════════════════════════
static void on_brightness_change(uint8_t val) {
    display_set_brightness(val);
    Serial.printf("[MAIN] Brightness → %d\n", val);
}

static void on_volume_change(uint8_t val) {
    audio_set_volume(val / 100.0f);
    Serial.printf("[MAIN] Volume → %d%%\n", val);
}

static void on_test_speaker() {
    test_sound_run_all();
    Serial.println("[MAIN] Speaker test — full suite (via test_sound_module)");
}

static void on_test_oled() {
    // Flash the display brightness to confirm OLED is alive
    display_set_brightness(255);
    Serial.println("[MAIN] OLED test — full brightness flash");
    // Restore after a short delay (non-blocking via flag)
}

#ifdef SERIAL_COMMAND
// ════════════════════════════════════════════════════════════════════
//  Configurable streaming frequency  (can be changed via serial cmd)
// ════════════════════════════════════════════════════════════════════
static uint32_t stream_interval_ms = TRAIN_SERIAL_INTERVAL_MS;  // default ~30 Hz

// ════════════════════════════════════════════════════════════════════
//  Serial streaming — driven from Core 0 sensor task at true 30 Hz
// ════════════════════════════════════════════════════════════════════
// This callback is invoked at 30 Hz directly from the sensor task on
// Core 0 (via sensors_set_stream_cb).  Keeping serial output on the
// same core as the sensor read avoids jitter from LVGL rendering on
// Core 1, which was the cause of the reported 14-19 Hz effective rate.
static void stream_serial_cb(const SensorData &d) {
    sensor_module_stream_raw_csv(d);
}

// ════════════════════════════════════════════════════════════════════
//  Serial command handler (for Web Serial data collection app)
// ════════════════════════════════════════════════════════════════════
static char serial_cmd_buf[64];
static int  serial_cmd_idx = 0;

static void handle_serial_commands() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_cmd_idx > 0) {
                serial_cmd_buf[serial_cmd_idx] = '\0';

                // Trim leading/trailing whitespace
                char *cmd = serial_cmd_buf;
                while (*cmd == ' ' || *cmd == '\t') cmd++;
                int len = strlen(cmd);
                while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t')) len--;
                cmd[len] = '\0';

                if (strcmp(cmd, "cal_dump") == 0) {
                    sensor_module_dump_calibration_json();
                } else if (strcmp(cmd, "data_start") == 0) {
                    current_mode = MODE_DATA_COLLECT;
                    sensors_set_active(true);
                    sensors_set_stream_cb(stream_serial_cb);
                    Serial.println("OK:data_start");
                } else if (strcmp(cmd, "data_stop") == 0) {
                    current_mode = MODE_MENU;
                    sensors_set_stream_cb(NULL);
                    Serial.println("OK:data_stop");
                } else if (strncmp(cmd, "set_freq=", 9) == 0) {
                    int hz = atoi(cmd + 9);
                    if (hz >= 1 && hz <= 100) {
                        stream_interval_ms = 1000 / hz;
                        sensors_set_rate_hz(hz);
                        Serial.printf("OK:freq=%d\n", hz);
                    } else {
                        Serial.println("ERR:freq out of range (1-100)");
                    }
                } else if (strcmp(cmd, "cal_start") == 0) {
                    // Remote calibration: phase 0 (flat hand)
                    sensors_set_active(true);
                    Serial.println("OK:cal_phase=0,sampling");
                    sensor_module_calibrate_phase(CALIB_PHASE_FLAT_HAND, nullptr);
                    Serial.println("OK:cal_phase=0,done");
                } else if (strcmp(cmd, "cal_phase1") == 0) {
                    Serial.println("OK:cal_phase=1,sampling");
                    sensor_module_calibrate_phase(CALIB_PHASE_FIST_THUMB_UP, nullptr);
                    Serial.println("OK:cal_phase=1,done");
                } else if (strcmp(cmd, "cal_phase2") == 0) {
                    Serial.println("OK:cal_phase=2,sampling");
                    sensor_module_calibrate_phase(CALIB_PHASE_FIST_THUMB_IN, nullptr);
                    Serial.println("OK:cal_phase=2,done");
                } else if (strcmp(cmd, "cal_finalize") == 0) {
                    bool ok = sensor_module_calibrate_finalize();
                    if (ok) {
                        sensor_module_save_calibration();
                        Serial.println("OK:cal_finalized");
                        sensor_module_dump_calibration_json();
                    } else {
                        Serial.println("ERR:cal_finalize_failed");
                    }
                } else if (strcmp(cmd, "ping") == 0) {
                    Serial.println("OK:pong");
                } else if (strcmp(cmd, "info") == 0) {
                    int hz = (stream_interval_ms > 0) ? (1000 / stream_interval_ms) : 30;
                    Serial.printf("OK:SignGlove,freq=%d,features=18,cal=%d\n",
                                  hz, sensor_module_is_calibrated() ? 1 : 0);
                }
                serial_cmd_idx = 0;
            }
        } else if (serial_cmd_idx < (int)sizeof(serial_cmd_buf) - 1) {
            serial_cmd_buf[serial_cmd_idx++] = c;
        }
    }
}
#else
static const uint32_t stream_interval_ms = TRAIN_SERIAL_INTERVAL_MS;
#endif  // SERIAL_COMMAND

// ════════════════════════════════════════════════════════════════════
//  Gesture classification via Edge Impulse model
// ════════════════════════════════════════════════════════════════════
static void classify_gesture() {
    if (!sensor_module_ei_ready()) return;

    uint32_t now_ms = millis();

    const char *raw_label = sensor_module_predict(processed_data);
    float raw_conf = processed_data.prediction_confidence;

    const char *label = sensor_module_is_nonsign_label(raw_label) ? "---" : raw_label;

    // Robust anti-random state machine:
    //  1) Need higher confidence + N consecutive frames to ENTER/SWITCH sign
    //  2) Lower threshold to STAY in current sign (hysteresis)
    //  3) During low confidence / transition noise, fall back to idle (---)
    bool candidate_is_sign = (strcmp(label, "---") != 0) && (raw_conf >= EI_SIGN_ENTER_CONF);

    // "---" can mean either explicit non-sign class OR uncertainty gate from EI.
    // If this repeats while a sign is latched, clear it so UI can return to idle.
    bool reject_or_uncertain = (strcmp(label, "---") == 0);
    if (strcmp(stable_pred_label, "---") != 0 && reject_or_uncertain) {
        if (uncertain_pred_count < 255) uncertain_pred_count++;
    } else {
        uncertain_pred_count = 0;
    }

    if (strcmp(stable_pred_label, "---") != 0 &&
        uncertain_pred_count >= EI_UNCERTAIN_RELEASE_FRAMES) {
        strncpy(stable_pred_label, "---", sizeof(stable_pred_label));
        stable_pred_conf = 0.0f;
        strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
        pending_pred_count = 0;
        pending_pred_since_ms = 0;
        uncertain_pred_count = 0;
    }

    // If currently in a sign and confidence collapses, release to idle quickly.
    if (strcmp(stable_pred_label, "---") != 0 && raw_conf < EI_SIGN_EXIT_CONF) {
        strncpy(stable_pred_label, "---", sizeof(stable_pred_label));
        stable_pred_conf = 0.0f;
        strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
        pending_pred_count = 0;
        pending_pred_since_ms = 0;
        uncertain_pred_count = 0;
    }

    if (strcmp(stable_pred_label, "---") == 0) {
        // Idle → Sign requires a 1000 ms confirmation window
        if (candidate_is_sign) {
            if (strcmp(label, pending_pred_label) == 0) {
                if (pending_pred_count < 255) pending_pred_count++;
            } else {
                strncpy(pending_pred_label, label, sizeof(pending_pred_label));
                pending_pred_count = 1;
                pending_pred_since_ms = now_ms;
            }

            if (pending_pred_since_ms == 0) pending_pred_since_ms = now_ms;

            if ((now_ms - pending_pred_since_ms) >= EI_SIGN_CONFIRM_MS) {
                strncpy(stable_pred_label, pending_pred_label, sizeof(stable_pred_label));
                stable_pred_conf = raw_conf;
                uncertain_pred_count = 0;
            }
        } else {
            strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
            pending_pred_count = 0;
            pending_pred_since_ms = 0;
        }
    } else {
        // Already in a sign
        if (strcmp(label, stable_pred_label) == 0) {
            // Same sign keeps lock with lower "stay" threshold
            stable_pred_conf = raw_conf;
            strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
            pending_pred_count = 0;
            pending_pred_since_ms = 0;
        } else if (candidate_is_sign) {
            // Sign → different sign requires both margin and 1000 ms confirmation
            bool strong_switch = (raw_conf >= EI_SWITCH_HARD_CONF) ||
                                 (raw_conf >= (stable_pred_conf + EI_SWITCH_MARGIN));
            if (strong_switch) {
                if (strcmp(label, pending_pred_label) == 0) {
                    if (pending_pred_count < 255) pending_pred_count++;
                } else {
                    strncpy(pending_pred_label, label, sizeof(pending_pred_label));
                    pending_pred_count = 1;
                    pending_pred_since_ms = now_ms;
                }

                if (pending_pred_since_ms == 0) pending_pred_since_ms = now_ms;

                if ((now_ms - pending_pred_since_ms) >= EI_SIGN_CONFIRM_MS) {
                    strncpy(stable_pred_label, pending_pred_label, sizeof(stable_pred_label));
                    stable_pred_conf = raw_conf;
                    uncertain_pred_count = 0;
                }
            } else {
                strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
                pending_pred_count = 0;
                pending_pred_since_ms = 0;
            }
        } else {
            // Non-sign / weak confidence while in sign: do not jump to random sign.
            // Release already handled by EI_SIGN_EXIT_CONF above.
            strncpy(pending_pred_label, "---", sizeof(pending_pred_label));
            pending_pred_count = 0;
            pending_pred_since_ms = 0;
        }
    }

    // Live UI label: show prediction instantly with a lower confidence gate.
    // Stable label (above) is still used for high-confidence commit/audio logic.
    bool live_is_sign = (strcmp(label, "---") != 0) && (raw_conf >= EI_SIGN_EXIT_CONF);
    if (!live_is_sign) {
        strncpy(gesture_text, "---", sizeof(gesture_text));
    } else {
        strncpy(gesture_text, label, sizeof(gesture_text));
    }
}

// ════════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(EI_SERIAL_BAUD);
    // With ARDUINO_USB_CDC_ON_BOOT=1, Serial is USB CDC.
    // If no USB host is connected (e.g. wake from deep sleep without
    // the serial monitor open), any Serial.print() call will block
    // until the host connects — which never happens, freezing the boot.
    // setTxTimeoutMs(0) makes every write return immediately if the
    // host is absent, so the display (and everything else) still boots.
    Serial.setTxTimeoutMs(0);

    // Shorter delay on deep-sleep wakeup (boot is effectively a restart)
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ||
        esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        delay(50);
        Serial.println("\n=== Signa v4.0 (wake) ===");
    } else {
        delay(200);
        Serial.println("\n=== Signa v4.0 ===");
    }

    // 1. Display + LVGL
    display_init();
    Serial.println("[MAIN] Display ready");

    // 2. GUI (creates all LVGL screens, shows splash)
    gui_init();
    display_set_brightness(gui_get_brightness());  // apply saved brightness (syncs current_brightness)
    gui_register_mode_callback(on_mode_change);
    gui_register_brightness_cb(on_brightness_change);
    gui_register_volume_cb(on_volume_change);
    gui_register_test_speaker_cb(on_test_speaker);
    gui_register_test_oled_cb(on_test_oled);
    gui_register_power_cb(on_power_action);
    Serial.println("[MAIN] GUI ready");

    // 3. Sensors
    sensors_init();
    Serial.println("[MAIN] Sensors ready");

    // 3b. Sensor processing module (calibration, percentages, prediction)
    sensor_module_init();
    Serial.println("[MAIN] Sensor module ready");

    // 4. Audio
    audio_init();
    audio_set_volume(gui_get_volume() / 100.0f);   // apply saved volume
    Serial.println("[MAIN] Audio ready");

    // 5. Power
    power_init();
    Serial.println("[MAIN] Power ready");
    
#ifdef I2C_FAST_MODE
    // 5b. Lock I2C bus to Fast-mode (400 kHz) now that every driver that   
    // calls Wire.begin() (display/touch, SY6970) has already initialised.
    // Wire.begin() resets the clock to 100 kHz each time, so we must set
    // it once here — after all begin() calls — as the global bus speed.
    Wire.setClock(I2C_FAST_MODE_HZ);
    Serial.printf("[MAIN] I2C bus clock → %d Hz (Fast-mode)\n", I2C_FAST_MODE_HZ);
#endif

    // 6. System info
    sysinfo_init();
    sysinfo_print_summary();
    Serial.println("[MAIN] SystemInfo ready");

    Serial.println("[MAIN] Setup complete — entering loop");
}

// ════════════════════════════════════════════════════════════════════
//  loop
// ════════════════════════════════════════════════════════════════════
void loop() {
    uint32_t now = millis();
    uint32_t loop_start = micros();

    // ── LVGL tick ──────────────────────────────────────────────────
    lv_timer_handler();

    // ── Execute deferred power actions (MUST be outside LVGL) ──────
    // The power menu callback only sets a flag; the actual sleep /
    // deep-sleep / restart happens here, safely outside lv_timer_handler.
    execute_pending_power_action();

    // ── Power management ───────────────────────────────────────────
    power_update();

    // Long press → show power menu dialog (Sleep / Shutdown / Restart / Cancel)
    if (power_button_long_press() && !gui_power_menu_visible()) {
        // Dismiss any auto-sleep UI first
        if (sleep_warn_active) { gui_hide_sleep_warning(); sleep_warn_active = false; }
        if (lock_active)       { gui_hide_lock_screen();   lock_active = false; }
        gui_show_power_menu();
        lv_disp_trig_activity(NULL);
    }

    // Short press while power menu is open → dismiss it
    if (power_button_pressed() && gui_power_menu_visible()) {
        gui_hide_power_menu();
        lv_disp_trig_activity(NULL);
    }
    // Short press while lock screen is active → dismiss lock
    else if (power_button_pressed() && lock_active) {
        gui_hide_lock_screen();
        lock_active = false;
        lv_disp_trig_activity(NULL);
    }
    // Short press otherwise → immediate light sleep
    else if (power_button_pressed() && !gui_power_menu_visible()) {
        pending_power_action = PWR_SLEEP;
    }

    // ── Auto-sleep / lock-screen logic ─────────────────────────────
    // Uses LVGL's built-in inactivity tracker (reset by touch events).
    // cfg_sleep_min is 1–30 minutes.
    {
        const uint32_t idle_ms  = lv_disp_get_inactive_time(NULL);
        const uint32_t sleep_ms = (uint32_t)gui_get_sleep_min() * 60UL * 1000UL;
        // Warning duration: 20 % of total, capped at 120 s, minimum 1 s
        int warn_sec = ((int)gui_get_sleep_min() * 60 * 20) / 100;
        if (warn_sec > 120) warn_sec = 120;
        if (warn_sec < 1)   warn_sec = 1;
        const uint32_t warn_ms = sleep_ms - (uint32_t)warn_sec * 1000UL;

        // Don't auto-sleep while power menu is open or action pending
        bool skip = gui_power_menu_visible() || pending_power_action != PWR_NONE;

        if (lock_active && !skip) {
            // ── Lock screen is showing ─────────────────────────
            // Each touch resets LVGL inactivity → idle_ms drops back to ~0.
            // Detect a new tap: previous loop's idle was large, this loop it's near zero.
            bool new_tap = (idle_ms < 80) && (lock_prev_idle_ms > 150);
            lock_prev_idle_ms = idle_ms;

            if (new_tap) {
                uint32_t gap = now - lock_last_tap_ms;
                if (gap < 500) {
                    lock_tap_count++;
                } else {
                    lock_tap_count = 1;   // too slow → restart count
                }
                lock_last_tap_ms = now;
                Serial.printf("[MAIN] Lock tap #%d (gap %lu ms)\n", lock_tap_count, (unsigned long)gap);
            }

            if (lock_tap_count >= 2) {
                // Double-tap confirmed → dismiss lock screen
                gui_hide_lock_screen();
                lock_active       = false;
                lock_tap_count    = 0;
                lock_prev_idle_ms = UINT32_MAX;
                Serial.println("[MAIN] Lock screen dismissed (double-tap)");
            } else {
                // Update lock screen content while waiting
                if (current_mode == MODE_PREDICT_LOCAL || current_mode == MODE_PREDICT_WEB) {
                    gui_lock_update_gesture(gesture_text);
                }
                if (now - last_lock_bat >= 60000) {
                    last_lock_bat = now;
                    gui_lock_update_battery(power_battery_percent());
                }
            }
        } else if (sleep_warn_active && !skip) {
            // ── Warning dialog is showing ──────────────────────
            if (idle_ms < warn_ms) {
                // User touched → cancel warning
                gui_hide_sleep_warning();
                sleep_warn_active = false;
                Serial.println("[MAIN] Auto-sleep cancelled (touch)");
            } else {
                int remaining = (int)((sleep_ms - idle_ms) / 1000);
                if (remaining <= 0) {
                    // Time's up — execute sleep action
                    gui_hide_sleep_warning();
                    sleep_warn_active = false;
                    bool is_active_mode = (current_mode == MODE_TRAIN
                                        || current_mode == MODE_PREDICT_LOCAL
                                        || current_mode == MODE_PREDICT_WEB);
                    if (is_active_mode && gui_get_lock_screen_on()) {
                        lock_active       = true;
                        lock_tap_count    = 0;
                        lock_prev_idle_ms = UINT32_MAX;
                        last_lock_bat = now;
                        gui_lock_update_battery(power_battery_percent());
                        gui_show_lock_screen(current_mode);
                        Serial.println("[MAIN] Auto-sleep → lock screen");
                    } else {
                        pending_power_action = PWR_SLEEP;
                        Serial.println("[MAIN] Auto-sleep → light sleep");
                    }
                } else {
                    gui_show_sleep_warning(remaining);
                }
            }
        } else if (!skip && !lock_active && !sleep_warn_active) {
            // ── Check if we should start the warning ───────────
            if (idle_ms >= warn_ms && idle_ms < sleep_ms) {
                sleep_warn_active = true;
                int remaining = (int)((sleep_ms - idle_ms) / 1000);
                gui_show_sleep_warning(remaining);
                Serial.printf("[MAIN] Auto-sleep warning (%d s)\n", remaining);
            } else if (idle_ms >= sleep_ms) {
                // Missed warning window (shouldn't happen normally)
                bool is_active_mode = (current_mode == MODE_TRAIN
                                    || current_mode == MODE_PREDICT_LOCAL
                                    || current_mode == MODE_PREDICT_WEB);
                if (is_active_mode && gui_get_lock_screen_on()) {
                    lock_active       = true;
                    lock_tap_count    = 0;
                    lock_prev_idle_ms = UINT32_MAX;
                    last_lock_bat = now;
                    gui_lock_update_battery(power_battery_percent());
                    gui_show_lock_screen(current_mode);
                } else {
                    pending_power_action = PWR_SLEEP;
                }
            }
        }
    }

    // ── Read sensors (non-blocking — copies from background task) ──
    if (now - last_sensor >= SENSOR_READ_INTERVAL_MS) {
        last_sensor = now;
        sensors_read(sensor_data);
        sensor_module_process(sensor_data, processed_data);

        // Push raw data into EI sliding window at model rate (30 Hz)
        if (current_mode == MODE_PREDICT_LOCAL || current_mode == MODE_PREDICT_WEB) {
            if (now - last_ei_push >= EI_PUSH_INTERVAL_MS) {
                last_ei_push = now;
                sensor_module_ei_push(sensor_data);
            }

            // Adaptive fast mode: infer faster while hand is moving,
            // then decay to slower cadence when idle.
            if (prediction_motion_detected(processed_data)) {
                last_motion_ms = now;
            }
            current_infer_interval_ms =
                ((now - last_motion_ms) <= EI_FAST_MODE_HOLD_MS)
                ? EI_INFER_INTERVAL_FAST_MS
                : EI_INFER_INTERVAL_SLOW_MS;

            if (now - last_ei_infer >= current_infer_interval_ms) {
                last_ei_infer = now;
                classify_gesture();
            }
        }
    }

#ifdef SERIAL_COMMAND
    // ── Handle serial commands from Web Serial data collection app ──
    handle_serial_commands();
#endif

    // ── Mode-specific logic ────────────────────────────────────────
    switch (current_mode) {

    case MODE_TRAIN:
        // Serial streaming handled by Core 0 callback (stream_serial_cb)
        {
            // Count serial frames for the GUI (approximate — incremented by lv frame period)
            static uint32_t last_cnt_check = 0;
            if (now - last_cnt_check >= stream_interval_ms) {
                last_cnt_check = now;
                train_sample_count++;
            }
        }
        // Update live sensor display on train screen
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_train_update(sensor_data, processed_data, train_sample_count);
        }
        break;

    case MODE_PREDICT_LOCAL:
        if (!lock_active && now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            if (gui_local_show_sensors())
                gui_local_sensor_update(processed_data);
            // Always show the gesture label
            gui_set_gesture(gesture_text);
            gui_set_predict_confidence(processed_data.prediction_confidence);

            // Update status text
            if (!sensor_module_ei_ready()) {
                gui_set_predict_status(LV_SYMBOL_REFRESH " Buffering...");
            } else if (strcmp(gesture_text, "---") == 0) {
                gui_set_predict_status(LV_SYMBOL_EYE_OPEN " Listening...");
            } else {
                gui_set_predict_status(LV_SYMBOL_OK " Detected!");
            }
        }
        // Play audio when a gesture is detected (only when label changes)
        if (gui_local_use_speech() && !audio_is_playing() && !lock_active) {
            if (strcmp(stable_pred_label, "---") != 0 &&
                strcmp(stable_pred_label, "ERROR") != 0 &&
                !sensor_module_is_nonsign_label(stable_pred_label) &&   // never speak null classes
                strcmp(stable_pred_label, last_spoken_label) != 0) {
                // Build the audio file path: /<voice>/<label>.mp3
                char audio_path[64];
                snprintf(audio_path, sizeof(audio_path), "/%s/%s.mp3",
                         gui_local_voice_dir(), stable_pred_label);

                // Only attempt playback if the file actually exists on LittleFS.
                // This silently skips FSL labels not yet recorded as audio.
                if (audio_mp3_exists(audio_path)) {
                    audio_play_mp3(audio_path);
                    Serial.printf("[MAIN] Playing audio: %s\n", audio_path);
                } else {
                    Serial.printf("[MAIN] No audio file for label '%s' (%s) — skipping\n",
                                  stable_pred_label, audio_path);
                }
                // Lock the label either way to prevent repeat-spam
                strncpy(last_spoken_label, stable_pred_label, sizeof(last_spoken_label));
            }
            // Only reset the spoken-label guard once audio has finished and
            // the gesture has gone back to idle — avoids immediate re-trigger.
            if (strcmp(stable_pred_label, "---") == 0 && !audio_is_playing()) {
                last_spoken_label[0] = '\0';
            }
        }
        break;

case MODE_PREDICT_WEB:
    if (!lock_active) {
        // WEB output uses stabilized prediction timing (same handover guard).
        web_server_update(sensor_data, processed_data, stable_pred_label,
                          stable_pred_conf);
        gui_web_set_connected(web_server_num_clients() > 0);
    }

        if (!lock_active && now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_set_gesture(gesture_text);
            gui_set_predict_confidence(processed_data.prediction_confidence);
        }
        break;

    case MODE_DATA_COLLECT:
        // Serial streaming handled by Core 0 callback (stream_serial_cb)
        break;

    case MODE_SETTINGS:
        // Idle — GUI handles slider interaction via LVGL
        break;

    case MODE_TEST:
        // Continuously feed processed sensor data to test screen
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_test_update(sensor_data, processed_data);
        }
        break;

    case MODE_MENU:
    default:
        break;
    }

    // ── Battery & charging indicator (all modes) ────────────────────
    // Immediate update on USB plug / unplug (edge detected by background task)
    if (power_usb_state_changed()) {
        bool charging = power_is_charging() || power_usb_connected();
        gui_set_charging(charging);
        gui_update_charge_icon(power_charging_status_str());
        gui_set_battery(power_battery_percent());
        // Show full-screen charging popup for 5 seconds
        gui_show_charge_popup(charging, power_battery_percent());
    }
    // Periodic refresh for normal battery % drift
    if (now - last_bat_read >= BATTERY_READ_INTERVAL_MS) {
        last_bat_read = now;
        gui_set_battery(power_battery_percent());
        gui_set_charging(power_is_charging() || power_usb_connected());
        gui_update_charge_icon(power_charging_status_str());
    }

    // ── CPU usage tracking ─────────────────────────────────────────
    uint32_t loop_end = micros();
    uint32_t elapsed  = loop_end - loop_start;
    cpu_busy_us  += elapsed;
    if (last_loop_us > 0)
        cpu_window_us += (loop_end - last_loop_us);
    last_loop_us = loop_end;

    if (now - last_cpu_upd >= 1000) {   // update every 1 s
        last_cpu_upd = now;
        if (cpu_window_us > 0)
            cpu_pct = (int)((uint64_t)cpu_busy_us * 100 / cpu_window_us);
        cpu_busy_us  = 0;
        cpu_window_us = 0;
        gui_set_cpu_usage(cpu_pct);

        // Update system info & About panel
        uint16_t lvgl_fps = 1000 / LV_DISP_DEF_REFR_PERIOD;  // approximate
        sysinfo_update((uint8_t)cpu_pct, lvgl_fps);
        gui_update_about(sysinfo_get());
    }

    // ── Yield to FreeRTOS — let background tasks (sensors, WiFi) breathe ──
    delay(1);
}
