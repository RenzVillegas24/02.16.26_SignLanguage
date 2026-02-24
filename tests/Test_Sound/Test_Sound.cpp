/*
 * @file Test_Sound.cpp
 * @brief Comprehensive audio test suite — calls audio.h / audio.cpp only.
 *        Tests: scale, sweeps, beep patterns, freq steps, volume fade, melody,
 *               extreme frequencies, rapid jumps, WAV playback from SPIFFS /audios
 */
#include <Arduino.h>
#include "audio.h"
#include "config.h"

#define TEST_VOLUME 0.9f    // default volume for tone tests

// ══════════════════════════════════════════════════════════════
//  Test Sequences
// ══════════════════════════════════════════════════════════════

// ── TEST 1: Musical Scale ────────────────────────────────────
void test_musical_scale() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 1: Musical Scale (C4→C5)");
    Serial.println("══════════════════════════════════════\n");

    const int    notes[] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4,
                             NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5};
    const char* names[] = {"C4","D4","E4","F4","G4","A4","B4","C5"};

    for (int i = 0; i < 8; i++) {
        Serial.printf("  %s  (%d Hz)\n", names[i], notes[i]);
        audio_play_tone(notes[i], 400);
        delay(100);
    }
}

// ── TEST 2: Frequency Sweeps ─────────────────────────────────
void test_frequency_sweep() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 2: Frequency Sweeps (Chirps)");
    Serial.println("══════════════════════════════════════\n");

    Serial.println("  Ascending:  200 Hz → 2000 Hz  (3 s)");
    audio_play_chirp(200, 2000, 3000);
    delay(500);

    Serial.println("  Descending: 2000 Hz → 200 Hz  (3 s)");
    audio_play_chirp(2000, 200, 3000);
    delay(500);
}

// ── TEST 3: Alarm / Beep Patterns ────────────────────────────
void test_alarm_patterns() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 3: Alarm / Beep Patterns");
    Serial.println("══════════════════════════════════════\n");

    Serial.println("  Fast beeps  — 5 × 100 ms  @ 1000 Hz");
    audio_play_beeps(1000, 100, 100, 5);
    delay(500);

    Serial.println("  Dual-tone siren  — 5 ×");
    for (int i = 0; i < 5; i++) {
        audio_play_tone(800, 300);
        audio_play_tone(600, 300);
    }
    delay(500);
}

// ── TEST 4: Frequency Steps ──────────────────────────────────
void test_frequency_steps() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 4: Frequency Steps (Low→High)");
    Serial.println("══════════════════════════════════════\n");

    const int freqs[] = {100, 200, 400, 800, 1600, 3200};
    for (int i = 0; i < 6; i++) {
        Serial.printf("  %4d Hz\n", freqs[i]);
        audio_play_tone(freqs[i], 500);
        delay(200);
    }
}

// ── TEST 5: Volume Fade ──────────────────────────────────────
void test_volume_fade() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 5: Volume Fade  (A4 @ 440 Hz)");
    Serial.println("══════════════════════════════════════\n");

    Serial.println("  Fade in  (5 % → 100 %)");
    for (float v = 0.05f; v <= 1.0; v += 0.05f) {
        Serial.printf("  vol = %.0f%%\n", v * 100);
        audio_set_volume(v);
        audio_play_tone(NOTE_A4, 200);
    }
    delay(200);

    Serial.println("  Fade out  (100 % → 5 %)");
    for (float v = 1.0f; v >= 0.05f; v -= 0.05f) {
        Serial.printf("  vol = %.0f%%\n", v * 100);
        audio_set_volume(v);
        audio_play_tone(NOTE_A4, 200);
    }

    // restore test volume
    audio_set_volume(TEST_VOLUME);
}

// ── TEST 6: Simple Melody ────────────────────────────────────
void test_melody() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 6: Simple Melody");
    Serial.println("══════════════════════════════════════\n");

    const int melody[]    = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5,
                              NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5,
                              NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4};
    const int durations[] = {300, 300, 300, 600,
                              300, 300, 300, 600,
                              300, 300, 300, 300, 600};

    for (int i = 0; i < 13; i++) {
        audio_play_tone(melody[i], durations[i]);
        delay(50);
    }
}

// ── TEST 7: Extreme Frequencies ──────────────────────────────
void test_extreme_frequencies() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 7: Extreme Frequencies");
    Serial.println("══════════════════════════════════════\n");

    Serial.println("  Ultra-low : 50 Hz  (2 s)");
    audio_play_tone(50, 2000);
    delay(500);

    Serial.println("  High      : 4000 Hz  (2 s)");
    audio_play_tone(4000, 2000);
    delay(500);
}

// ── TEST 8: Rapid Frequency Jumps ────────────────────────────
void test_rapid_jumps() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 8: Rapid Frequency Jumps");
    Serial.println("══════════════════════════════════════\n");

    const int freqs[] = {200, 800, 300, 1200, 400, 1600, 500, 2000};
    for (int i = 0; i < 8; i++) {
        Serial.printf("  %4d Hz\n", freqs[i]);
        audio_play_tone(freqs[i], 150);
        delay(50);
    }
}

// ── TEST 9: WAV Playback ─────────────────────────────────────
void test_wav_playback() {
    Serial.println("\n══════════════════════════════════════");
    Serial.println("  TEST 9: WAV File Playback");
    Serial.println("          SPIFFS directory: /audios");
    Serial.println("══════════════════════════════════════\n");

    audio_play_wav_dir("/audios");
}

// ══════════════════════════════════════════════════════════════
//  Arduino Entry Points
// ══════════════════════════════════════════════════════════════
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
    test_musical_scale();       delay(1000);
    test_frequency_sweep();     delay(1000);
    test_alarm_patterns();      delay(1000);
    test_frequency_steps();     delay(1000);
    test_volume_fade();         delay(1000);
    test_melody();              delay(1000);
    test_extreme_frequencies(); delay(1000);
    test_rapid_jumps();         delay(1000);
    test_wav_playback();        delay(1000);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║      All Tests Complete!               ║");
    Serial.println("║      Restarting in 5 seconds...        ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    audio_set_volume(TEST_VOLUME);  // reset volume for next cycle
    delay(5000);
}
