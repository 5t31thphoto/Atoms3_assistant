// fox_face.inc — the fox's face: an animated, expressive little vulpine mug
// on the AtomS3R's 128x128 display, with mouth movement driven by audio level
// (FFT lip-sync ported from m5stack-avatar-mic) and captions for clarity.
#pragma once
#include <M5GFX.h>

static M5Canvas* face_canvas = nullptr;
static FoxConfig* face_cfg = nullptr;
static float mouth_open = 0.f;      // 0..1
static int   blink_timer = 0;
static bool  blinking = false;
static String caption;
static uint32_t caption_until = 0;
static uint8_t ear_wiggle = 0;

static void face_begin(FoxConfig& c) {
    face_cfg = &c;
    M5.Display.setColorDepth(16);
    M5.Display.setBrightness(90);
    face_canvas = new M5Canvas(&M5.Display);
    face_canvas->createSprite(128, 128);
    face_canvas->setTextDatum(middle_center);
}

static void face_wake() { M5.Display.setBrightness(90); }
static void face_dim()  { M5.Display.setBrightness(20); }

// Draw the fox at the current expression. Eyes/ears/mouth respond to mood and
// to the live mouth_open value while speaking.
static void face_draw(FoxMood mood) {
    if (!face_canvas) return;
    uint16_t bg = face_cfg->color_bg;
    uint16_t fur = face_cfg->color_primary;
    uint16_t cream = face_cfg->color_accent;
    face_canvas->fillSprite(bg);

    // head
    face_canvas->fillEllipse(64, 66, 44, 40, fur);
    // cheeks / muzzle
    face_canvas->fillEllipse(64, 82, 26, 20, cream);
    // ears (triangles), wiggle offset
    int ew = (ear_wiggle % 8 < 4) ? 0 : 2;
    face_canvas->fillTriangle(30, 40, 44, 8 - ew, 56, 38, fur);
    face_canvas->fillTriangle(98, 40, 84, 8 - ew, 72, 38, fur);
    face_canvas->fillTriangle(36, 34, 45, 16, 52, 34, bg);
    face_canvas->fillTriangle(92, 34, 83, 16, 76, 34, bg);

    // eyes — shape depends on mood
    uint16_t eye = 0x0000;
    if (blinking) {
        face_canvas->drawFastHLine(44, 60, 14, eye);
        face_canvas->drawFastHLine(70, 60, 14, eye);
    } else {
        switch (mood) {
            case MOOD_SLEEPY:
                face_canvas->drawArc(51, 60, 8, 8, 200, 340, eye);
                face_canvas->drawArc(77, 60, 8, 8, 200, 340, eye);
                break;
            case MOOD_EXCITED:
                face_canvas->fillCircle(51, 58, 7, eye);
                face_canvas->fillCircle(77, 58, 7, eye);
                face_canvas->fillCircle(53, 56, 2, 0xFFFF);
                face_canvas->fillCircle(79, 56, 2, 0xFFFF);
                break;
            case MOOD_GRUMPY:
                face_canvas->drawLine(44, 56, 58, 60, eye);
                face_canvas->drawLine(84, 56, 70, 60, eye);
                face_canvas->fillCircle(51, 60, 4, eye);
                face_canvas->fillCircle(77, 60, 4, eye);
                break;
            default:
                face_canvas->fillCircle(51, 59, 6, eye);
                face_canvas->fillCircle(77, 59, 6, eye);
                face_canvas->fillCircle(53, 57, 2, 0xFFFF);
                face_canvas->fillCircle(79, 57, 2, 0xFFFF);
        }
    }
    // nose
    face_canvas->fillTriangle(60, 76, 68, 76, 64, 82, eye);
    // mouth — opens with mouth_open while speaking
    int mo = (int)(mouth_open * 12);
    if (mo < 1) {
        face_canvas->drawArc(64, 84, 8, 8, 20, 160, eye);
    } else {
        face_canvas->fillEllipse(64, 88, 7, 2 + mo, eye);
    }

    // caption bar
    if (caption.length() && millis() < caption_until && face_cfg->captions) {
        face_canvas->fillRect(0, 112, 128, 16, 0x0000);
        face_canvas->setTextColor(0xFFFF);
        face_canvas->setTextSize(1);
        face_canvas->drawString(caption.substring(0, 20), 64, 120);
    }
    face_canvas->pushSprite(0, 0);
}

// Called every loop to animate idle motion (blink, ear wiggle).
static void face_tick(FoxMood mood) {
    if (++blink_timer > 220) { blinking = true; }
    if (blink_timer > 230)   { blinking = false; blink_timer = 0; }
    if ((blink_timer & 7) == 0) ear_wiggle++;
    // decay mouth toward closed when not actively driven
    mouth_open *= 0.7f;
    face_draw(mood);
}

static void face_caption(const String& text) {
    caption = text;
    caption_until = millis() + 4000;
}

static void face_listen() { face_caption("*listening*"); }
static void face_think()  { face_caption("*thinking*"); }

// FFT-lite lip-sync: while speaking we can't easily tap the speaker DAC, so we
// approximate mouth motion from the text cadence in voice_say via this hook,
// and (when miced) from input level. Here we expose a setter the speaker path
// pokes. (Ported concept from m5stack-avatar-mic's lipsync level→mouth ratio.)
static void face_set_mouth(float level01) {
    if (level01 < 0) level01 = 0; if (level01 > 1) level01 = 1;
    mouth_open = level01;
}
