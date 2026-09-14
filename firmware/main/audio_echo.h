#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Atomic Echo Base (ES8311) on AtomS3R pin map:
// I2C: SDA=G38, SCL=G39
// I2S: DIN=G7, WS=G6, DOUT=G5, BCK=G8

void audio_echo_init(void);
void audio_echo_set_volume(uint8_t percent);

// Record PCM 16-bit mono @ 16 kHz into buffer (blocking or with timeout)
int audio_echo_record(int16_t *buf, size_t samples, int timeout_ms);

// Play PCM buffer
int audio_echo_play(const int16_t *buf, size_t samples);

// Simple amplitude for lip-sync
float audio_echo_last_amplitude(void);

// PTT helpers
void audio_echo_start_ptt(void);
void audio_echo_stop_ptt(void);
bool audio_echo_is_ptt_active(void);
