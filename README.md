# Fox Voice Assistant — Offline-First Core

Target: **M5Stack AtomS3R + Atomic Echo Base**.

This build makes the device useful with **no Wi-Fi and no API key**.

## Offline runtime

`USER button hold -> microphone -> AFE/VAD/NS/AGC -> English MultiNet7 -> deterministic command registry -> local tool/game -> PicoTTS -> Echo speaker`

There is no wake word.

The local speech model is loaded from the `model` flash partition. MultiNet7 supports customized English command phrases and multiple phrases per command ID. The firmware registers Fox's command grammar at startup.

The first offline command set includes:

- time/date
- weather (when configured and Wi-Fi exists)
- Wi-Fi scan
- BLE scan
- volume
- conversation mode
- 20 Questions
- riddles
- memory game
- reaction game
- help
- sleep
- yes/no/maybe/game cancellation

Cloud AI is **disabled by default** and is only a fallback when explicitly configured.

## Why this is the first real core

The previous core had an empty `recognize_local()` function. This one initializes Espressif's actual ESP-SR model stack, builds the English command grammar, captures the real Echo microphone, runs audio through AFE, runs MultiNet7, validates recognition probability, and executes only known deterministic commands.

That gives Fox a genuine offline voice loop before we attempt a general-purpose offline dictation model.

## Build

GitHub Actions is authoritative. It builds ESP-IDF, fetches M5Unified/Arduino, ESP-SR and PicoTTS through the component manager, packages the application/model/PicoTTS resource partitions, and emits SHA-256 checksums.

The AtomS3R USER button is GPIO41 and active-low. M5Stack documents the Atomic Echo Base on the AtomS3R as the external Echo audio device, and M5Unified exposes it with `external_speaker.atomic_echo = true`.


## Online mode: deliberately boring onboarding

Fox does **not** require an API key at build time. The firmware is identical whether cloud AI is used or not.

1. Flash Fox.
2. On first boot, Fox creates the Wi-Fi network **Fox-Setup** if it has no working Wi-Fi profile.
3. Join that network from a phone.
4. The captive setup page opens automatically on supported phones; if it does not, open `http://192.168.4.1/`.
5. Enter Wi-Fi credentials.
6. Optionally paste a Groq API key and choose the model.
7. Save. Fox stores the configuration in NVS and restarts.

Holding the USER button while Fox boots forces the setup portal. Fox also has an offline voice command to open setup.

### Online behavior

The device decides locally:

- recognized hardware/tool/game command -> execute locally
- Wi-Fi unavailable -> remain fully offline
- Wi-Fi available + model/key configured -> online conversational fallback

The API key never belongs in source code, GitHub Actions artifacts, or the firmware binary. The cloud model also does not own Fox's memory. The firmware keeps a small rolling context journal containing recent user turns, Fox responses, and device activity, and the prompt compiler injects only a bounded recent slice. Personality is likewise a local configuration value.
