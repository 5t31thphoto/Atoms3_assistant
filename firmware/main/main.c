/**
 * Fox Voice Assistant — main entry
 * Hardware: M5Stack AtomS3R + Atomic Echo Base
 *
 * Controls:
 *   Flick up/down (IMU)  → navigate menu
 *   Single click button  → confirm
 *   Double click         → open menu
 *   Long press           → PTT (listen)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "config_store.h"
#include "audio_echo.h"
#include "fox_avatar.h"
#include "tools.h"
#include "llm_client.h"

static const char *TAG = "fox";

// Placeholder generated header (CI writes the real one)
#ifndef FOX_NAME
#define FOX_NAME "Ember"
#endif
#ifndef FOX_MODE
#define FOX_MODE "groq"
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Fox Voice Assistant starting — name=%s mode=%s", FOX_NAME, FOX_MODE);

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Load persistent config (API key, Wi-Fi, fox name override, etc.)
    fox_config_t cfg;
    config_load(&cfg);
    if (cfg.fox_name[0] == '\0') {
        strncpy(cfg.fox_name, FOX_NAME, sizeof(cfg.fox_name) - 1);
    }

    // Init display + avatar
    fox_avatar_init(cfg.fox_name, cfg.color_primary, cfg.color_accent);
    fox_avatar_show_splash(cfg.splash_text[0] ? cfg.splash_text : "Hello!");

    // Audio (Atomic Echo Base — ES8311 I2S)
    audio_echo_init();

    // Tools (BLE, Wi-Fi, IR, IMU)
    tools_init(&cfg);

    // LLM client (Groq / on-device / stream)
    llm_client_init(&cfg);

    ESP_LOGI(TAG, "Ready. Long-press button for PTT.");

    // Main loop is event-driven (button ISR, IMU task, audio callbacks)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Heartbeat / low-power housekeeping can go here
    }
}
