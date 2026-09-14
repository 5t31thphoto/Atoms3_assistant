/**
 * Minimal Atomic Echo Base (ES8311) audio glue.
 * Full production code should use the official M5Atomic-EchoBase library
 * or a proper ES8311 + I2S driver. This is a structural stub.
 */

#include "audio_echo.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "audio";

#define SAMPLE_RATE 16000
#define I2S_NUM     I2S_NUM_0

// AtomS3R + Atomic Echo pin map (from M5 docs)
#define PIN_I2C_SDA 38
#define PIN_I2C_SCL 39
#define PIN_I2S_DIN  7
#define PIN_I2S_WS   6
#define PIN_I2S_DOUT 5
#define PIN_I2S_BCK  8

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static float last_amp = 0.f;
static bool ptt_active = false;

void audio_echo_init(void)
{
    ESP_LOGI(TAG, "Init Atomic Echo (ES8311 stub) @ %d Hz", SAMPLE_RATE);

    // TODO: Initialize ES8311 over I2C (codec registers, mic gain, speaker amp)
    // TODO: Create I2S duplex channel with the pins above

    // Placeholder so the rest of the firmware compiles and links
    ESP_LOGW(TAG, "Audio is a stub — integrate M5Atomic-EchoBase or ES8311 driver for real I/O");
}

void audio_echo_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    // Write to ES8311 volume register
    ESP_LOGI(TAG, "Volume %u%%", percent);
}

int audio_echo_record(int16_t *buf, size_t samples, int timeout_ms)
{
    if (!buf || samples == 0) return -1;
    // Real implementation: i2s_channel_read(rx_handle, ...)
    memset(buf, 0, samples * sizeof(int16_t));
    ESP_LOGD(TAG, "Record %u samples (stub)", (unsigned)samples);
    return (int)samples;
}

int audio_echo_play(const int16_t *buf, size_t samples)
{
    if (!buf || samples == 0) return -1;
    // Compute rough amplitude for lip-sync
    int64_t sum = 0;
    for (size_t i = 0; i < samples; i++) {
        sum += abs(buf[i]);
    }
    last_amp = (float)sum / (samples * 32768.f);
    // Real: i2s_channel_write(tx_handle, ...)
    ESP_LOGD(TAG, "Play %u samples amp=%.3f (stub)", (unsigned)samples, last_amp);
    return (int)samples;
}

float audio_echo_last_amplitude(void)
{
    return last_amp;
}

void audio_echo_start_ptt(void) { ptt_active = true; }
void audio_echo_stop_ptt(void)  { ptt_active = false; }
bool audio_echo_is_ptt_active(void) { return ptt_active; }
