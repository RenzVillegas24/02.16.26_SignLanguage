/*
 * @file test_sound_module.cpp
 * @brief Audio test functions — callable from SignGlove GUI.
 *        Mirrors Test_Sound.cpp but as library functions.
 */
#include "test_sound_module.h"
#include "audio.h"

#define TEST_VOLUME 0.9f

// ── Musical Scale ────────────────────────────
void test_sound_musical_scale() {
    Serial.println("\n══ Musical Scale (C4→C5) ══");
    const int notes[] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4,
                         NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5};
    for (int i = 0; i < 8; i++) {
        audio_play_tone(notes[i], 400);
        delay(100);
    }
}

// ── Frequency Sweeps ─────────────────────────
void test_sound_frequency_sweep() {
    Serial.println("\n══ Frequency Sweeps ══");
    Serial.println("  Ascending 200→2000 Hz");
    audio_play_chirp(200, 2000, 3000);
    delay(500);
    Serial.println("  Descending 2000→200 Hz");
    audio_play_chirp(2000, 200, 3000);
    delay(500);
}

// ── Alarm Patterns ───────────────────────────
void test_sound_alarm_patterns() {
    Serial.println("\n══ Alarm Patterns ══");
    Serial.println("  Fast beeps × 5");
    audio_play_beeps(1000, 100, 100, 5);
    delay(500);
    Serial.println("  Dual-tone siren × 5");
    for (int i = 0; i < 5; i++) {
        audio_play_tone(800, 300);
        audio_play_tone(600, 300);
    }
    delay(500);
}

// ── Frequency Steps ──────────────────────────
void test_sound_frequency_steps() {
    Serial.println("\n══ Frequency Steps ══");
    const int freqs[] = {100, 200, 400, 800, 1600, 3200};
    for (int i = 0; i < 6; i++) {
        Serial.printf("  %d Hz\n", freqs[i]);
        audio_play_tone(freqs[i], 500);
        delay(200);
    }
}

// ── Volume Fade ──────────────────────────────
void test_sound_volume_fade() {
    Serial.println("\n══ Volume Fade ══");
    float restore_vol = audio_get_volume();   // remember caller's volume
    Serial.println("  Fade in");
    for (float v = 0.05f; v <= 1.0f; v += 0.05f) {
        audio_set_volume(v);
        audio_play_tone(NOTE_A4, 200);
    }
    delay(200);
    Serial.println("  Fade out");
    for (float v = 1.0f; v >= 0.05f; v -= 0.05f) {
        audio_set_volume(v);
        audio_play_tone(NOTE_A4, 200);
    }
    audio_set_volume(restore_vol);   // restore to whatever the caller had set
}

// ── Melody ───────────────────────────────────
void test_sound_melody() {
    Serial.println("\n══ Simple Melody ══");
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

// ── Extreme Frequencies ──────────────────────
void test_sound_extreme_frequencies() {
    Serial.println("\n══ Extreme Frequencies ══");
    Serial.println("  50 Hz  (2 s)");
    audio_play_tone(50, 2000);
    delay(500);
    Serial.println("  4000 Hz  (2 s)");
    audio_play_tone(4000, 2000);
    delay(500);
}

// ── Rapid Jumps ──────────────────────────────
void test_sound_rapid_jumps() {
    Serial.println("\n══ Rapid Frequency Jumps ══");
    const int freqs[] = {200, 800, 300, 1200, 400, 1600, 500, 2000};
    for (int i = 0; i < 8; i++) {
        audio_play_tone(freqs[i], 150);
        delay(50);
    }
}

// ── MP3 Playback ─────────────────────────────
void test_sound_mp3_playback() {
    Serial.println("\n══ MP3 Playback ══");
    audio_play_mp3_dir("/");
}

// ── Quick beep ───────────────────────────────
void test_sound_quick_beep() {
    audio_play_tone(1000, 500);
}

// ── Run all ──────────────────────────────────
void test_sound_run_all() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   Comprehensive Audio Test Suite       ║");
    Serial.println("╚════════════════════════════════════════╝");

    float saved_vol = audio_get_volume();
    audio_set_volume(TEST_VOLUME);

    test_sound_musical_scale();       delay(1000);
    test_sound_frequency_sweep();     delay(1000);
    test_sound_alarm_patterns();      delay(1000);
    test_sound_frequency_steps();     delay(1000);
    test_sound_volume_fade();         delay(1000);
    test_sound_melody();              delay(1000);
    test_sound_extreme_frequencies(); delay(1000);
    test_sound_rapid_jumps();         delay(1000);
    test_sound_mp3_playback();        delay(1000);

    audio_set_volume(saved_vol);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║      All Audio Tests Complete!         ║");
    Serial.println("╚════════════════════════════════════════╝");
}
