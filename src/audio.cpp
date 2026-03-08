/*
 * @file audio.cpp
 * @brief I2S audio output via MAX98357A amplifier
 *        - Volume control
 *        - Sine-wave tone generation  (with fade envelope)
 *        - Frequency chirp (linear sweep)
 *        - Beep patterns
 *        - MP3 file streaming from LittleFS
 */
#include "audio.h"
#include "config.h"
#include "driver/i2s.h"
#include "LittleFS.h"
#include <math.h>
#include <vector>

// FreeRTOS (included via Arduino.h on ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// minimp3 — single-header, public-domain (CC0) MP3 decoder
// https://github.com/lieff/minimp3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// ─────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     44100
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     1024
#define TONE_BUF_LEN    512     // samples per chunk (mono)

// Forward declaration for the blocking MP3 decode function
static bool audio_play_mp3_blocking(const char* filepath);

// ─────────────────────────────────────────────
//  Module State
// ─────────────────────────────────────────────
static bool          s_installed = false;
static volatile bool s_playing   = false;
static float         s_volume    = 0.5f;    // 0.0 – 1.0
static float         s_gain      = 0.0f;    // linear gain derived from dB (set in audio_init)
static audio_poll_fn s_poll_fn   = nullptr; // optional per-chunk interrupt/pause hook

// ─────────────────────────────────────────────
//  Background audio task
//  MP3 decoding is CPU + stack intensive (~8-12 KB).
//  Running it on loopTask overflows the 8 KB default stack.
//  Instead we post the file path to a queue and let a dedicated
//  FreeRTOS task (Core 0, 16 KB stack) handle all decoding.
// ─────────────────────────────────────────────
#define AUDIO_TASK_STACK   (48 * 1024)
#define AUDIO_PATH_MAX     128

static QueueHandle_t   s_audio_queue  = nullptr;
static TaskHandle_t    s_audio_task   = nullptr;

static void audio_task_fn(void *) {
    char path[AUDIO_PATH_MAX];
    for (;;) {
        // Block indefinitely until a path is posted
        if (xQueueReceive(s_audio_queue, path, portMAX_DELAY) == pdTRUE) {
            audio_play_mp3_blocking(path);
        }
    }
}

void audio_set_poll(audio_poll_fn fn) { s_poll_fn = fn; }

// ─────────────────────────────────────────────
//  dB-based volume → linear gain
//  Mimics ESP-ADF ALC: volume 0.0→1.0 maps to
//  –64 dB (silence) → 0 dB (full).
// ─────────────────────────────────────────────
#define ALC_DB_MIN  -64.0f
#define ALC_DB_MAX    0.0f

static float volume_to_gain(float vol) {
    if (vol <= 0.0f) return 0.0f;
    if (vol >= 1.0f) return 1.0f;
    float db = ALC_DB_MIN + (ALC_DB_MAX - ALC_DB_MIN) * vol;  // –64…0
    return powf(10.0f, db / 20.0f);   // dB → linear
}

// ─────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────
static void i2s_install(uint32_t sample_rate) {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = sample_rate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;   // mono
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = DMA_BUF_COUNT;
    cfg.dma_buf_len          = DMA_BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[AUDIO] I2S install failed: %d\n", err);
        return;
    }
    i2s_set_pin(I2S_PORT, &pins);
    i2s_zero_dma_buffer(I2S_PORT);
    s_installed = true;
}

// ─────────────────────────────────────────────
//  Core API
// ─────────────────────────────────────────────
void audio_init() {
    if (s_installed) return;
    i2s_install(SAMPLE_RATE);
    s_gain = volume_to_gain(s_volume);   // compute initial linear gain

    // Create background audio task (Core 0, 16 KB stack)
    // MP3 decoding uses ~8-12 KB of stack; running on loopTask caused overflow.
    if (!s_audio_queue) {
        s_audio_queue = xQueueCreate(1, AUDIO_PATH_MAX);
    }
    if (!s_audio_task && s_audio_queue) {
        xTaskCreatePinnedToCore(
            audio_task_fn,   // function
            "audio_mp3",     // name
            AUDIO_TASK_STACK,// stack bytes
            nullptr,         // param
            2,               // priority (higher than idle, lower than sensors)
            &s_audio_task,   // handle
            0                // Core 0 (loop runs on Core 1)
        );
    }
    Serial.println("[AUDIO] I2S ready");
}

void audio_stop() {
    s_playing = false;
    if (s_installed) i2s_zero_dma_buffer(I2S_PORT);
}

bool audio_is_playing() { return s_playing; }

// ─────────────────────────────────────────────
//  Volume (dB-based, like ESP-ADF ALC)
// ─────────────────────────────────────────────
void audio_set_volume(float volume) {
    s_volume = constrain(volume, 0.0f, 1.0f);
    s_gain   = volume_to_gain(s_volume);

    float db = ALC_DB_MIN + (ALC_DB_MAX - ALC_DB_MIN) * s_volume;
    Serial.printf("[AUDIO] Volume %.0f%%  (%.1f dB, gain %.4f)\n",
                  s_volume * 100.0f, db, s_gain);
}

float audio_get_volume() { return s_volume; }

// ─────────────────────────────────────────────
//  Tone Generation
// ─────────────────────────────────────────────
void audio_play_tone(uint16_t freq_hz, uint16_t duration_ms) {
    if (!s_installed) return;
    s_playing = true;

    const int32_t total_samples = (int32_t)SAMPLE_RATE * duration_ms / 1000;
    const int32_t fade_samples  = SAMPLE_RATE / 50;    // 20 ms fade
    int16_t buf[TONE_BUF_LEN];                         // mono buffer

    float phase     = 0.0f;
    float phase_inc = 2.0f * PI * freq_hz / SAMPLE_RATE;
    int32_t written = 0;

    while (written < total_samples && s_playing) {
        // Poll hook: returns true → abort; may also block internally while paused
        if (s_poll_fn && s_poll_fn()) break;

        int32_t chunk = min((int32_t)TONE_BUF_LEN, total_samples - written);

        for (int32_t i = 0; i < chunk; i++) {
            float env = 1.0f;
            int32_t pos = written + i;
            if (pos < fade_samples)
                env = (float)pos / fade_samples;
            else if (pos > total_samples - fade_samples)
                env = (float)(total_samples - pos) / fade_samples;

            int16_t sample = (int16_t)(sinf(phase) * 32767.0f * s_gain * env);
            buf[i] = sample;   // mono

            phase += phase_inc;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }

        size_t bytes_written;
        i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        written += chunk;
    }

    // Wait for DMA buffers to drain before clearing (prevents popping)
    uint32_t dma_samples = DMA_BUF_COUNT * DMA_BUF_LEN;
    uint32_t drain_ms = (dma_samples * 1000UL) / SAMPLE_RATE + 20;  // +20ms margin
    delay(drain_ms);

    i2s_zero_dma_buffer(I2S_PORT);
    s_playing = false;
}

void audio_play_chirp(uint16_t start_hz, uint16_t end_hz, uint16_t duration_ms) {
    if (!s_installed) return;
    s_playing = true;

    const int32_t total_samples = (int32_t)SAMPLE_RATE * duration_ms / 1000;
    int16_t buf[TONE_BUF_LEN];                         // mono buffer

    float phase    = 0.0f;
    int32_t written = 0;

    while (written < total_samples && s_playing) {
        // Poll hook: returns true → abort; may also block internally while paused
        if (s_poll_fn && s_poll_fn()) break;

        int32_t chunk = min((int32_t)TONE_BUF_LEN, total_samples - written);

        for (int32_t i = 0; i < chunk; i++) {
            float progress = (float)(written + i) / total_samples;
            float cur_freq = start_hz + (end_hz - start_hz) * progress;

            int16_t sample = (int16_t)(sinf(phase) * 32767.0f * s_gain);
            buf[i] = sample;   // mono

            phase += 2.0f * PI * cur_freq / SAMPLE_RATE;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }

        size_t bytes_written;
        i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        written += chunk;
    }

    // Wait for DMA buffers to drain before clearing (prevents popping)
    uint32_t dma_samples = DMA_BUF_COUNT * DMA_BUF_LEN;
    uint32_t drain_ms = (dma_samples * 1000UL) / SAMPLE_RATE + 20;  // +20ms margin
    delay(drain_ms);

    i2s_zero_dma_buffer(I2S_PORT);
    s_playing = false;
}

void audio_play_beeps(uint16_t freq_hz, uint16_t beep_ms, uint16_t pause_ms, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        audio_play_tone(freq_hz, beep_ms);
        if (i < count - 1) delay(pause_ms);
    }
}

// ─────────────────────────────────────────────
//  WAV File Playback (LittleFS) [DEPRECATED]
// ─────────────────────────────────────────────
bool audio_play_wav(const char* filepath) {
    if (!s_installed) return false;

    File file = LittleFS.open(filepath, "r");
    if (!file) {
        Serial.printf("[AUDIO] Cannot open: %s\n", filepath);
        return false;
    }

    // ── Parse WAV header (44 bytes, standard PCM) ──
    uint8_t hdr[44];
    if (file.read(hdr, 44) != 44) {
        Serial.println("[AUDIO] Invalid WAV header");
        file.close();
        return false;
    }

    uint16_t num_channels   = *(uint16_t*)(hdr + 22);
    uint32_t wav_rate       = *(uint32_t*)(hdr + 24);
    uint16_t bits_per_samp  = *(uint16_t*)(hdr + 34);
    uint32_t data_size      = *(uint32_t*)(hdr + 40);
    float    duration_sec   = (float)data_size / (wav_rate * num_channels * (bits_per_samp / 8));

    Serial.printf("[AUDIO] WAV  %s\n", filepath);
    Serial.printf("        %u Hz | %u ch | %u-bit | %.2f s\n",
                  wav_rate, num_channels, bits_per_samp, duration_sec);

    // ── Reconfigure I2S if sample rate differs ──
    if (wav_rate != SAMPLE_RATE) {
        i2s_driver_uninstall(I2S_PORT);
        s_installed = false;
        i2s_install(wav_rate);
    }

    // ── Stream audio data (with stereo→mono conversion & volume scaling) ──
    const size_t BUF = 2048;
    uint8_t* buffer = (uint8_t*)malloc(BUF);
    int16_t* mono_buf = (int16_t*)malloc(BUF);  // for stereo→mono conversion
    if (!buffer || !mono_buf) {
        Serial.println("[AUDIO] malloc failed");
        if (buffer) free(buffer);
        if (mono_buf) free(mono_buf);
        file.close();
        return false;
    }

    s_playing = true;
    uint32_t streamed   = 0;
    int      last_pct   = -1;
    unsigned long t0    = millis();

    while (streamed < data_size && s_playing) {
        // Poll hook: returns true → abort; may also block internally while paused
        if (s_poll_fn && s_poll_fn()) break;

        size_t to_read   = min((uint32_t)BUF, data_size - streamed);
        size_t bytes_got = file.read(buffer, to_read);
        if (bytes_got == 0) break;

        size_t out_bytes = 0;
        uint8_t* out_ptr = buffer;

        if (bits_per_samp == 16) {
            int16_t* samples = (int16_t*)buffer;
            size_t num_samples = bytes_got / 2;

            if (num_channels == 2) {
                // Stereo → Mono: average L+R channels, apply volume
                size_t frames = num_samples / 2;
                for (size_t f = 0; f < frames; f++) {
                    int32_t left  = samples[f * 2];
                    int32_t right = samples[f * 2 + 1];
                    int32_t mixed = (left + right) / 2;
                    mono_buf[f] = (int16_t)(mixed * s_gain);
                }
                out_ptr = (uint8_t*)mono_buf;
                out_bytes = frames * sizeof(int16_t);
            } else {
                // Mono: just apply volume
                for (size_t s = 0; s < num_samples; s++) {
                    samples[s] = (int16_t)(samples[s] * s_gain);
                }
                out_bytes = bytes_got;
            }
        } else if (bits_per_samp == 8) {
            // 8-bit unsigned PCM → 16-bit signed mono
            size_t num_8bit = bytes_got;
            size_t out_samples = (num_channels == 2) ? num_8bit / 2 : num_8bit;
            
            for (size_t i = 0; i < out_samples; i++) {
                int32_t val;
                if (num_channels == 2) {
                    int32_t left  = (int32_t)buffer[i * 2] - 128;
                    int32_t right = (int32_t)buffer[i * 2 + 1] - 128;
                    val = (left + right) / 2;
                } else {
                    val = (int32_t)buffer[i] - 128;
                }
                mono_buf[i] = (int16_t)(val * 256 * s_gain);  // scale 8→16 bit
            }
            out_ptr = (uint8_t*)mono_buf;
            out_bytes = out_samples * sizeof(int16_t);
        } else {
            // Unsupported bit depth, pass through
            out_bytes = bytes_got;
        }

        size_t bytes_written;
        i2s_write(I2S_PORT, out_ptr, out_bytes, &bytes_written, portMAX_DELAY);
        streamed += bytes_got;

        int pct = (int)(streamed * 100UL / data_size);
        if (pct != last_pct && pct % 10 == 0) {
            last_pct = pct;
            Serial.printf("        %3d%%  (%lus elapsed)\n", pct, (millis() - t0) / 1000UL);
        }
    }

    free(buffer);
    free(mono_buf);
    file.close();
    
    // Wait for DMA buffers to fully drain before stopping
    // DMA has DMA_BUF_COUNT buffers of DMA_BUF_LEN samples each
    // At wav_rate samples/sec, calculate drain time + small margin
    uint32_t dma_samples = DMA_BUF_COUNT * DMA_BUF_LEN;
    uint32_t drain_ms = (dma_samples * 1000UL) / wav_rate + 50;  // +50ms margin
    delay(drain_ms);
    
    i2s_zero_dma_buffer(I2S_PORT);
    s_playing = false;

    Serial.printf("        Done  (%.1f s)\n", (millis() - t0) / 1000.0f);

    // ── Restore default sample rate ──
    if (wav_rate != SAMPLE_RATE) {
        i2s_driver_uninstall(I2S_PORT);
        s_installed = false;
        i2s_install(SAMPLE_RATE);
    }

    return true;
}

void audio_play_wav_dir(const char* dirpath) {
    if (!LittleFS.begin(true)) {
        Serial.println("[AUDIO] LittleFS mount failed");
        return;
    }

    File root = LittleFS.open(dirpath);
    if (!root || !root.isDirectory()) {
        Serial.printf("[AUDIO] Directory not found: %s\n", dirpath);
        LittleFS.end();
        return;
    }

    Serial.printf("[AUDIO] Scanning %s for WAV files...\n", dirpath);

    int count = 0;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String fname = String(entry.name());

            String fullpath;
            if (fname.startsWith("/")) {
                fullpath = fname;
            } else {
                String dir = String(dirpath);
                if (!dir.endsWith("/")) dir += "/";
                fullpath = dir + fname;
            }

            String lower = fullpath;
            lower.toLowerCase();
            if (lower.endsWith(".wav")) {
                count++;
                Serial.printf("\n[AUDIO] Track %d — %s\n", count, fullpath.c_str());
                audio_play_wav(fullpath.c_str());
                delay(500);
            }
        }
        entry = root.openNextFile();
    }

    if (count == 0)
        Serial.printf("[AUDIO] No WAV files found in %s\n", dirpath);
    else
        Serial.printf("[AUDIO] Played %d WAV file(s)\n", count);

    LittleFS.end();
}

// ─────────────────────────────────────────────
//  MP3 File Playback (LittleFS, via minimp3)
//
//  Streaming decoder: reads MP3 in small chunks,
//  decodes frame-by-frame, and sends PCM to I2S.
//  No external library needed — minimp3 is a
//  single public-domain header included above.
// ─────────────────────────────────────────────

// Size of the file-read ring buffer.  Must hold at least one full
// MP3 frame (max 1441 bytes for 320 kbps @ 44.1 kHz) plus some
// look-ahead so minimp3 can always find the next sync word.
#define MP3_INBUF_SIZE  (1024 * 8)   // 8 KB – comfortably fits ≥4 frames

// Internal blocking implementation (runs on the audio background task)
static bool audio_play_mp3_blocking(const char* filepath) {
    if (!s_installed) return false;

    // Mount LittleFS for this playback session
    if (!LittleFS.begin(true)) {
        Serial.println("[AUDIO] LittleFS mount failed");
        s_playing = false;
        return false;
    }

    File file = LittleFS.open(filepath, "r");
    if (!file) {
        Serial.printf("[AUDIO] Cannot open: %s\n", filepath);
        LittleFS.end();
        s_playing = false;
        return false;
    }

    const size_t file_size = file.size();
    Serial.printf("[AUDIO] MP3  %s  (%u bytes)\n", filepath, (unsigned)file_size);

    // ── Allocate buffers (all on heap to keep task stack lean) ──
    uint8_t*   inbuf = (uint8_t*)malloc(MP3_INBUF_SIZE);
    // MINIMP3_MAX_SAMPLES_PER_FRAME = 1152*2 (stereo)
    int16_t*   pcm   = (int16_t*)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    int16_t*   mono  = (int16_t*)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    // mp3dec_t holds MDCT overlap + QMF state (~7 KB) — keep off the task stack
    mp3dec_t*  dec   = (mp3dec_t*)malloc(sizeof(mp3dec_t));
    if (!inbuf || !pcm || !mono || !dec) {
        Serial.println("[AUDIO] MP3 malloc failed");
        if (inbuf) free(inbuf);
        if (pcm)   free(pcm);
        if (mono)  free(mono);
        if (dec)   free(dec);
        file.close();
        LittleFS.end();
        s_playing = false;
        return false;
    }

    // ── Initialise decoder ──
    mp3dec_init(dec);

    s_playing = true;
    unsigned long t0     = millis();
    size_t buf_bytes     = 0;        // valid bytes currently in inbuf
    size_t buf_offset    = 0;        // read cursor inside inbuf
    bool   eof           = false;
    bool   first_frame   = true;
    uint32_t last_rate   = 0;        // to detect sample-rate changes

    while (s_playing) {
        // Poll hook
        if (s_poll_fn && s_poll_fn()) break;

        // ── Refill input buffer ──
        // Shift unconsumed bytes to the front, then top-up from file
        if (buf_offset > 0 && buf_bytes > buf_offset) {
            memmove(inbuf, inbuf + buf_offset, buf_bytes - buf_offset);
            buf_bytes -= buf_offset;
        } else if (buf_offset > 0) {
            buf_bytes = 0;
        }
        buf_offset = 0;

        if (!eof && buf_bytes < MP3_INBUF_SIZE) {
            size_t want = MP3_INBUF_SIZE - buf_bytes;
            size_t got  = file.read(inbuf + buf_bytes, want);
            if (got == 0) eof = true;
            buf_bytes += got;
        }

        if (buf_bytes == 0) break;   // nothing left to decode

        // ── Decode one frame ──
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(dec, inbuf + buf_offset,
                                          buf_bytes, pcm, &info);

        if (info.frame_bytes == 0) {
            // No valid sync found in remaining data — we're done
            break;
        }
        buf_offset += info.frame_bytes;

        if (samples == 0) {
            // Skipped frame (ID3 tag, garbage) — keep going
            continue;
        }

        // ── First valid frame: configure I2S to match ──
        if (first_frame) {
            first_frame = false;
            last_rate   = info.hz;
            Serial.printf("        %d Hz | %d ch | %d kbps\n",
                          info.hz, info.channels, info.bitrate_kbps);

            if ((uint32_t)info.hz != SAMPLE_RATE) {
                i2s_driver_uninstall(I2S_PORT);
                s_installed = false;
                i2s_install(info.hz);
            }
        }

        // ── Handle mid-stream sample-rate change (rare) ──
        if ((uint32_t)info.hz != last_rate) {
            last_rate = info.hz;
            i2s_driver_uninstall(I2S_PORT);
            s_installed = false;
            i2s_install(info.hz);
        }

        // ── Convert to mono + apply volume gain ──
        size_t out_samples;
        if (info.channels == 2) {
            out_samples = samples / 2;   // samples is total (L+R interleaved)
            for (size_t i = 0; i < out_samples; i++) {
                int32_t l = pcm[i * 2];
                int32_t r = pcm[i * 2 + 1];
                mono[i] = (int16_t)(((l + r) / 2) * s_gain);
            }
        } else {
            out_samples = samples;
            for (size_t i = 0; i < out_samples; i++) {
                mono[i] = (int16_t)(pcm[i] * s_gain);
            }
        }

        // ── Send PCM to I2S ──
        size_t bytes_written;
        i2s_write(I2S_PORT, mono, out_samples * sizeof(int16_t),
                  &bytes_written, portMAX_DELAY);
    }

    // ── Clean up ──
    free(inbuf);
    free(pcm);
    free(mono);
    free(dec);
    file.close();
    LittleFS.end();

    // Drain DMA buffers
    uint32_t rate = last_rate ? last_rate : SAMPLE_RATE;
    uint32_t dma_samples = DMA_BUF_COUNT * DMA_BUF_LEN;
    uint32_t drain_ms = (dma_samples * 1000UL) / rate + 50;
    delay(drain_ms);

    i2s_zero_dma_buffer(I2S_PORT);
    s_playing = false;

    Serial.printf("        Done  (%.1f s)\n", (millis() - t0) / 1000.0f);

    // Restore default sample rate
    if (last_rate != 0 && last_rate != SAMPLE_RATE) {
        i2s_driver_uninstall(I2S_PORT);
        s_installed = false;
        i2s_install(SAMPLE_RATE);
    }

    return true;
}

// ── Recursive helper: collects all .mp3 paths into a list ───────────
static void mp3_collect_recursive(const String& dirpath, std::vector<String>& out) {
    File dir = LittleFS.open(dirpath);
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    while (entry) {
        String name = String(entry.name());
        String fullpath = dirpath;
        if (!fullpath.endsWith("/")) fullpath += "/";
        fullpath += name;

        if (entry.isDirectory()) {
            mp3_collect_recursive(fullpath, out);   // recurse first, no decode yet
        } else {
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".mp3"))
                out.push_back(fullpath);
        }
        entry = dir.openNextFile();
    }
}

// Non-blocking public API: queues filepath and returns immediately.
// audio_is_playing() returns true until the task finishes.
bool audio_play_mp3(const char* filepath) {
    if (!s_audio_queue || !s_audio_task) {
        // Fallback: no task yet — call blocking version directly
        return audio_play_mp3_blocking(filepath);
    }
    if (s_playing) return false;  // already playing — caller should check audio_is_playing()

    char path[AUDIO_PATH_MAX];
    strncpy(path, filepath, AUDIO_PATH_MAX - 1);
    path[AUDIO_PATH_MAX - 1] = '\0';

    // Mark as playing NOW (before the task picks it up) to close the
    // race window where audio_is_playing() briefly returns false after
    // this call but before audio_play_mp3_blocking() sets s_playing.
    s_playing = true;

    // Post to queue (non-blocking — drop if somehow full)
    if (xQueueSend(s_audio_queue, path, 0) != pdTRUE) {
        s_playing = false;  // queue full — roll back
        return false;
    }
    return true;
}

void audio_play_mp3_dir(const char* dirpath) {
    if (!LittleFS.begin(true)) {
        Serial.println("[AUDIO] LittleFS mount failed");
        return;
    }

    Serial.printf("[AUDIO] Scanning %s (recursive) for MP3 files...\n", dirpath);

    // Phase 1 — collect paths (no decode happens here, stack stays shallow)
    std::vector<String> tracks;
    mp3_collect_recursive(String(dirpath), tracks);

    LittleFS.end();   // release FS handle before decoding

    if (tracks.empty()) {
        Serial.printf("[AUDIO] No MP3 files found under %s\n", dirpath);
        return;
    }

    // Phase 2 — play each track (decode runs at this call depth, no recursion above it)
    Serial.printf("[AUDIO] Found %d MP3 file(s), starting playback...\n", (int)tracks.size());
    for (int i = 0; i < (int)tracks.size(); i++) {
        Serial.printf("\n[AUDIO] Track %d/%d — %s\n", i + 1, (int)tracks.size(), tracks[i].c_str());

        // Re-mount for each open (or keep mounted — either works with LittleFS)
        if (!LittleFS.begin(true)) {
            Serial.println("[AUDIO] LittleFS mount failed");
            break;
        }
        audio_play_mp3(tracks[i].c_str());
        LittleFS.end();
        delay(500);
    }
}
