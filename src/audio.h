/*
 * @file audio.h
 * @brief I2S audio playback via MAX98357A
 */
#pragma once

#include <Arduino.h>

void audio_init();
void audio_play_tone(uint16_t freq_hz, uint16_t duration_ms);
void audio_stop();
bool audio_is_playing();
