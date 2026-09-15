# 🦊 Fox Voice Companion

A cute, fidgety anime-fox companion that lives on an **M5Stack AtomS3R** with an
**Atomic Echo Base**. It listens, talks, makes faces, remembers you, blasts IR at
your TV, and plays little games — **entirely offline by default**. If you give it
wifi and a free API key, it grows an online brain too. But the online part is a
*bonus*, never a requirement.

There is **no wake word**. You talk to the fox by holding its button
(push-to-talk). A quick tap opens its menu. It can also enter a hands-free
"conversation mode" that listens for a while and times out on silence.

---

## What it does

- **Hears you offline.** MultiNet7 on-device speech recognition understands a set
  of spoken commands with no network at all.
- **Talks.** Two swappable voices (see brain packs) plus a procedural "babble"
  fallback so it is *never* mute. Captions always show on screen.
- **Has moods.** A personality engine tracks play / social / energy needs that
  drift over time, colouring how it phrases things and how its face looks.
- **Has a tiny brain.** An optional ~200K–1M parameter transformer runs *on the
  chip* and rephrases the fox's lines to be cuter. It can never invent facts (see
  Safety below).
- **Remembers.** A rolling journal is stored on the device, in its own flash
  partition. The fox owns its memory; the cloud only ever sees a recent slice.
- **Controls your TV.** A TV-B-Gone-style IR library plus a "fake learning"
  flow: the fox sweeps power codes and you tap the moment your TV turns off, and
  it binary-searches down to *your* code and remembers it.
- **Fidgets.** Idle chatter, IMU gestures (boop it, shake it), blinking, ear
  wiggles, a guess-the-paw game.
- **Optional online brain.** With wifi + an OpenAI-compatible key (Groq's free
  tier is perfect), it can transcribe free speech (Whisper) and hold richer
  conversations — while keeping its device-owned identity and memory.

---

## Hardware

| Part | Notes |
|------|-------|
| M5Stack **AtomS3R** | ESP32-S3, 8MB flash, 8MB octal PSRAM, 128×128 LCD, BMI270+BMM150 IMU, IR LED on **GPIO47**, USER button on **GPIO41** |
| **Atomic Echo Base** | ES8311 codec, mic + speaker over I2S (DIN 7 / WS 6 / DOUT 5 / BCK 8; I2C SDA 38 / SCL 39; amp via PI4IOE @0x43) |

The firmware uses **M5Unified's own** mic/speaker path
(`M5.config().external_speaker.atomic_echo = true`). It deliberately does **not**
also start the standalone EchoBase library — two I2S drivers on one bus fight.

---

## Flash it (no toolchain needed)

1. Open the **web flasher** (GitHub Pages deploy of `web/`) in Chrome, Edge, or
   Opera on a desktop.
2. Pick a **brain pack** (Chatterbox or Critter).
3. Click **Flash my fox** and follow the prompt. ~7 MB, about a minute.
4. In step 3, fill in a name/personality (and optionally wifi + API key) and
   click **Send to fox** — this streams the settings to the device over USB.
   **Your wifi password and API key are written only to the device.** They are
   never uploaded and never compiled into the firmware or CI.

Prefer the command line? Build with ESP-IDF v5.5.2 (`firmware/`), then flash the
app plus the data partitions at the offsets in `firmware/partitions.csv`.

---

## Brain packs (swappable at flash time)

| Pack | Voice | On-device brain | Feel |
|------|-------|-----------------|------|
| **A — Chatterbox** (default) | PicoTTS, pitched up | ~200K params | Speaks clear words; friendliest default |
| **B — Critter** | procedural formant synth | ~700K params | Chirpy little creature; more character |

Both are flashed; you can switch from the fox's on-device menu. Captions show
either way.

---

## The on-device brain, honestly

The brain is a real llama-style transformer (RMSNorm + RoPE + attention +
SwiGLU, int8 weights) trained **from scratch in pure numpy** — no PyTorch — by
`tools/train_brain.py`, so it builds on a plain CI runner in ~20–30 s. It reaches
cross-entropy ~0.2 and generates lines like:

```
[happy]   it is sunny        ->  ooh it is sunny.
[sleepy]  it is night        ->  it is night... *yawn*
[excited] found your remote  ->  found your remote! wag!
[grumpy]  battery is low     ->  battery is low. hmph.
```

It is intentionally tiny and "barely able to speak" — that's the charm. It runs
straight out of a flash partition with no RAM copy of the weights.

### Safety: the brain can't lie

The firmware computes every real fact (the time, a scan result, a tool action).
The brain only ever *rewraps* that fact. Its output is accepted **only if the
fact's key word survives** in what it generated; otherwise it's discarded and the
deterministic template layer speaks instead. So an undertrained or hallucinating
micro-model can never make the fox claim something false — worst case, it's quiet
and the templates carry the line. The fox also never fakes an action or speech it
didn't actually perform.

---

## Build pipeline

GitHub Actions (`.github/workflows/build-firmware.yml`):

1. **data job** — `pip install numpy`, then `build_ir.py` compiles the Flipper
   `.ir` libraries into a compact `FOXI` blob, and `train_brain.py` trains both
   brain packs into `FOXB` blobs.
2. **firmware job** — builds with `espressif/esp-idf-ci-action` (IDF v5.5.2,
   target esp32s3), assembles `dist/` (bootloader + partition table + app + data
   bins + `SHA256SUMS`), and emits an **esp-web-tools `manifest.json`**.
3. **pages** — publishes `web/` + `dist/` so the flasher works from a URL.

No secrets are ever needed to build; configuration happens on-device post-flash.

---

## Repo layout

```
firmware/                ESP-IDF (Arduino-as-component) app
  main/
    fox.h                shared config/types/tunables
    fox_main.cpp         wiring: speech, cloud, dispatch, PTT loop
    fox_brain.cpp        personality: moods, needs, reflection, templates
    fox_voice.cpp        PicoTTS / critter / babble speech
    fox_memory.cpp       device-owned rolling journal (LittleFS)
    fox_llm.cpp          tiny on-device brain loader + safety gate
    fox_llm_forward.inc  transformer forward pass (int8, mmap weights)
    fox_sam.c            original procedural "critter" voice
    fox_ir.inc           IR sweep + fake-learning (RMT TX, GPIO47)
    fox_face.inc         animated face + caption + lip-sync hooks
    fox_input.inc        PTT capture, IMU gestures, menu, USB config, sleep
    data/                built FOXB/FOXI blobs land here
  partitions.csv         8MB layout (app, models, PicoTTS, foxbrain, foxdata, foxfs)
  sdkconfig.defaults     esp32s3, PSRAM, MultiNet7, no wakenet, USB-CDC
tools/
  build_ir.py            Flipper .ir  -> FOXI binary
  train_brain.py         from-scratch numpy transformer -> FOXB binary
assets/ir/               source .ir libraries (tv/audio/projector/ac)
web/index.html           esp-web-tools flasher + USB config sender
tests/                   build-artifact validation (no toolchain needed)
docs/                    architecture, partition map, limitations
```

## Run the tests

```
python tests/test_build_artifacts.py
```

Validates the IR converter, the brain byte-contract (trainer ↔ firmware must
agree exactly), and the command registry — all without an ESP32.

---

## Known limitations & honest notes

See `docs/LIMITATIONS.md`. Highlights: PicoTTS "cuteness" is prosody-limited and
best confirmed on real hardware; the "critter" voice is an **original** synth,
not the licensing-murky reverse-engineered SAM; MultiNet phrases may need tuning
for your accent; the on-device brain is a toy by design.
