#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FOX_NAME_MAX 17
#define SPLASH_MAX   33
#define API_KEY_MAX  128
#define URL_MAX      256

typedef struct {
    char fox_name[FOX_NAME_MAX];
    char splash_text[SPLASH_MAX];
    char color_primary[8];
    char color_accent[8];
    char color_bg[8];

    char mode[16];          // "groq" | "ondevice" | "stream" | "hybrid"
    char groq_api_key[API_KEY_MAX];
    char groq_model[64];
    char system_prompt[512];

    char weights_url[URL_MAX];

    bool tool_ble_radar;
    bool tool_wifi_scan;
    bool tool_ir;
    bool tool_imu;
    bool tool_context;
    bool lip_sync;

    // Tiny persistent context (summaries only — flash friendly)
    char context_summary[256];
} fox_config_t;

void config_load(fox_config_t *cfg);
void config_save(const fox_config_t *cfg);
void config_set_defaults(fox_config_t *cfg);
