#pragma once
#include "config_store.h"

void tools_init(const fox_config_t *cfg);

// LLM-callable tools (return short JSON-ish strings the LLM can read)
const char *tool_ble_radar(void);   // scan + IMU bearing → list of devices
const char *tool_wifi_scan(void);
const char *tool_ir_send(const char *code_hex);
const char *tool_imu_gesture(void);

// Standalone menu modes (no LLM required)
void tools_menu_ble_radar(void);
void tools_menu_wifi(void);
