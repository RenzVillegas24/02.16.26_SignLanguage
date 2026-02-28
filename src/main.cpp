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

// CPU usage tracking
static uint32_t  cpu_busy_us   = 0;     // accumulated busy time (µs)
static uint32_t  cpu_window_us = 0;     // accumulated total time (µs)
static uint32_t  last_loop_us  = 0;     // micros() at start of previous loop
static uint32_t  last_cpu_upd  = 0;     // millis() of last CPU% push
static int       cpu_pct       = 0;     // smoothed CPU %

// ════════════════════════════════════════════════════════════════════
//  GUI → main mode-change callback
// ════════════════════════════════════════════════════════════════════
static void on_mode_change(AppMode m) {
    AppMode prev = current_mode;
    current_mode = m;

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
        audio_stop();
        power_light_sleep();
        // Execution resumes here after wakeup from light sleep
        Serial.println("[MAIN] Resumed from light sleep");
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
    // Format: flex0,..,flex4,hall0,..,hall4,hall_top0,..,hall_top4,ax,ay,az,gx,gy,gz,pitch,roll
    // 23 features: 5 flex + 5 hall + 5 hall_top + 3 accel + 3 gyro + pitch + roll
    Serial.printf("%u,%u,%u,%u,%u,"
                  "%u,%u,%u,%u,%u,"
                  "%u,%u,%u,%u,%u,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f\n",
        sensor_data.flex[0], sensor_data.flex[1], sensor_data.flex[2],
        sensor_data.flex[3], sensor_data.flex[4],
        sensor_data.hall[0], sensor_data.hall[1], sensor_data.hall[2],
        sensor_data.hall[3], sensor_data.hall[4],
        sensor_data.hall_top[0], sensor_data.hall_top[1], sensor_data.hall_top[2],
        sensor_data.hall_top[3], sensor_data.hall_top[4],
        sensor_data.accel_x, sensor_data.accel_y, sensor_data.accel_z,
        sensor_data.gyro_x,  sensor_data.gyro_y,  sensor_data.gyro_z,
        sensor_data.pitch,   sensor_data.roll);
}

// ════════════════════════════════════════════════════════════════════
//  Gesture classification via sensor_module
//  (uses sensor_module_predict — currently rule-based, future EI)
// ════════════════════════════════════════════════════════════════════
static void classify_gesture() {
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
        gui_show_power_menu();
    }

    // Short press while power menu is open → dismiss it
    if (power_button_pressed() && gui_power_menu_visible()) {
        gui_hide_power_menu();
    }

    // Short press while power menu is NOT open → immediate light sleep
    if (power_button_pressed() && !gui_power_menu_visible()) {
        pending_power_action = PWR_SLEEP;
    }

    // ── Read sensors ───────────────────────────────────────────────
    if (now - last_sensor >= SENSOR_READ_INTERVAL_MS) {
        last_sensor = now;
        sensors_read(sensor_data);
        sensor_module_process(sensor_data, processed_data);
        power_reset_idle_timer();
    }

    // ── Mode-specific logic ────────────────────────────────────────
    switch (current_mode) {

    case MODE_TRAIN:
        // Stream raw data over Serial for Edge Impulse
        if (now - last_train_tx >= TRAIN_SERIAL_INTERVAL_MS) {
            last_train_tx = now;
            train_serial_output();
        }
        break;

    case MODE_PREDICT_LOCAL:
        classify_gesture();
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            if (gui_local_show_sensors())
                gui_update(sensor_data);
            if (gui_local_show_words())
                gui_set_gesture(gesture_text);
        }
        if (gui_local_use_speech() && !audio_is_playing()) {
            if (strcmp(gesture_text, "FIST") == 0)
                audio_play_tone(440, 300);
            else if (strcmp(gesture_text, "OPEN HAND") == 0)
                audio_play_tone(880, 300);
        }
        break;

    case MODE_PREDICT_WEB:
        classify_gesture();
        web_server_update(sensor_data, gesture_text);
        gui_web_set_connected(web_server_num_clients() > 0);
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_set_gesture(gesture_text);
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
        gui_set_charging(power_is_charging() || power_usb_connected());
        gui_set_battery(power_battery_percent());
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
}
