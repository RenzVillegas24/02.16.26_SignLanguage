/*
 * @file audio.h
 * @brief I2S audio output via MAX98357A (tone generation + WAV playback)
 *
 * Pin config (from config.h):
 *   I2S_BCLK = 40 | I2S_LRCK = 41 | I2S_DOUT = 42
 */
#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────
//  Musical Note Frequencies (Hz)
// ─────────────────────────────────────────────
#define NOTE_C3   131
#define NOTE_D3   147
#define NOTE_E3   165
#define NOTE_F3   175
#define NOTE_G3   196
#define NOTE_A3   220
#define NOTE_B3   247
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_G5   784
#define NOTE_A5   880

// ─────────────────────────────────────────────
//  Core
// ─────────────────────────────────────────────
void audio_init();
void audio_stop();
bool audio_is_playing();

// ─────────────────────────────────────────────
//  Volume  (0.0 = silent … 1.0 = full)
//  Internally mapped to dB gain (–64 … 0 dB)
//  like ESP-ADF's i2s_alc_volume_set()
// ─────────────────────────────────────────────
void  audio_set_volume(float volume);   // clamps to [0.0, 1.0]
float audio_get_volume();

// ─────────────────────────────────────────────
//  Tone Generation
// ─────────────────────────────────────────────
void audio_play_tone(uint16_t freq_hz, uint16_t duration_ms);
void audio_play_chirp(uint16_t start_hz, uint16_t end_hz, uint16_t duration_ms);
void audio_play_beeps(uint16_t freq_hz, uint16_t beep_ms, uint16_t pause_ms, uint8_t count);

// ─────────────────────────────────────────────
//  WAV File Playback  (SPIFFS)
// ─────────────────────────────────────────────
bool audio_play_wav(const char* filepath);          // play a single .wav file
void audio_play_wav_dir(const char* dirpath);       // play all .wav files in a dir
