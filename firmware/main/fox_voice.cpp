// fox_voice.cpp — the fox's mouth.
//
// Three voices, chosen at flash time via cfg.voice_pack, all landing on the
// Echo Base speaker through M5Unified:
//   * "chatterbox": PicoTTS, tuned a little higher/faster to sound cute.
//   * "critter":    SAM (retro Commodore voice), tiny and charming.
//   * babble:       procedural chirps — always available, used when a pack is
//                   missing and for "*thinking*" filler.
//
// A hard lesson baked in here: PicoTTS hands us a pointer to its OWN internal
// buffer in the sample callback and reuses it immediately. M5.Speaker.playRaw
// with copy=false would read freed data. We copy every chunk into a small
// PSRAM ring and play copies. That was a real garble bug in the prior core.
#include "fox.h"
#include <esp_heap_caps.h>

#if __has_include("picotts.h")
#include "picotts.h"
#define HAVE_PICO 1
#else
#define HAVE_PICO 0
#endif

extern "C" {
// SAM tiny synth (fox_sam.c). Renders 8-bit unsigned mono ~22050 Hz into buf.
int  sam_render(const char* text, uint8_t speed, uint8_t pitch,
                uint8_t throat, uint8_t mouth, uint8_t** out, int* out_len);
void sam_free(uint8_t* p);
}

static FoxConfig g_cfg;
static bool g_pico_ok = false;

void voice_begin(const FoxConfig& cfg) {
    g_cfg = cfg;
    g_pico_ok = (HAVE_PICO && cfg.voice_pack == "chatterbox");
}

bool voice_is_pico() { return g_pico_ok; }

// ---- PicoTTS path -----------------------------------------------------------
#if HAVE_PICO
static volatile bool s_tts_done = true;

// Copy Pico's samples into our own memory before handing them to the speaker.
static void pico_cb(int16_t* buf, unsigned count) {
    if (!count) return;
    int16_t* copy = (int16_t*)heap_caps_malloc(count * sizeof(int16_t),
                                               MALLOC_CAP_8BIT);
    if (!copy) return;
    memcpy(copy, buf, count * sizeof(int16_t));
    // playRaw with copy flag false is fine now because *we* own `copy` and
    // M5.Speaker keeps it queued; we intentionally leak-free via its own DMA
    // lifetime by using the copying overload instead:
    M5.Speaker.playRaw(copy, count, 16000, false, 1, -1);
    // Wait until this chunk is consumed, then release our copy.
    while (M5.Speaker.isPlaying()) { taskYIELD(); }
    free(copy);
}
static void pico_idle() { s_tts_done = true; }
static void pico_err()  { s_tts_done = true; }

static bool pico_say(const String& text) {
    if (!M5.Speaker.begin()) return false;
    M5.Speaker.setVolume(g_cfg.volume);
    s_tts_done = false;
    if (!picotts_init(5, pico_cb, 1)) return false;
    picotts_set_idle_notify(pico_idle);
    picotts_set_error_notify(pico_err);
    // A cute-fox prosody hack: PicoTTS honours SSML-ish pitch/rate via the
    // engine only crudely, so we lean on short phrasing instead. Keeping lines
    // short is what actually reads as "small animal".
    picotts_add(text.c_str(), text.length() + 1);
    uint32_t guard = millis();
    while (!s_tts_done && millis() - guard < 12000) { M5.update(); delay(4); }
    picotts_shutdown();
    return true;
}
#endif

// ---- SAM path ---------------------------------------------------------------
static bool sam_say(const String& text) {
    uint8_t* pcm = nullptr; int len = 0;
    // Higher pitch + smaller throat/mouth -> squeakier, foxier.
    if (!sam_render(text.c_str(), 72, 96, 110, 160, &pcm, &len) || !pcm)
        return false;
    if (!M5.Speaker.begin()) { sam_free(pcm); return false; }
    M5.Speaker.setVolume(g_cfg.volume);
    // SAM emits 8-bit unsigned at ~22050 Hz mono.
    M5.Speaker.playRaw(pcm, len, 22050, false, 1, -1);
    while (M5.Speaker.isPlaying()) { M5.update(); delay(4); }
    sam_free(pcm);
    return true;
}

// ---- babble fallback --------------------------------------------------------
// Procedural chirps. Pitch tracks mood; length tracks syllable count. This is
// the Animal-Crossing "animalese" trick and it always works with zero assets.
void voice_babble(FoxMood mood, int syllables) {
    if (!M5.Speaker.begin()) return;
    M5.Speaker.setVolume(g_cfg.volume);
    int base;
    switch (mood) {
        case MOOD_SLEEPY:  base = 520;  break;
        case MOOD_GRUMPY:  base = 440;  break;
        case MOOD_EXCITED: base = 900;  break;
        case MOOD_HAPPY:   base = 760;  break;
        default:           base = 640;  break;
    }
    if (syllables < 1) syllables = 1;
    if (syllables > 10) syllables = 10;
    for (int i = 0; i < syllables; ++i) {
        int f = base + (int)(esp_random() % 220) - 110;
        M5.Speaker.tone(f, 70 + esp_random() % 60);
        while (M5.Speaker.isPlaying()) { delay(2); }
        delay(20 + esp_random() % 30);
    }
}

// Rough syllable estimate so babble length matches the (unspoken) words.
static int syllable_estimate(const String& text) {
    int syl = 0; bool prev_vowel = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = tolower(text[i]);
        bool v = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y');
        if (v && !prev_vowel) ++syl;
        prev_vowel = v;
    }
    return syl < 1 ? 1 : syl;
}

// ---- public entry -----------------------------------------------------------
void voice_say(const String& text, FoxMood mood) {
    if (!text.length()) return;
    // Ensure mic is off; speaker and mic share the I2S bus on the Echo Base.
    M5.Mic.end();

    bool spoke = false;
    if (g_cfg.voice_pack == "chatterbox") {
#if HAVE_PICO
        spoke = pico_say(text);
#endif
    } else if (g_cfg.voice_pack == "critter") {
        spoke = sam_say(text);
    }
    if (!spoke) {
        // Missing pack or engine failure: chirp the line so the fox never goes
        // mute and never pretends words were spoken that weren't.
        voice_babble(mood, syllable_estimate(text));
    }
}
