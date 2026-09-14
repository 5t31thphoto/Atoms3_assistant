/**
 * LLM client — Groq (OpenAI-compatible), on-device stub, experimental stream stub.
 * Production code should use esp_http_client + JSON streaming for Groq,
 * and a real on-device engine (Needle / MimiModel / esp32-ai) for offline.
 */

#include "llm_client.h"
#include "tools.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "llm";
static fox_config_t cfg;

void llm_client_init(const fox_config_t *c)
{
    if (c) cfg = *c;
    ESP_LOGI(TAG, "LLM client mode=%s model=%s", cfg.mode, cfg.groq_model);
    llm_client_register_tools();
}

void llm_client_register_tools(void)
{
    // In a full implementation, build an OpenAI-style tools array that
    // describes tool_ble_radar, tool_wifi_scan, etc. and send it with every request.
    ESP_LOGD(TAG, "Tools registered for LLM");
}

static char *groq_chat(const char *user_text)
{
    if (cfg.groq_api_key[0] == '\0') {
        ESP_LOGE(TAG, "No Groq API key");
        return strdup("I need a Groq API key to talk properly.");
    }

    // Real code: POST https://api.groq.com/openai/v1/chat/completions
    // with messages + tools, stream or non-stream, parse tool_calls, execute, continue.
    ESP_LOGI(TAG, "Groq chat (stub): %s", user_text);
    char *reply = malloc(256);
    if (reply) {
        snprintf(reply, 256, "Hi! I'm %s (Groq stub). You said: %.80s", cfg.fox_name, user_text);
    }
    return reply;
}

static char *ondevice_chat(const char *user_text)
{
    // Hook for Needle 2 / MimiModel / esp32-ai inference
    ESP_LOGI(TAG, "On-device chat (stub): %s", user_text);
    char *reply = malloc(128);
    if (reply) {
        snprintf(reply, 128, "(on-device) I heard you, but the model is not linked yet.");
    }
    return reply;
}

static char *stream_chat(const char *user_text)
{
    // Request weight chunks from cfg.weights_url, run partial forward passes
    ESP_LOGI(TAG, "Stream chat (stub) weights=%s", cfg.weights_url);
    char *reply = malloc(128);
    if (reply) {
        snprintf(reply, 128, "(stream) Experimental weight streaming not fully wired.");
    }
    return reply;
}

char *llm_client_chat(const char *user_text)
{
    if (!user_text) return NULL;

    if (strcmp(cfg.mode, "ondevice") == 0) {
        return ondevice_chat(user_text);
    }
    if (strcmp(cfg.mode, "stream") == 0) {
        return stream_chat(user_text);
    }
    // groq or hybrid (hybrid offline detection omitted in stub)
    return groq_chat(user_text);
}
