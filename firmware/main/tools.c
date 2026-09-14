/**
 * Flipper-inspired tools.
 * These are intentionally lightweight stubs that compile and demonstrate
 * the call surface the LLM (and the menu) can use.
 */

#include "tools.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tools";
static fox_config_t local_cfg;

void tools_init(const fox_config_t *cfg)
{
    if (cfg) local_cfg = *cfg;
    ESP_LOGI(TAG, "Tools init (ble=%d wifi=%d ir=%d imu=%d)",
             local_cfg.tool_ble_radar, local_cfg.tool_wifi_scan,
             local_cfg.tool_ir, local_cfg.tool_imu);
    // TODO: start BLE scanner task, IMU read task, IR RMT, etc.
}

const char *tool_ble_radar(void)
{
    // Real: continuous BLE scan + BMI270 orientation → polar positions
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"devices\":[{\"name\":\"demo\",\"rssi\":-55,\"bearing\":30}],\"note\":\"stub\"}");
    ESP_LOGI(TAG, "BLE radar (stub)");
    return buf;
}

const char *tool_wifi_scan(void)
{
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"aps\":[{\"ssid\":\"demo\",\"rssi\":-40,\"ch\":6}],\"note\":\"stub\"}");
    ESP_LOGI(TAG, "Wi-Fi scan (stub)");
    return buf;
}

const char *tool_ir_send(const char *code_hex)
{
    static char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"sent\":\"%s\"}", code_hex ? code_hex : "");
    ESP_LOGI(TAG, "IR send %s (stub)", code_hex ? code_hex : "");
    return buf;
}

const char *tool_imu_gesture(void)
{
    static char buf[64];
    snprintf(buf, sizeof(buf), "{\"gesture\":\"none\"}");
    return buf;
}

void tools_menu_ble_radar(void)
{
    ESP_LOGI(TAG, "Menu: BLE radar view — fox steps aside, polar plot on screen");
    // Draw radar UI, use IMU for orientation, list devices
}

void tools_menu_wifi(void)
{
    ESP_LOGI(TAG, "Menu: Wi-Fi tools");
}
