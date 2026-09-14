# Architecture

## High-level flow

```
[User] --PTT--> [Atomic Echo Mic]
                    |
                    v
              [16 kHz PCM]
                    |
         +----------+----------+
         |                     |
    Groq Whisper STT      (optional local VAD)
         |                     |
         v                     v
   [transcript] --------> [LLM client]
                              |
              +---------------+---------------+
              |               |               |
           Groq API      On-device eng.   Weight stream
              |               |               |
              +-------+-------+-------+-------+
                      |
                 [reply text]
                      |
              +-------+-------+
              |               |
         Edge TTS / Groq   On-device beep
              |               |
              v               v
         [PCM play] ----> [Speaker]
                      |
                 lip-sync amplitude
                      |
                 [Fox avatar]
```

## Controls
- IMU flick up/down → menu navigation
- Button single click → confirm
- Double click → open menu
- Long press → PTT

## Tools exposed to the LLM
- `ble_radar` — BLE scan + IMU orientation → polar list
- `wifi_scan` — AP list
- `ir_send` — raw / learned codes
- `imu_gesture` — current gesture

Menu can run the same tools without the LLM; the fox “does the work” then steps aside for the radar/plot UI.

## Persistence
- NVS only (fox name, API key, mode, tiny context summary)
- Never store full conversation history on flash

## Build path
Web UI → config JSON → GitHub Actions (ESP-IDF) → merged `.bin` → ESP Web Tools / esptool-js in the browser.
