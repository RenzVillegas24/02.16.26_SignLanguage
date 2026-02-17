/*
 * @file main.cpp
 * @brief Entry point — Sign Language Translator Glove v4.0
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
#include "config.h"
#include "display.h"
#include "gui.h"
#include "sensors.h"
#include "audio.h"
#include "power.h"
#include "web_server.h"

// ════════════════════════════════════════════════════════════════════
//  State
// ════════════════════════════════════════════════════════════════════
static AppMode   current_mode  = MODE_MENU;
static SensorData sensor_data  = {};
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
//  Settings / test callbacks (wired from GUI)
// ════════════════════════════════════════════════════════════════════
static void on_brightness_change(uint8_t val) {
    display_set_brightness(val);
    Serial.printf("[MAIN] Brightness → %d\n", val);
}

static void on_volume_change(uint8_t val) {
    // Volume is stored in gui state; just log for now
    Serial.printf("[MAIN] Volume → %d\n", val);
}

static void on_test_speaker() {
    audio_play_tone(1000, 500);   // 1 kHz, 500 ms beep
    Serial.println("[MAIN] Speaker test tone");
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
    // Format: flex0,flex1,..,flex4,hall0,..,hall4,ax,ay,az,gx,gy,gz
    Serial.printf("%u,%u,%u,%u,%u,"
                  "%u,%u,%u,%u,%u,"
                  "%.2f,%.2f,%.2f,"
                  "%.2f,%.2f,%.2f\n",
        sensor_data.flex[0], sensor_data.flex[1], sensor_data.flex[2],
        sensor_data.flex[3], sensor_data.flex[4],
        sensor_data.hall[0], sensor_data.hall[1], sensor_data.hall[2],
        sensor_data.hall[3], sensor_data.hall[4],
        sensor_data.accel_x, sensor_data.accel_y, sensor_data.accel_z,
        sensor_data.gyro_x,  sensor_data.gyro_y,  sensor_data.gyro_z);
}

// ════════════════════════════════════════════════════════════════════
//  Simple gesture placeholder
//  (replace with Edge Impulse classifier or rule-based logic later)
// ════════════════════════════════════════════════════════════════════
static void classify_gesture() {
    // Placeholder: fist detection (all flex sensors above threshold)
    bool all_bent = true;
    for (int i = 0; i < 5; i++) {
        if (sensor_data.flex[i] < 2500) { all_bent = false; break; }
    }
    if (all_bent) {
        strncpy(gesture_text, "FIST", sizeof(gesture_text));
    }
    // Open hand (all flex sensors low)
    else {
        bool all_open = true;
        for (int i = 0; i < 5; i++) {
            if (sensor_data.flex[i] > 1500) { all_open = false; break; }
        }
        if (all_open) {
            strncpy(gesture_text, "OPEN HAND", sizeof(gesture_text));
        } else {
            strncpy(gesture_text, "---", sizeof(gesture_text));
        }
    }
}

// ════════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(EI_SERIAL_BAUD);
    delay(200);
    Serial.println("\n=== Sign Language Glove v4.0 ===");

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
    Serial.println("[MAIN] GUI ready");

    // 3. Sensors
    sensors_init();
    Serial.println("[MAIN] Sensors ready");

    // 4. Audio
    audio_init();
    Serial.println("[MAIN] Audio ready");

    // 5. Power
    power_init();
    Serial.println("[MAIN] Power ready");

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

    // ── Power management ───────────────────────────────────────────
    power_update();

    if (power_button_pressed()) {
        // Short press → deep sleep
        audio_stop();
        if (web_server_is_running()) web_server_stop();
        display_off();
        power_deep_sleep();
    }

    // ── Read sensors ───────────────────────────────────────────────
    if (now - last_sensor >= SENSOR_READ_INTERVAL_MS) {
        last_sensor = now;
        sensors_read(sensor_data);
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

    case MODE_PREDICT_WORDS:
        classify_gesture();
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_update(sensor_data);
            gui_set_gesture(gesture_text);
        }
        break;

    case MODE_PREDICT_SPEECH:
        classify_gesture();
        // Play tone for detected gesture (simple demo)
        if (!audio_is_playing()) {
            if (strcmp(gesture_text, "FIST") == 0)
                audio_play_tone(440, 300);
            else if (strcmp(gesture_text, "OPEN HAND") == 0)
                audio_play_tone(880, 300);
        }
        break;

    case MODE_PREDICT_BOTH:
        classify_gesture();
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_update(sensor_data);
            gui_set_gesture(gesture_text);
        }
        if (!audio_is_playing()) {
            if (strcmp(gesture_text, "FIST") == 0)
                audio_play_tone(440, 300);
            else if (strcmp(gesture_text, "OPEN HAND") == 0)
                audio_play_tone(880, 300);
        }
        break;

    case MODE_PREDICT_WEB:
        classify_gesture();
        web_server_update(sensor_data, gesture_text);
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_set_gesture(gesture_text);
        }
        break;

    case MODE_SETTINGS:
        // Idle — GUI handles slider interaction via LVGL
        break;

    case MODE_TEST:
        // Continuously feed sensor data to test screen
        if (now - last_display >= DISPLAY_UPDATE_INTERVAL_MS) {
            last_display = now;
            gui_test_update(sensor_data);
        }
        break;

    case MODE_MENU:
    default:
        break;
    }

    // ── Battery indicator (all modes) ──────────────────────────────
    if (now - last_bat_read >= BATTERY_READ_INTERVAL_MS) {
        last_bat_read = now;
        gui_set_battery(power_battery_percent());
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
    }
}
