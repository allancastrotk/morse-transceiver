# Morse Transceiver — Core Documentation

## 1. Overview

Modular firmware for ESP8266 implementing a CW (Morse) transceiver with both local input (physical button) and remote input (Wi-Fi/TCP). The system provides sound feedback via a **buzzer**, visual feedback via an **OLED SSD1306**, and a **LED blinker**. The architecture is fully **non-blocking**, based on `millis()` + `yield()`, ensuring responsiveness and multitasking.

---

## 2. File and Module Structure

| Module / File       | Main Responsibility                        | Interactions                      |
| ------------------- | ------------------------------------------ | --------------------------------- |
| `main.cpp`          | Entry point; module integration; callbacks | Calls `init`/`update` for all     |
| `morse-key.*`       | Physical button events (ISR + debounce)    | Telegrapher, History              |
| `telegrapher.*`     | Morse classification, translation, modes   | History, Network-State, Display   |
| `morse-telecom.*`   | Text-based TCP protocol                    | Network-Connect, Telegrapher      |
| `network-state.*`   | FSM: FREE/TX/RX                            | Buzzer, Display                   |
| `network-connect.*` | Wi-Fi/TCP: scanning, connection, heartbeat | Network-State, Telecom            |
| `history.*`         | Circular TX/RX buffers (30 chars)          | Display reads, Telegrapher writes |
| `display-adapter.*` | OLED UI: splash, redraw, caching           | Telegrapher, Network-State        |
| `buzzer-driver.*`   | Non-blocking buzzer driver                 | Telegrapher, Network-State        |
| `blinker.*`         | LED blinker (phrase → Morse → LED)         | Translator                        |
| `translator.*`      | ASCII ↔ Morse lookup table                 | Telegrapher, Blinker              |
| `bitmap.h`          | Bitmap for splash screen                   | Display                           |

---

## 3. Header Pattern and Log Flags

All source files follow:

```cpp
// File: name.ext vX.X
// Description: explanation
// Last modification: description
// Modified: YYYY-MM-DD HH:MM
// Created: YYYY-MM-DD
```

Directly below the includes:

```cpp
// ====== LOG FLAGS ======
#define LOG_[MODULE]_INIT   1
#define LOG_[MODULE]_EVENT  1
#define LOG_[MODULE]_ERROR  0
```

---

## 4. Module Descriptions

### 4.1 main.cpp

* Integrates all modules.
* `setup()`: initializes History, Translator, Telecom, Network, Telegrapher, Display, Buzzer, and Blinker.
* `loop()`: calls each module’s `update()`.
* Manages callbacks and event flow.

### 4.2 morse-key

* Captures physical button events (pin D5).
* Uses ISR + debounce.
* Sends events to the Telegrapher.
* Flags: `LOG_MORSE_KEY_ISR`, `LOG_MORSE_KEY_EVENT`.

### 4.3 telegrapher

* Classifies durations into dot/dash.
* Converts Morse sequences into characters.
* Modes: DIDACTIC / MORSE.
* Updates History; notifies Display; sends events via Network-State.
* Flags: `LOG_TELEGRAPHER_SYMBOL`, `LOG_TELEGRAPHER_MODE`.

### 4.4 morse-telecom

* Text-based TCP protocol (port 5000).
* Messages: `alive`, `duration:<ms>`, `request_tx`, `ok`, `busy`, `mac:<mac>`.
* Flags: `LOG_TELECOM_RX`, `LOG_TELECOM_TX`.

### 4.5 network-state

* Manages FREE/TX/RX states.
* Controls Display and Buzzer.
* Flags: `LOG_NETSTATE_CHANGE`, `LOG_NETSTATE_TIMEOUT`.

### 4.6 network-connect

* Performs network scan, connection, and heartbeat.
* Provides socket to Telecom.
* Flags: `LOG_NETCONNECT_SCAN`, `LOG_NETCONNECT_CONN`.

### 4.7 history

* Circular buffer for TX/RX (30 chars).
* Display shows 29 visible characters:

  * Line 1: 10 chars
  * Line 2: 10 chars
  * Line 3: 9 chars
* Flags: `LOG_HISTORY_PUSH`, `LOG_HISTORY_OVERFLOW`.

### 4.8 display-adapter

* Renders the OLED layout.
* Left half: TX/RX history.
* Right half: current symbol or letter (1.5s).
* Wi-Fi indicator shown in top-right corner.
* Blinking cursor in DIDACTIC mode.
* Flags: `LOG_DISPLAY_UPDATE`, `LOG_DISPLAY_CACHE`.

### 4.9 buzzer-driver

* Non-blocking sound patterns.
* Used by Telegrapher and Network-State.
* Flags: `LOG_BUZZER_INIT`, `LOG_BUZZER_PATTERN`.

### 4.10 blinker

* Phrase → Morse → LED blinking loop.
* Fully independent.
* Flags: `LOG_BLINKER_INIT`, `LOG_BLINKER_BUILD`, `LOG_BLINKER_RUN`, `LOG_BLINKER_PHASE`.

### 4.11 translator

* ASCII ↔ Morse lookup.
* Flags: `LOG_TRANSLATOR_LOOKUP`.

### 4.12 bitmap.h

* Bitmap in PROGMEM.
* Displayed during the 3-second splash screen.

---

## 5. Best Practices

* Always use `millis()` + `yield()`.
* Timestamped logs.
* Boundary checks for all buffers.
* Use callbacks for loose coupling.
* Constants in CAPS.
* Splash screen fixed at 3 seconds.
* History must always reflect 29 visible characters.

---

## 6. Interaction Flow

### Local input (morse-key)

Button → Telegrapher → History → Display → Buzzer → Network-State → Telecom.

### Remote input

TCP → Network-Connect → Telecom → Telegrapher → History → Display → Buzzer.

### Blinker

Independent system; LED blinks the configured phrase.

### Display

Reads History; shows current symbol/letter; TX/RX/Free states; Wi-Fi indicator.

### Buzzer

Provides both local and remote feedback.

---

## 7. Testing and Troubleshooting

* Splash screen: must last 3 seconds.
* Button test: press → buzzer ON; release → duration logged.
* Classification thresholds: ≤150ms = dot, >150ms = dash.
* Translation: 800ms of silence triggers letter conversion.
* Networking: communication between devices must be stable.
* Display: verify history, symbol output, and Wi-Fi indicator.

**Common issues:**

* Blank OLED → verify SDA/SCL.
* Wi-Fi stuck in AP mode → check SSID.
* No buzzer sound → inspect transistor.
* Poor debounce → adjust `DEBOUNCE_TIME`.

---

## 8. Advanced Recommendations

* Configurable SSID and password.
* Log levels: DEBUG/INFO/WARN/ERROR.
* TCP packet validation.
* Synchronize blinker timings with thresholds.
* Export history over the network.
* Maintain strict module isolation.

---

## 9. License and Credits

* License: MIT.
* Credits: Adafruit GFX / SSD1306, ESP8266 core.
* Author: Allan.

---

## 10. Conclusion

This document defines the technical structure, architecture, interaction flows, display layout, and best practices for maintaining and evolving the Morse transceiver firmware.