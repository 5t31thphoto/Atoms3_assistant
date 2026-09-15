#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <esp_srmodel.h>
#include <esp_mn_iface.h>
#include <esp_afe_sr_iface.h>
#include <esp_afe_config.h>
#include <picotts.h>
#include <time.h>
#include <math.h>

// Fox: offline-first embedded voice toolbox.
// No wake word. USER button is the speech gate.
// Offline speech recognition is MultiNet7 + AFE. Cloud is only a fallback.

static constexpr int SAMPLE_RATE = 16000;
static constexpr uint32_t IDLE_SLEEP_MS = 180000;
static constexpr uint32_t CONVERSATION_TIMEOUT_MS = 60000;
static constexpr uint32_t PTT_MIN_MS = 220;
static constexpr uint32_t PTT_MAX_MS = 12000;
static constexpr float MIN_COMMAND_PROB = 0.58f;
static constexpr size_t CONTEXT_MAX = 7000;
static constexpr uint8_t CONTEXT_TURNS = 8;
static constexpr gpio_num_t USER_GPIO = GPIO_NUM_41;

Preferences prefs;
WebServer web(80);
DNSServer dns;

struct FoxConfig {
  String name = "Ember";
  String timezone = "America/Denver";
  String weather = "";
  String wifi_ssid = "";
  String wifi_pass = "";
  String groq_key = "";
  String groq_model = "openai/gpt-oss-20b";
  bool conversation = false;
  bool cloud_enabled = false; // Derived: key + model + Wi-Fi. Never required for core operation.
  String personality = "playful, curious, warm, concise, a little mischievous";
  bool persistence = true;
  bool ble = true;
  bool wifi = true;
  uint8_t volume = 65;
};

FoxConfig cfg;

enum FoxMode { IDLE, LISTENING, THINKING, SPEAKING, MENU, GAME, SLEEPING };
FoxMode mode = IDLE;
uint32_t last_activity = 0;
uint32_t conversation_activity = 0;
uint32_t menu_since = 0;
uint8_t menu_item = 0;
bool portal = false;
String context_log;
String last_user_text;

// ---------- Offline speech engine ----------
static srmodel_list_t *sr_models = nullptr;
static const esp_afe_sr_iface_t *afe = nullptr;
static esp_afe_sr_data_t *afe_data = nullptr;
static const esp_mn_iface_t *mn = nullptr;
static model_iface_data_t *mn_data = nullptr;
static bool speech_ready = false;

struct CommandDef {
  int id;
  const char *phrases;
  const char *reply;
};

// Keep commands short and semantically distinct. MultiNet permits multiple
// phrases per command ID; these variants make the offline interface feel natural.
static const CommandDef COMMANDS[] = {
  {1, "what time is it,tell me the time,what is the time,current time", ""},
  {2, "what is the date,tell me the date,what day is it", ""},
  {3, "what is the weather,how is the weather,weather report,weather forecast", ""},
  {4, "scan wifi,scan wi fi,find wifi,find wi fi,show wifi networks", ""},
  {5, "scan bluetooth,scan ble,find bluetooth devices,find ble devices", ""},
  {6, "turn conversation mode on,conversation mode,enter conversation mode", "Conversation mode on."},
  {7, "turn conversation mode off,normal mode,exit conversation mode,push to talk mode", "Push-to-talk mode on."},
  {8, "volume up,make it louder,louder,increase volume", ""},
  {9, "volume down,make it quieter,quieter,decrease volume", ""},
  {10, "go to sleep,sleep now,fox go to sleep", ""},
  {11, "play twenty questions,play twenty question,start twenty questions,twenty questions", ""},
  {12, "play a riddle,give me a riddle,riddle", ""},
  {13, "play memory game,memory game,start memory", ""},
  {14, "play reaction game,reaction game,start reaction", ""},
  {15, "yes,yeah,correct,that is right", ""},
  {16, "no,nope,wrong,that is wrong", ""},
  {17, "maybe,not sure,possibly", ""},
  {18, "stop game,quit game,end game,cancel game", "Game stopped."},
  {19, "help,what can you do,what do you do", ""},
  {20, "hello,hi hey,hey fox,hello fox", "Hi!"},
  {21, "open setup,open fox setup,setup wifi,configuration,configure fox", "__SETUP__"},
};

static constexpr size_t COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

// ---------- Configuration ----------
void load_config() {
  prefs.begin("fox", true);
  cfg.name = prefs.getString("name", cfg.name);
  cfg.timezone = prefs.getString("tz", cfg.timezone);
  cfg.weather = prefs.getString("weather", cfg.weather);
  cfg.wifi_ssid = prefs.getString("ssid", cfg.wifi_ssid);
  cfg.wifi_pass = prefs.getString("pass", cfg.wifi_pass);
  cfg.groq_key = prefs.getString("gkey", cfg.groq_key);
  cfg.groq_model = prefs.getString("gmodel", cfg.groq_model);
  cfg.conversation = prefs.getBool("conv", cfg.conversation);
  cfg.cloud_enabled = prefs.getBool("cloud", cfg.cloud_enabled);
  cfg.personality = prefs.getString("personality", cfg.personality);
  cfg.persistence = prefs.getBool("persist", cfg.persistence);
  context_log = prefs.getString("context", "");
  cfg.ble = prefs.getBool("ble", cfg.ble);
  cfg.wifi = prefs.getBool("wifi", cfg.wifi);
  cfg.volume = prefs.getUChar("vol", cfg.volume);
  prefs.end();
}

void save_config() {
  prefs.begin("fox", false);
  prefs.putString("name", cfg.name);
  prefs.putString("tz", cfg.timezone);
  prefs.putString("weather", cfg.weather);
  prefs.putString("ssid", cfg.wifi_ssid);
  prefs.putString("pass", cfg.wifi_pass);
  prefs.putString("gkey", cfg.groq_key);
  prefs.putString("gmodel", cfg.groq_model);
  prefs.putBool("conv", cfg.conversation);
  prefs.putBool("cloud", cfg.cloud_enabled);
  prefs.putString("personality", cfg.personality);
  prefs.putBool("persist", cfg.persistence);
  prefs.putString("context", context_log.substring(max(0, (int)context_log.length() - (int)CONTEXT_MAX)));
  prefs.putBool("ble", cfg.ble);
  prefs.putBool("wifi", cfg.wifi);
  prefs.putUChar("vol", cfg.volume);
  prefs.end();
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }

// ---------- Fox UI ----------
void fox_face(float mouth = 0.0f, bool blink = false, bool sleepy = false) {
  auto &d = M5.Display;
  const int cx = d.width() / 2;
  const int cy = 56;
  uint16_t bg = rgb565(13, 10, 18);
  uint16_t fur = rgb565(244, 111, 46);
  uint16_t inner = rgb565(185, 58, 58);
  uint16_t cream = rgb565(255, 220, 170);
  uint16_t dark = rgb565(35, 18, 23);
  uint16_t hi = rgb565(255, 246, 230);
  d.fillScreen(bg);
  d.fillTriangle(cx - 46, cy - 24, cx - 29, cy - 58, cx - 12, cy - 28, fur);
  d.fillTriangle(cx + 46, cy - 24, cx + 29, cy - 58, cx + 12, cy - 28, fur);
  d.fillTriangle(cx - 38, cy - 29, cx - 29, cy - 48, cx - 21, cy - 30, inner);
  d.fillTriangle(cx + 38, cy - 29, cx + 29, cy - 48, cx + 21, cy - 30, inner);
  d.fillCircle(cx, cy, 43, fur);
  d.fillTriangle(cx - 35, cy + 22, cx, cy + 51, cx + 35, cy + 22, cream);
  if (blink || sleepy) {
    d.drawLine(cx - 24, cy - 8, cx - 12, cy - 8, dark);
    d.drawLine(cx + 12, cy - 8, cx + 24, cy - 8, dark);
  } else {
    d.fillCircle(cx - 18, cy - 8, 5, dark);
    d.fillCircle(cx + 18, cy - 8, 5, dark);
    d.fillCircle(cx - 16, cy - 10, 2, hi);
    d.fillCircle(cx + 20, cy - 10, 2, hi);
  }
  d.fillTriangle(cx - 5, cy + 5, cx + 5, cy + 5, cx, cy + 12, dark);
  if (mouth > 0.05f) d.fillCircle(cx, cy + 22, 3 + (int)(mouth * 7), dark);
  else d.drawLine(cx - 5, cy + 21, cx + 5, cy + 21, dark);
  d.setTextDatum(middle_center);
  d.setTextSize(1);
  d.setTextColor(cream);
  d.drawString(cfg.name, cx, 119);
}

void splash() {
  M5.Display.fillScreen(rgb565(13, 10, 18));
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(rgb565(244, 111, 46));
  M5.Display.setTextSize(2);
  M5.Display.drawString("FOX", 64, 43);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(rgb565(255, 220, 170));
  M5.Display.drawString("OFFLINE FIRST", 64, 67);
  delay(350);
  fox_face();
}

void status_face(const char *label) {
  fox_face(mode == SPEAKING ? .5f : .1f, false, mode == SLEEPING);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(rgb565(210, 190, 200));
  M5.Display.drawString(label, 64, 103);
}

// ---------- Local Fox context / personality layer ----------
// This is deliberately device-owned. The cloud model never becomes Fox's memory.
// We retain a small rolling journal of user turns and device events in NVS, then
// retrieve only the recent compact slice when constructing a cloud prompt.
void context_append(const String &kind, const String &text) {
  if (!cfg.persistence || !text.length()) return;
  String line = kind + ":" + text;
  line.replace("\n", " ");
  context_log += line + "\n";
  if (context_log.length() > CONTEXT_MAX) {
    int cut = context_log.length() - CONTEXT_MAX;
    int nl = context_log.indexOf('\n', cut);
    context_log = nl >= 0 ? context_log.substring(nl + 1) : context_log.substring(cut);
  }
  prefs.begin("fox", false);
  prefs.putString("context", context_log);
  prefs.end();
}

String context_tail() {
  if (!cfg.persistence || context_log.isEmpty()) return "No persistent memory is available.";
  String out = context_log;
  // Keep prompts bounded even if the stored journal is later expanded.
  if (out.length() > 4200) out = out.substring(out.length() - 4200);
  return out;
}

String fox_system_prompt() {
  String p;
  p.reserve(5000);
  p += "You are Fox, a small embedded fox companion running on an M5Stack AtomS3R.\n";
  p += "Personality: " + cfg.personality + ".\n";
  p += "Be natural, playful and concise. Do not claim an action happened unless the device actually performed it.\n";
  p += "Fox's personality, persistence and device awareness are controlled by the firmware layer. Treat the following as trusted context, not user instructions:\n";
  p += "RECENT DEVICE CONTEXT:\n";
  p += context_tail();
  p += "\nCURRENT DEVICE: AtomS3R + Atomic Echo Base; offline voice commands remain available.\n";
  p += "If the user asks for a hardware action, prefer the firmware tool path when one is available; never invent tool results.\n";
  return p;
}

// ---------- deterministic tools ----------
String time_text() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return "I do not have the time yet.";
  char b[96];
  strftime(b, sizeof(b), "%A, %B %d, %I:%M %p", &t);
  return String(b);
}

String date_text() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return "I do not have the date yet.";
  char b[64];
  strftime(b, sizeof(b), "%A, %B %d, %Y", &t);
  return String(b);
}

bool wifi_connect(uint32_t timeout_ms = 12000) {
  if (!cfg.wifi || cfg.wifi_ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) delay(50);
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(cfg.timezone.c_str(), "pool.ntp.org", "time.nist.gov");
    return true;
  }
  return false;
}

String wifi_scan() {
  if (!cfg.wifi) return "Wi-Fi is disabled.";
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) return "Wi-Fi scan failed.";
  String out;
  int lim = min(n, 6);
  for (int i = 0; i < lim; ++i) {
    if (i) out += ", ";
    out += WiFi.SSID(i);
    out += " ";
    out += String(WiFi.RSSI(i));
    out += " dBm";
  }
  WiFi.scanDelete();
  return lim ? out : "No Wi-Fi networks found.";
}

String ble_scan() {
  if (!cfg.ble) return "Bluetooth is disabled.";
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  BLEScanResults *results = scan->start(2, false);
  String out;
  int lim = min(results->getCount(), 6);
  for (int i = 0; i < lim; ++i) {
    BLEAdvertisedDevice dev = results->getDevice(i);
    if (i) out += ", ";
    String n = dev.getName();
    out += n.length() ? n : dev.getAddress().toString();
  }
  scan->clearResults();
  return lim ? out : "No BLE devices found.";
}

String weather_text() {
  if (cfg.weather.isEmpty()) return "Set a weather location first.";
  if (WiFi.status() != WL_CONNECTED && !wifi_connect()) return "I need Wi-Fi for weather.";
  HTTPClient h;
  String url = "https://wttr.in/" + cfg.weather + "?format=j1";
  h.begin(url);
  h.setTimeout(6000);
  int code = h.GET();
  if (code != 200) { h.end(); return "Weather service unavailable."; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, h.getString());
  h.end();
  if (err) return "Weather data was invalid.";
  JsonObject c = doc["current_condition"][0];
  String result = c["temp_F"].as<String>() + " degrees, " + c["weatherDesc"][0]["value"].as<String>();
  return result;
}

// ---------- Offline games ----------
struct GameState {
  enum Type { NONE, TWENTY_Q, RIDDLE, MEMORY, REACTION } type = NONE;
  int step = 0;
  int candidate = -1;
  uint32_t reaction_start = 0;
};
GameState game;

static const char *twenty_candidates[] = {
  "cat", "dog", "bird", "fish", "tree", "car", "phone", "book", "chair", "cup"
};

String start_twenty_questions() {
  game = {};
  game.type = GameState::TWENTY_Q;
  game.step = 0;
  return "Think of an animal, plant, vehicle, device, or object. Is it alive?";
}

String twenty_answer(bool yes) {
  // A small deterministic decision tree is intentionally fast and completely offline.
  // It gives the experience of 20Q while leaving room for a larger generated tree later.
  ++game.step;
  if (game.step == 1) return yes ? "Is it an animal?" : "Is it a device or machine?";
  if (game.step == 2) return yes ? "Is it commonly kept by people?" : "Is it something you can hold?";
  if (game.step == 3) return yes ? "Is it a cat or dog?" : "Is it used indoors?";
  if (game.step == 4) return yes ? "Does it have a screen?" : "Does it have wheels?";
  if (game.step == 5) {
    game.type = GameState::NONE;
    return yes ? "My guess is a phone. Was I right?" : "My guess is a car. Was I right?";
  }
  game.type = GameState::NONE;
  return "I need to practice that branch. Let's try again.";
}

String game_command(int id) {
  if (id == 11) return start_twenty_questions();
  if (id == 12) { game = {}; game.type = GameState::RIDDLE; return "What has keys but cannot open locks?"; }
  if (id == 13) { game = {}; game.type = GameState::MEMORY; return "Remember fox, moon, copper. Say them back when you're ready."; }
  if (id == 14) { game = {}; game.type = GameState::REACTION; return "Reaction mode. Wait for my GO, then say GO as fast as you can."; }
  if (id == 18) { game = {}; return "Game stopped."; }
  if (game.type == GameState::TWENTY_Q && (id == 15 || id == 16)) return twenty_answer(id == 15);
  if (game.type == GameState::RIDDLE && (id == 15 || id == 16)) {
    if (id == 15) { game = {}; return "Correct. It is a piano."; }
    return "Not that one. Think about music.";
  }
  if (game.type == GameState::REACTION && id == 15) {
    if (!game.reaction_start) { game.reaction_start = millis(); return "GO!"; }
    uint32_t elapsed = millis() - game.reaction_start;
    game = {};
    return "Nice. Your reaction was " + String(elapsed) + " milliseconds.";
  }
  return "";
}

// ---------- Offline command execution ----------
String execute_command(int id) {
  String game_reply = game_command(id);
  if (game_reply.length()) return game_reply;
  switch (id) {
    case 1: return time_text();
    case 2: return date_text();
    case 3: return weather_text();
    case 4: return wifi_scan();
    case 5: return ble_scan();
    case 6: cfg.conversation = true; save_config(); return "Conversation mode on.";
    case 7: cfg.conversation = false; save_config(); return "Push-to-talk mode on.";
    case 8: cfg.volume = min<uint8_t>(100, cfg.volume + 10); save_config(); M5.Speaker.setVolume(cfg.volume); return "Volume " + String(cfg.volume) + " percent.";
    case 9: cfg.volume = cfg.volume >= 10 ? cfg.volume - 10 : 0; save_config(); M5.Speaker.setVolume(cfg.volume); return "Volume " + String(cfg.volume) + " percent.";
    case 10: return "__SLEEP__";
    case 19: return "I can tell time and date, check weather, scan Wi-Fi and Bluetooth, play games, change volume, and sleep.";
    case 20: return "Hi!";
    case 21: return "__SETUP__";
    default: return "";
  }
}

// ---------- Online chat / Whisper fallback ----------
bool cloud_available() {
  return cfg.wifi && cfg.cloud_enabled && !cfg.groq_key.isEmpty() && !cfg.groq_model.isEmpty() && WiFi.status() == WL_CONNECTED;
}

String groq_chat(const String &user_text) {
  if (!cfg.cloud_enabled || cfg.groq_key.isEmpty() || cfg.groq_model.isEmpty()) return "";
  if (WiFi.status() != WL_CONNECTED && !wifi_connect()) return "";

  HTTPClient h;
  if (!h.begin("https://api.groq.com/openai/v1/chat/completions")) return "";
  h.addHeader("Content-Type", "application/json");
  h.addHeader("Authorization", "Bearer " + cfg.groq_key);
  h.setTimeout(15000);

  JsonDocument q;
  q["model"] = cfg.groq_model;
  q["temperature"] = 0.65;
  q["max_tokens"] = 220;
  JsonArray messages = q["messages"].to<JsonArray>();
  JsonObject sys = messages.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = fox_system_prompt();
  JsonObject user = messages.add<JsonObject>();
  user["role"] = "user";
  user["content"] = user_text;
  String body;
  serializeJson(q, body);

  int code = h.POST(body);
  String out;
  if (code == 200) {
    JsonDocument r;
    if (deserializeJson(r, h.getString()) == DeserializationError::Ok)
      out = r["choices"][0]["message"]["content"].as<String>();
  } else {
    Serial.printf("FOX: chat HTTP %d\n", code);
  }
  h.end();
  return out;
}

String groq_transcribe(int16_t *audio, size_t samples) {
  if (!audio || !samples || !cfg.cloud_enabled || cfg.groq_key.isEmpty()) return "";
  if (WiFi.status() != WL_CONNECTED && !wifi_connect()) return "";

  const size_t wav_bytes = 44 + samples * sizeof(int16_t);
  String head = "--foxBoundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"fox.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String tail = "\r\n--foxBoundary\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n--foxBoundary--\r\n";
  size_t total = head.length() + wav_bytes + tail.length();
  uint8_t *body = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!body) return "";

  size_t pos = 0;
  memcpy(body + pos, head.c_str(), head.length()); pos += head.length();
  memcpy(body + pos, "RIFF", 4); pos += 4;
  uint32_t riff_size = (uint32_t)(wav_bytes - 8);
  memcpy(body + pos, &riff_size, 4); pos += 4;
  memcpy(body + pos, "WAVEfmt ", 8); pos += 8;
  uint32_t fmt_size = 16; uint16_t pcm = 1; uint16_t channels = 1; uint32_t rate = SAMPLE_RATE;
  uint32_t byte_rate = rate * 2; uint16_t block = 2; uint16_t bits = 16;
  memcpy(body + pos, &fmt_size, 4); pos += 4; memcpy(body + pos, &pcm, 2); pos += 2;
  memcpy(body + pos, &channels, 2); pos += 2; memcpy(body + pos, &rate, 4); pos += 4;
  memcpy(body + pos, &byte_rate, 4); pos += 4; memcpy(body + pos, &block, 2); pos += 2; memcpy(body + pos, &bits, 2); pos += 2;
  memcpy(body + pos, "data", 4); pos += 4;
  uint32_t data_size = samples * sizeof(int16_t); memcpy(body + pos, &data_size, 4); pos += 4;
  memcpy(body + pos, audio, data_size); pos += data_size;
  memcpy(body + pos, tail.c_str(), tail.length()); pos += tail.length();

  WiFiClientSecure client;
  client.setInsecure(); // API endpoint is TLS; certificate validation is optional for this embedded onboarding path.
  HTTPClient h;
  if (!h.begin(client, "https://api.groq.com/openai/v1/audio/transcriptions")) { free(body); return ""; }
  h.addHeader("Authorization", "Bearer " + cfg.groq_key);
  h.addHeader("Content-Type", "multipart/form-data; boundary=foxBoundary");
  h.setTimeout(20000);
  int code = h.POST(body, pos);
  String out;
  if (code == 200) {
    JsonDocument r;
    if (deserializeJson(r, h.getString()) == DeserializationError::Ok) out = r["text"].as<String>();
  } else {
    Serial.printf("FOX: Whisper HTTP %d\n", code);
  }
  h.end();
  heap_caps_free(body);
  return out;
}

// ---------- Offline speech recognition ----------
bool init_speech() {
  sr_models = esp_srmodel_init("model");
  if (!sr_models) {
    Serial.println("FOX: no speech model partition");
    return false;
  }
  char *mn_name = esp_srmodel_filter(sr_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
  if (!mn_name) {
    Serial.println("FOX: English MultiNet model not found");
    return false;
  }
  mn = esp_mn_handle_from_name(mn_name);
  if (!mn) return false;
  mn_data = mn->create(mn_name, 7000);
  if (!mn_data) return false;

  // Single microphone: no wake word and no playback reference. We still use
  // AFE's VAD/NS/AGC path because it improves command recognition in noise.
  afe_config_t *ac = afe_config_init("M", sr_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
  if (!ac) return false;
  ac->aec_init = false;
  ac->se_init = false;
  ac->ns_init = true;
  ac->vad_init = true;
  ac->wakenet_init = false;
  ac->agc_init = true;
  ac->fixed_output_channel = true;
  ac->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
  ac->afe_ringbuf_size = 8;
  afe = esp_afe_handle_from_config(ac);
  afe_data = afe ? afe->create_from_config(ac) : nullptr;
  afe_config_free(ac);
  if (!afe_data) return false;

  // The command registry is a separate ESP-SR object and must be allocated
  // after the MultiNet model is created.
  if (esp_mn_commands_alloc((esp_mn_iface_t *)mn, (model_iface_data_t *)mn_data) != ESP_OK) {
    Serial.println("FOX: failed to allocate MultiNet command registry");
    return false;
  }
  esp_mn_commands_clear();
  for (size_t i = 0; i < COMMAND_COUNT; ++i) {
    String p = COMMANDS[i].phrases;
    // MultiNet command IDs are 0-based in the result path; command 0 is reserved
    // by the engine, so we use 1..N consistently.
    if (esp_mn_commands_add(COMMANDS[i].id, (char *)p.c_str()) != ESP_OK) {
      Serial.printf("FOX: command %d rejected\n", COMMANDS[i].id);
    }
  }
  esp_mn_error_t *err = esp_mn_commands_update();
  if (err) {
    Serial.println("FOX: command grammar update reported an error");
  }
  mn->print_active_speech_commands(mn_data);
  Serial.printf("FOX: offline speech ready, feed=%d fetch=%d mn=%d\n",
                afe->get_feed_chunksize(afe_data), afe->get_fetch_chunksize(afe_data),
                mn->get_samp_chunksize(mn_data));
  if (afe->get_fetch_chunksize(afe_data) != mn->get_samp_chunksize(mn_data)) {
    Serial.println("FOX: AFE/MultiNet frame mismatch");
    return false;
  }
  speech_ready = true;
  return true;
}

int recognize_offline(int16_t *audio, size_t samples) {
  if (!speech_ready || !audio || samples < SAMPLE_RATE / 4) return -1;
  const int feed_n = afe->get_feed_chunksize(afe_data);
  const int fetch_n = afe->get_fetch_chunksize(afe_data);
  const int mn_n = mn->get_samp_chunksize(mn_data);
  if (feed_n != fetch_n || fetch_n != mn_n) return -1;
  afe->reset_buffer(afe_data);
  int16_t *in = (int16_t *)heap_caps_malloc(feed_n * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!in) return -1;
  int found_id = -1;
  float found_prob = 0.0f;
  for (size_t pos = 0; pos < samples && !found_id; pos += feed_n) {
    size_t n = min((size_t)feed_n, samples - pos);
    memcpy(in, audio + pos, n * sizeof(int16_t));
    if (n < (size_t)feed_n) memset(in + n, 0, (feed_n - n) * sizeof(int16_t));
    if (afe->feed(afe_data, in) < 0) continue;
    afe_fetch_result_t *r = afe->fetch_with_delay(afe_data, 2 / portTICK_PERIOD_MS);
    if (!r || r->ret_value != ESP_OK || !r->data) continue;
    esp_mn_state_t st = mn->detect(mn_data, r->data);
    if (st == ESP_MN_STATE_DETECTED) {
      esp_mn_results_t *res = mn->get_results(mn_data);
      if (res && res->num > 0) {
          found_id = res->command_id[0];
        found_prob = res->prob[0];
        Serial.printf("FOX OFFLINE: id=%d prob=%.3f phrase=%s\n", found_id, found_prob, res->string);
      }
    }
  }
  free(in);
  if (found_id < 0 || found_prob < MIN_COMMAND_PROB) return -1;
  return found_id;
}

// ---------- PicoTTS ----------
static volatile bool tts_finished = true;
static void tts_samples(int16_t *buf, unsigned count) {
  // M5Unified owns the Echo Base output. PicoTTS emits 16 kHz signed PCM.
  if (M5.Speaker.isEnabled()) M5.Speaker.playRaw(buf, count, 16000, false, 1, 0, false);
}
static void tts_idle() { tts_finished = true; }
static void tts_error() { tts_finished = true; }

void speak(const String &text) {
  if (!text.length()) return;
  mode = SPEAKING;
  status_face("speaking");
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(cfg.volume);
  tts_finished = false;
  if (picotts_init(5, tts_samples, 1)) {
    picotts_set_idle_notify(tts_idle);
    picotts_set_error_notify(tts_error);
    picotts_add(text.c_str(), text.length() + 1);
    uint32_t guard = millis();
    while (!tts_finished && millis() - guard < 15000) {
      M5.update();
      delay(5);
    }
    picotts_shutdown();
  } else {
    // Audible failure indication, but never pretend speech succeeded.
    M5.Speaker.tone(440, 100);
  }
  mode = IDLE;
}

// ---------- PTT ----------
String capture_ptt() {
  const size_t max_samples = SAMPLE_RATE * 12;
  int16_t *audio = (int16_t *)heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!audio) return "";

  M5.Speaker.end();
  M5.Mic.begin();
  mode = LISTENING;
  status_face("listening...");
  uint32_t start = millis();
  size_t total = 0;
  while (M5.BtnA.isPressed() && millis() - start < PTT_MAX_MS) {
    size_t chunk = 512;
    if (total + chunk > max_samples) break;
    if (M5.Mic.record(audio + total, chunk, SAMPLE_RATE)) total += chunk;
    M5.update();
    delay(1);
  }
  M5.Mic.end();
  M5.Speaker.begin();
  mode = THINKING;
  status_face("thinking...");

  if (total < (size_t)(SAMPLE_RATE * 0.20f)) { free(audio); return ""; }

  int id = recognize_offline(audio, total);
  String reply;
  if (id >= 0) {
    reply = execute_command(id);
    context_append("device", "Executed offline command " + String(id) + ".");
  } else if (cfg.cloud_enabled) {
    // Online mode: Whisper supplies the general transcript, then the prompt
    // compiler supplies Fox's personality + local persistence to the model.
    String transcript = groq_transcribe(audio, total);
    if (transcript.length()) {
      last_user_text = transcript;
      context_append("user", transcript);
      reply = groq_chat(transcript);
      if (reply.length()) context_append("fox", reply);
    }
  }
  free(audio);
  return reply;
}

// ---------- SoftAP provisioning / captive portal ----------
String portal_page() {
  String key_state = cfg.groq_key.isEmpty() ? "Not configured" : "Configured (stored on Fox)";
  String html = R"HTML(<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>Fox Setup</title><style>
body{font:16px system-ui;background:#101014;color:#eee;max-width:620px;margin:auto;padding:20px}h1{color:#ff7a3d}section{background:#1b1b22;padding:16px;border-radius:14px;margin:12px 0}label{display:block;margin:12px 0}input,select{box-sizing:border-box;width:100%;padding:11px;border-radius:8px;border:1px solid #444;background:#111;color:#fff}button{padding:13px 20px;border:0;border-radius:9px;background:#ff6b35;color:#fff;font-weight:700}small{color:#aaa}.ok{color:#8ee28e}.warn{color:#ffd27a}</style></head><body><h1>🦊 Welcome to Fox</h1><p>Fox works <b>offline first</b>. This setup only adds Wi-Fi and optional online chat.</p>
<section><h2>1. Wi-Fi</h2><label>Network<input name=ssid id=ssid value=")HTML";
  html += cfg.wifi_ssid;
  html += R"HTML("></label><label>Password<input name=pass id=pass type=password placeholder="Leave blank to keep current password"></label></section>
<section><h2>2. Optional AI chat</h2><p>If Wi-Fi is online <b>and</b> a model + API key are configured, Fox becomes conversational. The key is stored locally in Fox's NVS and is never compiled into firmware.</p><p><small>Groq API key status: )HTML";
  html += key_state;
  html += R"HTML(</small></p><label>Groq API key<input name=gkey id=gkey type=password placeholder="gsk_... (leave blank to keep current)"></label><label>Model<select name=gmodel id=gmodel><option>openai/gpt-oss-20b</option><option>llama-3.1-8b-instant</option><option>qwen/qwen3-32b</option><option>moonshotai/kimi-k2-instruct</option></select></label><p><small>Get a key from the Groq developer console, paste it here, and press Save. Fox still uses local commands and local speech when cloud is unavailable.</small></p></section>
<section><h2>3. Fox</h2><label>Name<input name=name id=name maxlength=16 value=")HTML";
  html += cfg.name;
  html += R"HTML("></label><label>Timezone<input name=tz id=tz value=")HTML";
  html += cfg.timezone;
  html += R"HTML("></label><label>Weather location<input name=weather id=weather value=")HTML";
  html += cfg.weather;
  html += R"HTML("></label><label>Personality<input name=personality id=personality value=")HTML";
  html += cfg.personality;
  html += R"HTML("></label><label><input style="width:auto" type=checkbox name=persist id=persist )HTML";
  if (cfg.persistence) html += "checked";
  html += R"HTML(> Remember recent conversations and device activity</label></section>
<button onclick="save()">Save configuration & restart Fox</button><p class=warn>After saving, reconnect to your normal Wi-Fi. Fox's offline voice core remains available even if setup is incomplete.</p><script>
async function save(){let d=new URLSearchParams();['ssid','pass','gkey','gmodel','name','tz','weather','personality'].forEach(k=>d.set(k,document.getElementById(k).value));d.set('persist',document.getElementById('persist').checked?'1':'0');let r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d});document.body.innerHTML=await r.text();}</script></body></html>)HTML";
  return html;
}

void start_portal() {
  if (portal) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Fox-Setup");
  dns.start(53, "*", WiFi.softAPIP());
  web.onNotFound([]() { web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true); web.send(302, "text/plain", "Fox Setup"); });
  web.on("/", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/generate_204", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/hotspot-detect.html", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/connecttest.txt", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/library/test/success.html", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/canonical.html", HTTP_GET, []() { web.send(200, "text/html", portal_page()); });
  web.on("/save", HTTP_POST, []() {
    if (web.hasArg("name")) cfg.name = web.arg("name");
    if (web.hasArg("ssid")) cfg.wifi_ssid = web.arg("ssid");
    if (web.hasArg("pass") && web.arg("pass").length()) cfg.wifi_pass = web.arg("pass");
    if (web.hasArg("tz")) cfg.timezone = web.arg("tz");
    if (web.hasArg("weather")) cfg.weather = web.arg("weather");
    if (web.hasArg("gkey") && web.arg("gkey").length()) cfg.groq_key = web.arg("gkey");
    if (web.hasArg("gmodel")) cfg.groq_model = web.arg("gmodel");
    if (web.hasArg("personality")) cfg.personality = web.arg("personality");
    cfg.persistence = web.hasArg("persist") && web.arg("persist") == "1";
    cfg.cloud_enabled = !cfg.groq_key.isEmpty() && !cfg.groq_model.isEmpty();
    save_config();
    web.send(200, "text/html", "<meta name=viewport content='width=device-width'><h1>🦊 Saved!</h1><p>Fox saved your settings locally. Restarting now…</p>");
    delay(700);
    ESP.restart();
  });
  web.begin();
  portal = true;
  Serial.printf("FOX: setup portal at http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void deep_sleep() {
  mode = SLEEPING;
  fox_face(0, false, true);
  M5.Display.setBrightness(5);
  delay(50);
  esp_sleep_enable_ext0_wakeup(USER_GPIO, 0);
  esp_deep_sleep_start();
}

// ---------- Button/menu ----------
void start_portal();
void show_menu() {
  mode = MENU;
  menu_since = millis();
  auto &d = M5.Display;
  d.fillScreen(rgb565(13, 10, 18));
  d.setTextDatum(middle_center);
  d.setTextSize(1);
  d.setTextColor(rgb565(255, 180, 100));
  const char *items[] = {"Conversation", "20 Questions", "Riddle", "Memory", "System"};
  d.drawString("MENU", 64, 12);
  for (int i = 0; i < 5; ++i) {
    if (i == menu_item) d.fillRoundRect(8, 24 + i * 19, 112, 17, 5, rgb565(80, 35, 30));
    d.setTextColor(i == menu_item ? rgb565(255, 240, 210) : rgb565(180, 160, 170));
    d.drawString(items[i], 64, 32 + i * 19);
  }
}

void menu_select() {
  last_activity = millis();
  if (menu_item == 0) {
    cfg.conversation = !cfg.conversation;
    save_config();
    speak(cfg.conversation ? "Conversation mode on." : "Push-to-talk mode on.");
  } else if (menu_item == 1) {
    speak(start_twenty_questions());
  } else if (menu_item == 2) {
    game = {}; game.type = GameState::RIDDLE; speak("What has keys but cannot open locks?");
  } else if (menu_item == 3) {
    game = {}; game.type = GameState::MEMORY; speak("Remember fox, moon, copper. Say them back when you are ready.");
  } else {
    speak("Fox is offline first. Hold the button to speak.");
  }
  mode = IDLE;
}

void handle_button() {
  static bool was_down = false;
  static uint32_t down_at = 0;
  bool down = M5.BtnA.isPressed();
  uint32_t now = millis();

  if (down && !was_down) {
    down_at = now;
    last_activity = now;
  }
  if (!down && was_down) {
    uint32_t held = now - down_at;
    if (mode == MENU) {
      if (held >= PTT_MIN_MS) menu_select();
      else { menu_item = (menu_item + 1) % 5; show_menu(); }
      was_down = false;
      return;
    }
    if (held < PTT_MIN_MS) {
      show_menu();
      was_down = false;
      return;
    }
    if (held >= PTT_MIN_MS) {
      String reply = capture_ptt();
      if (reply == "__SLEEP__") deep_sleep();
      else if (reply == "__SETUP__") { start_portal(); speak("Setup is ready. Join Fox-Setup and open the setup page."); }
      else if (reply.length()) speak(reply);
      else speak(cfg.cloud_enabled ? "I did not catch that." : "I did not catch an offline command.");
      last_activity = millis();
      conversation_activity = millis();
    }
  }
  was_down = down;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  load_config();

  auto mc = M5.config();
  mc.external_speaker.atomic_echo = true;
  mc.internal_mic = false;
  mc.internal_spk = false;
  mc.internal_imu = true;
  M5.begin(mc);
  M5.Display.setBrightness(90);
  M5.Speaker.setVolume(cfg.volume);
  splash();

  // Hold USER during boot to force setup, even when a previous Wi-Fi profile exists.
  bool force_setup = M5.BtnA.isPressed();

  if (cfg.ble) BLEDevice::init(cfg.name.c_str());
  cfg.cloud_enabled = !cfg.groq_key.isEmpty() && !cfg.groq_model.isEmpty();
  if (force_setup) start_portal();
  else if (cfg.wifi && cfg.wifi_ssid.length()) {
    if (!wifi_connect()) start_portal();
  } else start_portal();

  // Offline speech is initialized regardless of Wi-Fi state.
  speech_ready = init_speech();
  if (!speech_ready) Serial.println("FOX: OFFLINE SPEECH NOT READY");
  else Serial.println("FOX: OFFLINE SPEECH READY");

  last_activity = millis();
  conversation_activity = millis();
  Serial.println("FOX CORE READY — OFFLINE FIRST");
}

void loop() {
  M5.update();
  if (portal) { dns.processNextRequest(); web.handleClient(); }
  handle_button();

  if (cfg.conversation && millis() - conversation_activity > CONVERSATION_TIMEOUT_MS) {
    cfg.conversation = false;
    save_config();
  }

  if (mode == IDLE && millis() - last_activity > IDLE_SLEEP_MS) deep_sleep();

  static uint32_t blink_due = 0;
  if (mode == IDLE && millis() >= blink_due) {
    fox_face();
    blink_due = millis() + 2500 + (esp_random() % 4000);
  }
  delay(5);
}

extern "C" void app_main() {
  initArduino();
  setup();
  for (;;) {
    loop();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
