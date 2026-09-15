// fox_main.cpp — Fox voice companion, main firmware.
//
// Target: M5Stack AtomS3R (ESP32-S3) + Atomic Echo Base.
// Offline-first: everything core works with no network. Cloud (Groq or any
// OpenAI-compatible endpoint) is an *optional* enhancement layered on top.
//
// Interaction model (per the owner's emphatic instructions):
//   * PUSH-TO-TALK ONLY. Hold the USER button to talk; release to process.
//     There is NO wake word. A short tap opens the menu instead.
//   * "Conversation mode" is opt-in from the menu (or by asking the fox). In
//     that mode it keeps listening for a while and times out on silence.
//
// This file wires together the subsystems implemented in the sibling files:
//   fox_brain.cpp   personality/mood/needs/reflection
//   fox_voice.cpp   Pico / critter / babble speech
//   fox_memory.cpp  device-owned rolling journal
//   fox_llm.cpp     optional tiny on-device brain (safe: can't invent facts)
//   fox_ir.cpp      IR sweep + fake-learning (this file includes helpers)
//
// Seven concrete bugs from the previous core are fixed here; each is called out
// with a "// FIX:" comment where it lives.
#include "fox.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <driver/rmt_tx.h>
#include <math.h>

// esp-sr (offline speech)
#include <esp_mn_iface.h>
#include <esp_afe_sr_iface.h>
#include <esp_afe_config.h>
#include <esp_mn_models.h>
#include <esp_process_sdkconfig.h>
#include <model_path.h>

// ============================================================================
//  Config persistence
// ============================================================================
static FoxConfig cfg;
static FoxNeeds  needs;
static Preferences prefs;

static void load_config() {
    prefs.begin("fox", true);
    cfg.name        = prefs.getString("name", cfg.name);
    cfg.timezone    = prefs.getString("tz", cfg.timezone);
    cfg.weather     = prefs.getString("wx", cfg.weather);
    cfg.wifi_ssid   = prefs.getString("ssid", cfg.wifi_ssid);
    cfg.wifi_pass   = prefs.getString("pass", cfg.wifi_pass);
    cfg.api_key     = prefs.getString("key", cfg.api_key);
    cfg.chat_model  = prefs.getString("cmodel", cfg.chat_model);
    cfg.stt_model   = prefs.getString("smodel", cfg.stt_model);
    cfg.api_base    = prefs.getString("abase", cfg.api_base);
    cfg.personality = prefs.getString("pers", cfg.personality);
    cfg.voice_pack  = prefs.getString("voice", cfg.voice_pack);
    cfg.volume      = prefs.getUChar("vol", cfg.volume);
    cfg.color_primary = prefs.getUShort("cpri", cfg.color_primary);
    cfg.color_accent  = prefs.getUShort("cacc", cfg.color_accent);
    cfg.color_bg      = prefs.getUShort("cbg", cfg.color_bg);
    cfg.captions    = prefs.getBool("cap", cfg.captions);
    prefs.end();
    cfg.cloud_enabled = cfg.api_key.length() > 0;
}

static void save_config() {
    prefs.begin("fox", false);
    prefs.putString("name", cfg.name);
    prefs.putString("tz", cfg.timezone);
    prefs.putString("wx", cfg.weather);
    prefs.putString("ssid", cfg.wifi_ssid);
    prefs.putString("pass", cfg.wifi_pass);
    prefs.putString("key", cfg.api_key);
    prefs.putString("cmodel", cfg.chat_model);
    prefs.putString("smodel", cfg.stt_model);
    prefs.putString("abase", cfg.api_base);
    prefs.putString("pers", cfg.personality);
    prefs.putString("voice", cfg.voice_pack);
    prefs.putUChar("vol", cfg.volume);
    prefs.putUShort("cpri", cfg.color_primary);
    prefs.putUShort("cacc", cfg.color_accent);
    prefs.putUShort("cbg", cfg.color_bg);
    prefs.putBool("cap", cfg.captions);
    prefs.end();
    cfg.cloud_enabled = cfg.api_key.length() > 0;
}

// ============================================================================
//  Offline speech (MultiNet7 + AFE). Reused from the prior core, which was
//  correct here — with bug #1 fixed in recognize_offline().
// ============================================================================
struct Command { int id; const char* phrases; const char* action; };
// Keep phrases short and phonetically distinct. IDs are 1-based.
static const Command COMMANDS[] = {
    {1,  "hello fox;hey fox;hi fox",             "greet"},
    {2,  "what time is it;tell me the time",      "time"},
    {3,  "how are you;how do you feel",           "mood"},
    {4,  "what is the weather;weather",           "weather"},
    {5,  "turn off the tv;power off tv",          "ir_tv_power"},
    {6,  "volume up",                              "ir_vol_up"},
    {7,  "volume down",                            "ir_vol_dn"},
    {8,  "lets play;play a game",                  "play"},
    {9,  "go to sleep;good night",                 "sleep"},
    {10, "conversation mode;lets chat",            "conv_on"},
    {11, "remember this",                          "remember"},
    {12, "what do you remember",                   "recall"},
};
static const size_t COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

static srmodel_list_t* sr_models = nullptr;
static const esp_afe_sr_iface_t* afe = nullptr;
static esp_afe_sr_data_t* afe_data = nullptr;
static const esp_mn_iface_t* mn = nullptr;
static model_iface_data_t* mn_data = nullptr;
static bool speech_ready = false;

static bool init_speech() {
    sr_models = esp_srmodel_init("model");
    if (!sr_models) { Serial.println("FOX: no speech model partition"); return false; }
    char* mn_name = esp_srmodel_filter(sr_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) { Serial.println("FOX: English MultiNet not found"); return false; }
    mn = esp_mn_handle_from_name(mn_name);
    if (!mn) return false;
    mn_data = mn->create(mn_name, 7000);
    if (!mn_data) return false;

    afe_config_t* ac = afe_config_init("M", sr_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!ac) return false;
    ac->aec_init = false; ac->se_init = false; ac->ns_init = true;
    ac->vad_init = true;  ac->wakenet_init = false; ac->agc_init = true;
    ac->fixed_output_channel = true;
    ac->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    ac->afe_ringbuf_size = 8;
    afe = esp_afe_handle_from_config(ac);
    afe_data = afe ? afe->create_from_config(ac) : nullptr;
    afe_config_free(ac);
    if (!afe_data) return false;

    if (esp_mn_commands_alloc((esp_mn_iface_t*)mn, (model_iface_data_t*)mn_data) != ESP_OK)
        return false;
    esp_mn_commands_clear();
    for (size_t i = 0; i < COMMAND_COUNT; ++i)
        esp_mn_commands_add(COMMANDS[i].id, (char*)COMMANDS[i].phrases);
    esp_mn_commands_update();
    mn->print_active_speech_commands(mn_data);
    if (afe->get_fetch_chunksize(afe_data) != mn->get_samp_chunksize(mn_data)) {
        Serial.println("FOX: AFE/MultiNet frame mismatch"); return false;
    }
    speech_ready = true;
    return true;
}

static int recognize_offline(int16_t* audio, size_t samples) {
    if (!speech_ready || !audio || samples < SAMPLE_RATE / 4) return -1;
    const int feed_n = afe->get_feed_chunksize(afe_data);
    if (feed_n != afe->get_fetch_chunksize(afe_data)) return -1;
    afe->reset_buffer(afe_data);
    int16_t* in = (int16_t*)heap_caps_malloc(feed_n * sizeof(int16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!in) return -1;
    int found_id = -1;
    float found_prob = 0.0f;
    // FIX (bug #1): the old loop condition was `pos < samples && !found_id`.
    // found_id starts at -1, and !(-1) is false, so the body never ran and
    // offline recognition ALWAYS failed. Use an explicit found flag instead.
    bool found = false;
    for (size_t pos = 0; pos < samples && !found; pos += feed_n) {
        size_t n = min((size_t)feed_n, samples - pos);
        memcpy(in, audio + pos, n * sizeof(int16_t));
        if (n < (size_t)feed_n) memset(in + n, 0, (feed_n - n) * sizeof(int16_t));
        if (afe->feed(afe_data, in) < 0) continue;
        afe_fetch_result_t* r = afe->fetch_with_delay(afe_data, 2 / portTICK_PERIOD_MS);
        if (!r || r->ret_value != ESP_OK || !r->data) continue;
        esp_mn_state_t st = mn->detect(mn_data, r->data);
        if (st == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* res = mn->get_results(mn_data);
            if (res && res->num > 0) {
                found_id = res->command_id[0];
                found_prob = res->prob[0];
                found = true;
            }
        }
    }
    free(in);
    if (found_id < 0 || found_prob < MIN_COMMAND_PROB) return -1;
    return found_id;
}

// ============================================================================
//  Cloud (optional). OpenAI-compatible chat + Whisper transcription.
// ============================================================================
static bool wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (cfg.wifi_ssid.isEmpty()) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(150);
    return WiFi.status() == WL_CONNECTED;
}

static String fox_system_prompt() {
    // The device stays in charge of identity and memory. The cloud model is
    // told who it is and given only a bounded recent slice of the journal.
    String p = "You are " + cfg.name + ", a small AI fox companion living in a "
               "tiny device. Personality: " + cfg.personality + ". Keep replies "
               "to one or two short, warm, playful sentences. You are a fox, not "
               "an assistant; be a little fidgety and affectionate. Never claim to "
               "do things the device cannot actually do.\n";
    String mem = mem_tail(1200);
    if (mem.length()) p += "Recent memories:\n" + mem;
    return p;
}

static String cloud_chat(const String& user_text) {
    if (!cfg.cloud_enabled || !wifi_connect()) return "";
    WiFiClientSecure client; client.setInsecure();
    HTTPClient h;
    if (!h.begin(client, cfg.api_base + "/chat/completions")) return "";
    h.addHeader("Content-Type", "application/json");
    h.addHeader("Authorization", "Bearer " + cfg.api_key);
    h.setTimeout(15000);
    JsonDocument q;
    q["model"] = cfg.chat_model;
    q["temperature"] = 0.7;
    q["max_tokens"] = 160;
    JsonArray msgs = q["messages"].to<JsonArray>();
    JsonObject s = msgs.add<JsonObject>(); s["role"] = "system"; s["content"] = fox_system_prompt();
    JsonObject u = msgs.add<JsonObject>(); u["role"] = "user"; u["content"] = user_text;
    String body; serializeJson(q, body);
    int code = h.POST(body);
    String out;
    if (code == 200) {
        JsonDocument r;
        if (deserializeJson(r, h.getString()) == DeserializationError::Ok)
            out = r["choices"][0]["message"]["content"].as<String>();
    }
    h.end();
    return out;
}

static String cloud_transcribe(int16_t* audio, size_t samples) {
    if (!audio || !samples || !cfg.cloud_enabled || !wifi_connect()) return "";
    String head = "--foxB\r\nContent-Disposition: form-data; name=\"file\"; "
                  "filename=\"a.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--foxB\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"
                  + cfg.stt_model + "\r\n--foxB--\r\n";
    // Build a minimal WAV header so Whisper accepts the PCM.
    uint32_t data_bytes = samples * 2;
    uint32_t riff = 36 + data_bytes;
    uint8_t wav[44];
    memcpy(wav, "RIFF", 4); memcpy(wav + 4, &riff, 4); memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4); uint32_t six = 16; memcpy(wav + 16, &six, 4);
    uint16_t pcm = 1, ch = 1; memcpy(wav + 20, &pcm, 2); memcpy(wav + 22, &ch, 2);
    uint32_t sr = SAMPLE_RATE; memcpy(wav + 24, &sr, 4);
    uint32_t br = SAMPLE_RATE * 2; memcpy(wav + 28, &br, 4);
    uint16_t ba = 2, bps = 16; memcpy(wav + 32, &ba, 2); memcpy(wav + 34, &bps, 2);
    memcpy(wav + 36, "data", 4); memcpy(wav + 40, &data_bytes, 4);

    size_t total = head.length() + 44 + data_bytes + tail.length();
    uint8_t* buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return "";
    size_t o = 0;
    memcpy(buf + o, head.c_str(), head.length()); o += head.length();
    memcpy(buf + o, wav, 44); o += 44;
    memcpy(buf + o, audio, data_bytes); o += data_bytes;
    memcpy(buf + o, tail.c_str(), tail.length()); o += tail.length();

    WiFiClientSecure client; client.setInsecure();
    HTTPClient h;
    if (!h.begin(client, cfg.api_base + "/audio/transcriptions")) { free(buf); return ""; }
    h.addHeader("Authorization", "Bearer " + cfg.api_key);
    h.addHeader("Content-Type", "multipart/form-data; boundary=foxB");
    int code = h.POST(buf, total);
    String out;
    if (code == 200) {
        JsonDocument r;
        if (deserializeJson(r, h.getString()) == DeserializationError::Ok)
            out = r["text"].as<String>();
    }
    h.end(); free(buf);
    out.trim();
    return out;
}

#include "fox_ir.inc"     // IR sweep + fake-learning (RMT TX on GPIO47)
#include "fox_face.inc"   // animated fox face + FFT lip-sync
#include "fox_input.inc"  // PTT capture, IMU gestures, menu, USB config

// ============================================================================
//  Command dispatch
// ============================================================================
static void speak(const String& fact) {
    FoxMood m = fox_mood(needs);
    String line = fox_dress(fact, m, cfg);
    face_caption(line);
    voice_say(line, m);
    needs_interact(needs, false);
    if (cfg.persistence) mem_append("say", line);
}

static void do_action(const char* action) {
    needs_interact(needs, false);
    if (!strcmp(action, "greet")) {
        struct tm t; getLocalTime(&t, 5);
        speak(fox_time_greeting(t.tm_hour));
    } else if (!strcmp(action, "time")) {
        struct tm t;
        if (getLocalTime(&t, 50)) {
            char b[32]; strftime(b, sizeof(b), "it's %I:%M %p", &t);
            speak(b);
        } else speak("i don't know the time yet");
    } else if (!strcmp(action, "mood")) {
        static const char* M[] = {"i'm sleepy", "i'm calm", "i'm happy", "i'm excited", "i'm a bit grumpy"};
        speak(M[fox_mood(needs)]);
    } else if (!strcmp(action, "weather")) {
        speak(fetch_weather());
    } else if (!strcmp(action, "ir_tv_power")) {
        ir_command("tv", "power");
    } else if (!strcmp(action, "ir_vol_up")) {
        ir_command("tv", "vol_up");
    } else if (!strcmp(action, "ir_vol_dn")) {
        ir_command("tv", "vol_dn");
    } else if (!strcmp(action, "play")) {
        needs_interact(needs, true);
        play_game();
    } else if (!strcmp(action, "sleep")) {
        speak("okay... good night");
        enter_light_sleep();
    } else if (!strcmp(action, "conv_on")) {
        cfg.conversation = true;
        speak("okay! i'm listening. talk to me~");
    } else if (!strcmp(action, "remember")) {
        speak("what should i remember? tell me~");
        // Next utterance is stored verbatim by the caller.
    } else if (!strcmp(action, "recall")) {
        String m = mem_tail(300);
        speak(m.length() ? "i remember: " + m : "we haven't made memories yet");
    }
}

// Turn a transcript into a reply. Cloud if available, else reflection/templates.
static void handle_free_text(const String& text) {
    if (!text.length()) { speak(fox_idle_line(fox_mood(needs))); return; }
    if (cfg.persistence) mem_append("you", text);
    String reply = cloud_chat(text);
    if (!reply.length()) {
        String low = text; low.toLowerCase();
        reply = fox_reflect(low);
    }
    speak(reply);
}

// ============================================================================
//  Main capture flow (push-to-talk)
// ============================================================================
static void process_utterance(int16_t* audio, size_t n) {
    // Try offline command grammar first (works with no network).
    int id = recognize_offline(audio, n);
    if (id >= 0) {
        for (size_t i = 0; i < COMMAND_COUNT; ++i)
            if (COMMANDS[i].id == id) { do_action(COMMANDS[i].action); return; }
    }
    // Not a known command: if cloud is on, transcribe + chat; else reflect.
    if (cfg.cloud_enabled) {
        String t = cloud_transcribe(audio, n);
        handle_free_text(t);
    } else {
        // Offline and unrecognised: acknowledge without pretending to understand.
        speak(fox_reflect(""));
    }
}

// ============================================================================
//  Arduino entry points
// ============================================================================
void setup() {
    auto c = M5.config();
    // Use M5Unified's own mic/speaker on the Echo Base; do NOT also start the
    // EchoBase library or the two I2S drivers fight over the bus.
    c.external_speaker.atomic_echo = true;
    M5.begin(c);
    Serial.begin(115200);
    M5.Speaker.setVolume(cfg.volume);

    load_config();
    M5.Speaker.setVolume(cfg.volume);

    // Time + power management
    setenv("TZ", cfg.timezone.c_str(), 1); tzset();

    mem_begin();
    voice_begin(cfg);
    face_begin(cfg);
    ir_begin();
    input_begin();

    if (cfg.wifi_enabled && !cfg.wifi_ssid.isEmpty()) {
        wifi_connect();
        configTime(0, 0, "pool.ntp.org", "time.google.com");
    }

    if (!init_speech())
        Serial.println("FOX: offline speech unavailable; cloud/reflection only");

    // Power-conscious defaults: throttle CPU; radios only when needed.
    if (!cfg.cloud_enabled) { WiFi.mode(WIFI_OFF); btStop(); }
    setCpuFrequencyMhz(160);

    needs.last_tick = millis();
    face_wake();
    speak(String("hi! i'm ") + cfg.name + "~");
}

static uint32_t last_activity = 0;
static uint32_t last_idle_chatter = 0;

void loop() {
    M5.update();
    needs_tick(needs);
    input_poll();          // handles USB serial config + IMU wake
    face_tick(fox_mood(needs));

    uint32_t now = millis();

    // --- Push-to-talk: hold USER button to talk ---------------------------
    ButtonEvent ev = input_button_event();
    if (ev == BTN_HOLD_START) {
        face_listen();
        size_t n = 0;
        int16_t* audio = capture_while_held(&n);   // returns on release / max
        face_think();
        if (audio && n > SAMPLE_RATE / 3) process_utterance(audio, n);
        if (audio) heap_caps_free(audio);
        last_activity = now;
    } else if (ev == BTN_TAP) {
        // FIX (bug #6): short tap now opens the real menu (was unimplemented).
        open_menu();
        last_activity = now;
    }

    // --- Conversation mode: listen in bursts, time out on silence ---------
    if (cfg.conversation) {
        size_t n = 0;
        int16_t* audio = capture_vad_burst(&n, CONVERSATION_TIMEOUT);
        if (audio && n > SAMPLE_RATE / 3) {
            face_think();
            process_utterance(audio, n);
            last_activity = now;
        } else {
            cfg.conversation = false;   // silence timeout ends conversation mode
            speak("okay, i'll be here if you need me~");
        }
        if (audio) heap_caps_free(audio);
    }

    // --- IMU gestures (tap to interact, shake to play) --------------------
    Gesture g = input_gesture();
    if (g == GST_TAP)   { needs_interact(needs, false); speak("boop!"); last_activity = now; }
    if (g == GST_SHAKE) { needs_interact(needs, true);  speak("wheee!"); last_activity = now; }

    // --- Idle chatter (fidgety companion) ---------------------------------
    if (now - last_activity > 20000 && now - last_idle_chatter > 25000) {
        FoxMood m = fox_mood(needs);
        String line = fox_idle_line(m);
        face_caption(line);
        voice_babble(m, 2 + (esp_random() % 3));   // babble, don't fake words
        last_idle_chatter = now;
    }

    // --- Sleep when idle a long time --------------------------------------
    if (now - last_activity > IDLE_SLEEP_MS) {
        enter_light_sleep();
        last_activity = millis();
    }

    delay(10);   // nap between frames to save power
}
