/*
 * @file audio.cpp
 * @brief I2S audio output via MAX98357A amplifier
 *        Generates tones / plays simple audio feedback
 */
#include "audio.h"
#include "config.h"
#include "driver/i2s.h"
#include <math.h>

#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     22050
#define DMA_BUF_COUNT   4
#define DMA_BUF_LEN     256

static bool     i2s_installed = false;
static volatile bool playing  = false;

void audio_init() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = DMA_BUF_COUNT;
    cfg.dma_buf_len          = DMA_BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;

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
    i2s_installed = true;
    Serial.println("[AUDIO] I2S ready");
}

void audio_play_tone(uint16_t freq_hz, uint16_t duration_ms) {
    if (!i2s_installed) return;
    playing = true;

    uint32_t total_samples = (uint32_t)SAMPLE_RATE * duration_ms / 1000;
    int16_t  buf[DMA_BUF_LEN];
    uint32_t written = 0;
    float    phase   = 0;
    float    inc     = 2.0f * PI * freq_hz / SAMPLE_RATE;

    while (written < total_samples && playing) {
        uint32_t chunk = min((uint32_t)DMA_BUF_LEN, total_samples - written);

        // Envelope: simple linear fade-in / fade-out (20 ms)
        for (uint32_t i = 0; i < chunk; i++) {
            float env = 1.0f;
            uint32_t pos = written + i;
            uint32_t fade_samples = SAMPLE_RATE / 50;    // 20 ms
            if (pos < fade_samples)
                env = (float)pos / fade_samples;
            else if (pos > total_samples - fade_samples)
                env = (float)(total_samples - pos) / fade_samples;

            buf[i] = (int16_t)(sinf(phase) * 16000.0f * env);
            phase += inc;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }

        size_t bytes_written;
        i2s_write(I2S_PORT, buf, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        written += chunk;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    playing = false;
}

void audio_stop() {
    playing = false;
    if (i2s_installed) i2s_zero_dma_buffer(I2S_PORT);
}

bool audio_is_playing() { return playing; }
