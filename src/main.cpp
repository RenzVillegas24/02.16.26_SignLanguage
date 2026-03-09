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
static uint32_t  last_train_tx = 0;
static uint32_t  last_bat_read = 0;
static uint32_t  train_sample_count = 0;

// EI inference timing
static uint32_t  last_ei_push = 0;
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

// ════════════════════════════════════════════════════════════════════
//  GUI → main mode-change callback
// ════════════════════════════════════════════════════════════════════
static void on_mode_change(AppMode m) {
    AppMode prev = current_mode;
    current_mode = m;

    // Activate/deactivate background sensor reading based on mode.
    // Menu & Settings don't need live sensor data.
    bool need_sensors = (m == MODE_TRAIN || m == MODE_PREDICT_LOCAL
                      || m == MODE_PREDICT_WEB || m == MODE_TEST);
    sensors_set_active(need_sensors);

    // Start / stop web server as needed
    if (m == MODE_PREDICT_WEB && !web_server_is_running()) {
        web_server_start();
        gui_show_web_qr(web_server_get_url().c_str());
    }
    if (prev == MODE_PREDICT_WEB && m != MODE_PREDICT_WEB) {
        web_server_stop();
    }

    Serial.printf("[MAIN] Mode → %d\n", (int)m);
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

// ════════════════════════════════════════════════════════════════════
//  Edge Impulse serial streaming (TRAIN mode)
// ════════════════════════════════════════════════════════════════════
static void train_serial_output() {
    // Format: flex0,..,flex4,hall0,..,hall4,ax,ay,az,gx,gy,gz,pitch,roll
    // 18 features: 5 flex + 5 hall + 3 accel + 3 gyro + pitch + roll
    Serial.printf("%u,%u,%u,%u,%u,"
                  "%u,%u,%u,%u,%u,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f\n",
        sensor_data.flex[0], sensor_data.flex[1], sensor_data.flex[2],
        sensor_data.flex[3], sensor_data.flex[4],
        sensor_data.hall[0], sensor_data.hall[1], sensor_data.hall[2],
        sensor_data.hall[3], sensor_data.hall[4],
        sensor_data.accel_x, sensor_data.accel_y, sensor_data.accel_z,
        sensor_data.gyro_x,  sensor_data.gyro_y,  sensor_data.gyro_z,
        sensor_data.pitch,   sensor_data.roll);
}

// ════════════════════════════════════════════════════════════════════
//  Gesture classification via Edge Impulse model
// ════════════════════════════════════════════════════════════════════
static void classify_gesture() {
    if (!sensor_module_ei_ready()) return;
    // Don't overwrite the displayed label while audio is speaking it —
    // the EI window keeps sliding and would switch gesture_text mid-playback,
    // either triggering a double-play or skipping the current word entirely.
    if (audio_is_playing()) return;
    const char *label = sensor_module_predict(processed_data);
    strncpy(gesture_text, label, sizeof(gesture_text));
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
    // Drain the initial USB state-change flag produced by the first SY6970 read.
    // Without this, every cold boot / deep-sleep wakeup would trigger the popup
    // because the background task transitions usb_connected from false → actual.
    // We wait just long enough for the first read to complete (~500 ms task period)
    // then discard the flag so only real live plug/unplug events show the popup.
    delay(600);
    power_usb_state_changed();   // silently drain boot-time edge

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

        // Push raw data into EI sliding window (~55 ms interval matches training)
        if (current_mode == MODE_PREDICT_LOCAL || current_mode == MODE_PREDICT_WEB) {
            if (now - last_ei_push >= 55) {  // EI_CLASSIFIER_INTERVAL_MS ≈ 55.56
                last_ei_push = now;
                sensor_module_ei_push(sensor_data);
            }
            // Classify gesture when buffer is ready
            classify_gesture();
        }
    }

    // ── Mode-specific logic ────────────────────────────────────────
    switch (current_mode) {

    case MODE_TRAIN:
        // Stream raw data over Serial for Edge Impulse
        if (now - last_train_tx >= TRAIN_SERIAL_INTERVAL_MS) {
            last_train_tx = now;
            train_serial_output();
            train_sample_count++;
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
            if (strcmp(gesture_text, "---") != 0 &&
                strcmp(gesture_text, "ERROR") != 0 &&
                strcmp(gesture_text, last_spoken_label) != 0) {
                // Build the audio file path: /<voice>/<label>.mp3
                char audio_path[64];
                snprintf(audio_path, sizeof(audio_path), "/%s/%s.mp3",
                         gui_local_voice_dir(), gesture_text);

                // Only attempt playback if the file actually exists on LittleFS
                if (audio_mp3_exists(audio_path)) {
                    audio_play_mp3(audio_path);
                    Serial.printf("[MAIN] Playing audio: %s\n", audio_path);
                } else {
                    Serial.printf("[MAIN] No audio file for label '%s' (%s)\n",
                                  gesture_text, audio_path);
                }
                // Lock the label either way to prevent spamming
                strncpy(last_spoken_label, gesture_text, sizeof(last_spoken_label));
            }
            // Only reset the spoken-label guard once audio has finished and
            // the gesture has gone back to idle — avoids immediate re-trigger.
            if (strcmp(gesture_text, "---") == 0 && !audio_is_playing()) {
                last_spoken_label[0] = '\0';
            }
        }
        break;

    case MODE_PREDICT_WEB:
        if (!lock_active) {
            web_server_update(sensor_data, gesture_text);
            gui_web_set_connected(web_server_num_clients() > 0);
        }
        if (!lock_active && now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_set_gesture(gesture_text);
            gui_set_predict_confidence(processed_data.prediction_confidence);
        }
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
        gui_set_battery(power_battery_percent());
        // Show full-screen charging popup for 5 seconds
        gui_show_charge_popup(charging, power_battery_percent());
    }
    // Periodic refresh for normal battery % drift
    if (now - last_bat_read >= BATTERY_READ_INTERVAL_MS) {
        last_bat_read = now;
        gui_set_battery(power_battery_percent());
        gui_set_charging(power_is_charging() || power_usb_connected());
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
