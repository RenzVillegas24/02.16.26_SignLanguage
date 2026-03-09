/*
 * @file audio.h
 * @brief I2S audio output via MAX98357A (tone generation + MP3 playback)
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
//  Playback poll hook
//  Called inside every write loop chunk.
//  Return true  → abort current audio immediately (stop/interrupt).
//  Return false → continue (or block internally while paused).
//  Set to nullptr to disable.
// ─────────────────────────────────────────────
typedef bool (*audio_poll_fn)();
void audio_set_poll(audio_poll_fn fn);   // register / clear the poll hook

// ─────────────────────────────────────────────
//  MP3 File Playback  (LittleFS, via minimp3)
//  No external library required — uses the
//  public-domain minimp3 single-header decoder.
// ─────────────────────────────────────────────
bool audio_play_mp3(const char* filepath);          // play a single .mp3 file
void audio_play_mp3_dir(const char* dirpath);       // play all .mp3 files in a dir
bool audio_mp3_exists(const char* filepath);         // check if .mp3 file exists on LittleFS
