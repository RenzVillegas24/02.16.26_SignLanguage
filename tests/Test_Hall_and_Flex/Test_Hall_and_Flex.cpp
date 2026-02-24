/*
 * @file Test_Hall_and_Flex.cpp
 * @brief Test flex sensors and hall-effect sensors via CD74HC4067 multiplexer
 *        Continuously reads and prints all sensor values over Serial.
 *
 * Pin config (from config.h):
 *   MUX_S0  = 21    MUX_S1  = 47    MUX_S2  = 48    MUX_S3  = 45
 *   MUX_SIG = 1     (ADC1_CH0)
 *
 * Mux channels:
 *   CH 0 = Flex Thumb      CH 5 = Hall Thumb
 *   CH 1 = Flex Index      CH 6 = Hall Index
 *   CH 2 = Flex Middle     CH 7 = Hall Middle
 *   CH 3 = Flex Ring       CH 8 = Hall Ring
 *   CH 4 = Flex Pinky      CH 9 = Hall Pinky
 */
#include <Arduino.h>
#include "config.h"

// ── Finger labels ────────────────────────────
static const char* finger_names[] = {
    "Thumb", "Index", "Middle", "Ring", "Pinky"
};

// ── Local mux helpers (standalone, no dependency on sensors.cpp) ──
static void mux_init() {
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
    analogReadResolution(12);  // 0-4095
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

// ── Bar graph helper ─────────────────────────
static void print_bar(uint16_t value, uint16_t max_val = 4095, uint8_t width = 20) {
    uint8_t filled = (uint8_t)((uint32_t)value * width / max_val);
    Serial.print('[');
    for (uint8_t i = 0; i < width; i++) {
        Serial.print(i < filled ? '#' : ' ');
    }
    Serial.print(']');
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("========================================");
    Serial.println("  Test_Hall_and_Flex - Sensor Test      ");
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
        Serial.printf("  CH %d = Flex %-6s    CH %d = Hall %s\n",
                      MUX_CH_FLEX_THUMB + i, finger_names[i],
                      MUX_CH_HALL_THUMB + i, finger_names[i]);
    }
    Serial.println();

    mux_init();
    Serial.println("[TEST] Multiplexer initialised. Reading sensors...\n");
}

void loop() {
    uint16_t flex[NUM_FLEX_SENSORS];
    uint16_t hall[NUM_HALL_SENSORS];

    // ── Read all sensors ──
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        flex[i] = mux_read(MUX_CH_FLEX_THUMB + i);
    }
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        hall[i] = mux_read(MUX_CH_HALL_THUMB + i);
    }

    // ── Print flex sensors ──
    Serial.println("─── Flex Sensors ───────────────────────");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("  %-6s (CH%d): %4d  ", finger_names[i], MUX_CH_FLEX_THUMB + i, flex[i]);
        print_bar(flex[i]);
        Serial.println();
    }

    // ── Print hall sensors ──
    Serial.println("─── Hall Sensors ───────────────────────");
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("  %-6s (CH%d): %4d  ", finger_names[i], MUX_CH_HALL_THUMB + i, hall[i]);
        print_bar(hall[i]);
        Serial.println();
    }

    // ── CSV-style compact line (for Serial Plotter) ──
    Serial.print("[CSV] ");
    for (int i = 0; i < NUM_FLEX_SENSORS; i++) {
        Serial.printf("F%d:%d", i, flex[i]);
        Serial.print(',');
    }
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("H%d:%d", i, hall[i]);
        if (i < NUM_HALL_SENSORS - 1) Serial.print(',');
    }
    Serial.println();

    Serial.println();
    delay(500);
}
