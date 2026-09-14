/**
 * Minimal 128×128 fox avatar driver for AtomS3R (ST7735 / GC9107).
 * Real implementation should use M5Unified / esp_lcd + a small sprite sheet.
 */

#include "fox_avatar.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "avatar";
static char fox_name[17] = "Ember";
static char emotion[16] = "idle";
static float mouth = 0.f;

void fox_avatar_init(const char *name, const char *primary, const char *accent)
{
    if (name && name[0]) {
        strncpy(fox_name, name, sizeof(fox_name) - 1);
    }
    ESP_LOGI(TAG, "Avatar init name=%s primary=%s accent=%s", fox_name, primary ? primary : "?", accent ? accent : "?");
    // TODO: init LCD panel, load palette from primary/accent, allocate sprite buffers in PSRAM
}

void fox_avatar_show_splash(const char *text)
{
    ESP_LOGI(TAG, "Splash: %s", text ? text : "");
    // Draw centered text + simple fox face for 1–2 s
}

void fox_avatar_set_emotion(const char *e)
{
    if (e) {
        strncpy(emotion, e, sizeof(emotion) - 1);
        ESP_LOGD(TAG, "Emotion → %s", emotion);
    }
}

void fox_avatar_set_mouth(float open)
{
    if (open < 0.f) open = 0.f;
    if (open > 1.f) open = 1.f;
    mouth = open;
}

void fox_avatar_tick(void)
{
    // Redraw sprite according to emotion + mouth openness
    // Keep this cheap — 128×128 @ ~15–30 fps is plenty
}
