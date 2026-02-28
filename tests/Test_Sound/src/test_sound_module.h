/*
 * @file test_sound_module.h
 * @brief Callable audio test functions for the SignGlove environment.
 *        Wraps the comprehensive test suite from Test_Sound.cpp
 *        so individual tests can be triggered from the GUI.
 *
 * NOTE: Most functions are BLOCKING (they play audio synchronously).
 *       Call them from a context where that is acceptable.
 */
#pragma once

#include <Arduino.h>

/// Run all audio tests in sequence (blocking, ~30 s)
void test_sound_run_all();

/// Individual tests (blocking)
void test_sound_musical_scale();
void test_sound_frequency_sweep();
void test_sound_alarm_patterns();
void test_sound_frequency_steps();
void test_sound_volume_fade();
void test_sound_melody();
void test_sound_extreme_frequencies();
void test_sound_rapid_jumps();
void test_sound_wav_playback();
void test_sound_mp3_playback();

/// Quick speaker verification — plays a 1 kHz beep (blocking, ~0.5 s)
void test_sound_quick_beep();

