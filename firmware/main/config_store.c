#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config";
static const char *NS = "fox";

void config_set_defaults(fox_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->fox_name, "Ember", FOX_NAME_MAX - 1);
    strncpy(cfg->splash_text, "Hello!", SPLASH_MAX - 1);
    strncpy(cfg->color_primary, "#ff6b35", 7);
    strncpy(cfg->color_accent, "#f7c59f", 7);
    strncpy(cfg->color_bg, "#1a1a2e", 7);
    strncpy(cfg->mode, "groq", 15);
    strncpy(cfg->groq_model, "llama-3.1-8b-instant", 63);
    cfg->tool_ble_radar = true;
    cfg->tool_wifi_scan = true;
    cfg->tool_ir = true;
    cfg->tool_imu = true;
    cfg->tool_context = true;
    cfg->lip_sync = true;
}

void config_load(fox_config_t *cfg)
{
    config_set_defaults(cfg);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "No saved config, using defaults");
        return;
    }

    size_t len;
    len = sizeof(cfg->fox_name); nvs_get_str(h, "fox_name", cfg->fox_name, &len);
    len = sizeof(cfg->mode); nvs_get_str(h, "mode", cfg->mode, &len);
    len = sizeof(cfg->groq_api_key); nvs_get_str(h, "groq_key", cfg->groq_api_key, &len);
    len = sizeof(cfg->groq_model); nvs_get_str(h, "groq_model", cfg->groq_model, &len);
    len = sizeof(cfg->weights_url); nvs_get_str(h, "weights_url", cfg->weights_url, &len);
    len = sizeof(cfg->context_summary); nvs_get_str(h, "ctx", cfg->context_summary, &len);

    uint8_t b;
    if (nvs_get_u8(h, "ble", &b) == ESP_OK) cfg->tool_ble_radar = b;
    if (nvs_get_u8(h, "wifi", &b) == ESP_OK) cfg->tool_wifi_scan = b;
    if (nvs_get_u8(h, "ir", &b) == ESP_OK) cfg->tool_ir = b;
    if (nvs_get_u8(h, "imu", &b) == ESP_OK) cfg->tool_imu = b;
    if (nvs_get_u8(h, "ctx_en", &b) == ESP_OK) cfg->tool_context = b;
    if (nvs_get_u8(h, "lips", &b) == ESP_OK) cfg->lip_sync = b;

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded: name=%s mode=%s", cfg->fox_name, cfg->mode);
}

void config_save(const fox_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_str(h, "fox_name", cfg->fox_name);
    nvs_set_str(h, "mode", cfg->mode);
    nvs_set_str(h, "groq_key", cfg->groq_api_key);
    nvs_set_str(h, "groq_model", cfg->groq_model);
    nvs_set_str(h, "weights_url", cfg->weights_url);
    nvs_set_str(h, "ctx", cfg->context_summary);

    nvs_set_u8(h, "ble", cfg->tool_ble_radar);
    nvs_set_u8(h, "wifi", cfg->tool_wifi_scan);
    nvs_set_u8(h, "ir", cfg->tool_ir);
    nvs_set_u8(h, "imu", cfg->tool_imu);
    nvs_set_u8(h, "ctx_en", cfg->tool_context);
    nvs_set_u8(h, "lips", cfg->lip_sync);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved");
}
