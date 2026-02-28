/*
 * @file Test_Sound.cpp
 * @brief Comprehensive audio test suite — utilises test_sound_module
 *        for all test functions. Delegates to: musical scale, sweeps,
 *        beep patterns, freq steps, volume fade, melody, extreme
 *        frequencies, rapid jumps, and MP3 playback from LittleFS /audios.
 */
#include <Arduino.h>
#include "audio.h"
#include "config.h"
#include "test_sound_module.h"

#define TEST_VOLUME 0.9f

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   Comprehensive I2S Audio Test Suite   ║");
    Serial.println("║         MAX98357A Amplifier            ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    Serial.printf("  I2S_BCLK = %d\n", I2S_BCLK);
    Serial.printf("  I2S_LRCK = %d\n", I2S_LRCK);
    Serial.printf("  I2S_DOUT = %d\n", I2S_DOUT);
    Serial.println();

    audio_init();
    audio_set_volume(TEST_VOLUME);
    delay(500);
}

void loop() {
    // Run all tests via the test module (blocking, ~30 s total)
    test_sound_run_all();

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║      All Tests Complete!               ║");
    Serial.println("║      Restarting in 5 seconds...        ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    audio_set_volume(TEST_VOLUME);  // reset volume for next cycle
    delay(5000);
}
