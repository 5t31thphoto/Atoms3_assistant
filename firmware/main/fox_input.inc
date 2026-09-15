// fox_input.inc — buttons, microphone capture, IMU gestures, on-device menu,
// USB-serial config receiver, and the (fixed) light-sleep path.
#pragma once
#include <esp_sleep.h>
#include <driver/gpio.h>

enum ButtonEvent { BTN_NONE, BTN_TAP, BTN_HOLD_START };
enum Gesture     { GST_NONE, GST_TAP, GST_SHAKE, GST_TILT };

static uint32_t btn_down_at = 0;
static bool     btn_was_down = false;

static void input_begin() {
    pinMode((int)USER_GPIO, INPUT_PULLUP);
    M5.Imu.begin();
}

// Distinguish a quick tap (open menu) from a hold (push-to-talk). We report
// HOLD_START as soon as the press passes PTT_MIN so capture can begin live.
static ButtonEvent input_button_event() {
    bool down = digitalRead((int)USER_GPIO) == LOW;
    uint32_t now = millis();
    if (down && !btn_was_down) { btn_down_at = now; btn_was_down = true; }
    if (down && btn_was_down && now - btn_down_at > PTT_MIN_MS) {
        return BTN_HOLD_START;   // caller will capture until release
    }
    if (!down && btn_was_down) {
        uint32_t held = now - btn_down_at;
        btn_was_down = false;
        if (held <= PTT_MIN_MS) return BTN_TAP;
    }
    return BTN_NONE;
}

static bool button_is_down() { return digitalRead((int)USER_GPIO) == LOW; }

// Simple yes-confirm used by IR narrowing: tap the button within `ms`.
bool input_wait_yes(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        M5.update();
        if (digitalRead((int)USER_GPIO) == LOW) {
            // debounce release
            while (digitalRead((int)USER_GPIO) == LOW) delay(5);
            return true;
        }
        delay(5);
    }
    return false;
}

// ---- microphone capture -----------------------------------------------------
// Capture while the USER button is held. Returns a PSRAM buffer of int16 mono
// @16k; caller frees. Also drives the mouth from live level for lip-sync.
static int16_t* capture_while_held(size_t* out_n) {
    size_t cap = SAMPLE_RATE * (PTT_MAX_MS / 1000) + SAMPLE_RATE;
    int16_t* buf = (int16_t*)heap_caps_malloc(cap * sizeof(int16_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { *out_n = 0; return nullptr; }
    M5.Speaker.end();
    if (!M5.Mic.begin()) { *out_n = 0; return buf; }
    size_t n = 0; uint32_t t0 = millis();
    const size_t CH = 256;
    while (button_is_down() && n + CH < cap && millis() - t0 < PTT_MAX_MS) {
        if (M5.Mic.record(buf + n, CH, SAMPLE_RATE)) {
            // crude level for lip-sync
            long s = 0; for (size_t i = 0; i < CH; ++i) s += abs(buf[n + i]);
            face_set_mouth((s / (float)CH) / 8000.0f);
            n += CH;
        }
        M5.update();
    }
    M5.Mic.end();
    *out_n = n;
    return buf;
}

// Capture a single utterance using level-gated VAD, ending after trailing
// silence or when `timeout_ms` elapses with no speech. Used in conversation mode.
static int16_t* capture_vad_burst(size_t* out_n, uint32_t timeout_ms) {
    size_t cap = SAMPLE_RATE * 12;
    int16_t* buf = (int16_t*)heap_caps_malloc(cap * sizeof(int16_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { *out_n = 0; return nullptr; }
    M5.Speaker.end();
    if (!M5.Mic.begin()) { *out_n = 0; return buf; }
    size_t n = 0; uint32_t t0 = millis(); uint32_t last_voice = 0;
    bool started = false;
    const size_t CH = 256;
    while (n + CH < cap) {
        if (millis() - t0 > timeout_ms && !started) break;   // never spoke
        if (M5.Mic.record(buf + n, CH, SAMPLE_RATE)) {
            long s = 0; for (size_t i = 0; i < CH; ++i) s += abs(buf[n + i]);
            float lvl = (s / (float)CH);
            face_set_mouth(lvl / 8000.0f);
            if (lvl > 900) { started = true; last_voice = millis(); }
            n += CH;
            if (started && millis() - last_voice > 900) break;  // trailing silence
        }
        M5.update();
        if (button_is_down()) break;  // let PTT interrupt
    }
    M5.Mic.end();
    *out_n = started ? n : 0;
    return buf;
}

// ---- IMU gestures -----------------------------------------------------------
static Gesture input_gesture() {
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return GST_NONE;
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 400) return GST_NONE;
    if (mag > 2.4f) { last = now; return GST_SHAKE; }
    if (mag > 1.6f) { last = now; return GST_TAP; }
    return GST_NONE;
}

// pickup-to-wake used when returning from light sleep
static bool imu_picked_up() {
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;
    return fabsf(sqrtf(ax*ax + ay*ay + az*az) - 1.0f) > 0.35f;
}

// ---- light sleep (FIX bug #4) ----------------------------------------------
// The old code used ext0 wake on GPIO41. On the ESP32-S3, ext0/ext1 deep-sleep
// wake only supports RTC-capable GPIO0..21 — GPIO41 is not one, so the device
// would sleep forever. We instead use *light* sleep with a GPIO wake source,
// which works on any GPIO, and also wake on a timer to check the IMU.
void enter_light_sleep() {
    face_caption("*curls up*");
    face_dim();
    M5.Speaker.end(); M5.Mic.end();
    gpio_wakeup_enable(USER_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(3ULL * 1000000ULL);   // peek every 3s
    for (;;) {
        esp_light_sleep_start();
        esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
        if (c == ESP_SLEEP_WAKEUP_GPIO) break;           // button press
        if (c == ESP_SLEEP_WAKEUP_TIMER && imu_picked_up()) break;  // picked up
    }
    face_wake();
}

// ---- on-device menu (FIX bug #6) -------------------------------------------
// A minimal scrollable menu: short-tap cycles items, hold selects. Kept tiny;
// the web flasher does the heavy configuration.
static const char* MENU_ITEMS[] = {
    "talk to me", "conversation mode", "volume", "voice pack",
    "learn my tv", "forget memories", "sleep", "close",
};
static const int MENU_N = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

void speak(const String&);
void ir_narrow_power();

static void menu_render(int sel) {
    face_canvas->fillSprite(face_cfg->color_bg);
    face_canvas->setTextColor(0xFFFF);
    face_canvas->setTextDatum(middle_center);
    for (int i = 0; i < MENU_N; ++i) {
        int y = 20 + i * 13;
        if (i == sel) {
            face_canvas->fillRoundRect(6, y - 6, 116, 13, 3, face_cfg->color_primary);
            face_canvas->setTextColor(0x0000);
        } else face_canvas->setTextColor(0xFFFF);
        face_canvas->drawString(MENU_ITEMS[i], 64, y);
    }
    face_canvas->pushSprite(0, 0);
}

void open_menu() {
    int sel = 0;
    uint32_t idle = millis();
    menu_render(sel);
    for (;;) {
        M5.update();
        bool down = digitalRead((int)USER_GPIO) == LOW;
        if (down) {
            uint32_t t0 = millis();
            while (digitalRead((int)USER_GPIO) == LOW) { delay(5); M5.update(); }
            uint32_t held = millis() - t0;
            idle = millis();
            if (held > 500) {   // select
                const char* it = MENU_ITEMS[sel];
                if (!strcmp(it, "close")) return;
                if (!strcmp(it, "conversation mode")) { cfg.conversation = true; speak("okay, i'm listening~"); return; }
                if (!strcmp(it, "volume")) { cfg.volume = (cfg.volume + 20) % 120; M5.Speaker.setVolume(cfg.volume); save_config(); speak("volume set"); }
                if (!strcmp(it, "voice pack")) { cfg.voice_pack = (cfg.voice_pack == "chatterbox") ? "critter" : "chatterbox"; voice_begin(cfg); save_config(); speak("voice changed~"); }
                if (!strcmp(it, "learn my tv")) { ir_narrow_power(); return; }
                if (!strcmp(it, "forget memories")) { mem_clear(); speak("okay, all forgotten"); return; }
                if (!strcmp(it, "sleep")) { speak("night night"); enter_light_sleep(); return; }
                if (!strcmp(it, "talk to me")) return;
            } else {            // tap: next item
                sel = (sel + 1) % MENU_N;
                menu_render(sel);
            }
        }
        if (millis() - idle > 8000) return;   // auto-close
        delay(10);
    }
}

// ---- games (fidget mini-interactions) --------------------------------------
void play_game() {
    // "guess my paw": fox picks left/right, you tap within 2s to match.
    int paw = esp_random() & 1;
    speak(paw ? "left paw or right? tap if you think right~"
              : "left paw or right? tap if you think left~");
    bool tapped = input_wait_yes(2200);
    speak(tapped ? "hehe you got me!" : "missed! again sometime~");
    needs_interact(needs, true);
}

// ---- weather ---------------------------------------------------------------
String fetch_weather() {
    if (cfg.weather.isEmpty() || !wifi_connect()) return "i can't check the weather right now";
    WiFiClientSecure c; c.setInsecure();
    HTTPClient h;
    String url = "https://wttr.in/" + cfg.weather + "?format=%C+%t";
    if (!h.begin(c, url)) return "weather is being shy";
    String out = (h.GET() == 200) ? h.getString() : String("");
    h.end();
    out.trim();
    return out.length() ? ("it's " + out) : "weather is being shy";
}

// ---- USB serial config receiver --------------------------------------------
// The web flasher sends a single JSON line over USB CDC after flashing. We
// parse it and persist. Secrets (wifi/key) live only in NVS on the device;
// they are never compiled into the binary or CI.
static String serial_line;
static void apply_config_json(const String& js) {
    JsonDocument d;
    if (deserializeJson(d, js) != DeserializationError::Ok) return;
    if (d["name"].is<const char*>())  cfg.name = d["name"].as<String>();
    if (d["tz"].is<const char*>())    cfg.timezone = d["tz"].as<String>();
    if (d["weather"].is<const char*>()) cfg.weather = d["weather"].as<String>();
    if (d["ssid"].is<const char*>())  cfg.wifi_ssid = d["ssid"].as<String>();
    if (d["pass"].is<const char*>())  cfg.wifi_pass = d["pass"].as<String>();
    if (d["key"].is<const char*>())   cfg.api_key = d["key"].as<String>();
    if (d["chat_model"].is<const char*>()) cfg.chat_model = d["chat_model"].as<String>();
    if (d["stt_model"].is<const char*>())  cfg.stt_model = d["stt_model"].as<String>();
    if (d["api_base"].is<const char*>())   cfg.api_base = d["api_base"].as<String>();
    if (d["personality"].is<const char*>()) cfg.personality = d["personality"].as<String>();
    if (d["voice"].is<const char*>()) cfg.voice_pack = d["voice"].as<String>();
    if (d["volume"].is<int>())        cfg.volume = d["volume"].as<int>();
    if (d["cpri"].is<int>())          cfg.color_primary = d["cpri"].as<int>();
    if (d["cacc"].is<int>())          cfg.color_accent = d["cacc"].as<int>();
    if (d["cbg"].is<int>())           cfg.color_bg = d["cbg"].as<int>();
    save_config();
    voice_begin(cfg);
    Serial.println("FOX_CONFIG_OK");
    speak(String("thanks! i'm ") + cfg.name + " now~");
}

static void input_poll() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') { apply_config_json(serial_line); serial_line = ""; }
        else if (c != '\r' && serial_line.length() < 1024) serial_line += c;
    }
}
