# morse-transceiver Documentation

## Project overview

morse-transceiver is an ESP8266-based CW (Morse) transceiver firmware that lets two units exchange Morse keying locally (button + buzzer) and remotely over Wi‑Fi/TCP. It provides visual feedback on an SSD1306 OLED and via an LED blinker. The firmware is non-blocking (millis()/yield()) and built for reproducible builds and stable releases.

Key features
- Local key input (button + buzzer) and remote key input (duration messages over TCP)
- Connection states: FREE, TX, RX — network is occupied during local TX
- Simple text TCP protocol on port 5000: `alive`, `duration:<ms>`, `request_tx`, `ok`/`busy`, `mac:<mac>`
- Didactic vs Morse display modes (toggle by long press)
- TX / RX character history buffers
- LED blinker that flashes configurable Morse messages
- Non-blocking architecture suitable for ESP8266

---

## Hardware and pinout

Recommended components
- ESP8266 module (NodeMCU, Wemos D1 mini)
- Momentary button(s) wired to ground (INPUT_PULLUP)
- Buzzer (or small speaker; use transistor if needed)
- Status LED (external or on-board)
- 128x64 SSD1306 I2C OLED display

Pin mapping (as used in the code)
- D5 — LOCAL button (INPUT_PULLUP)
- D6 — REMOTE input (INPUT_PULLUP)
- D8 — BUZZER output
- D4 — LED blinker (GPIO2, active HIGH)
- I2C (SSD1306) — commonly D1 = SCL, D2 = SDA (Wire.begin(D2, D1) in code)

Hardware notes
- Buttons should short to GND (internal pull-ups enabled).  
- Passive buzzers may require a transistor or driver.  
- Use series resistors for external LEDs (e.g., 220 Ω).  
- Verify I2C pin mapping for your board variant.

---

## Source structure and startup flow

Files (present in repository)
- morse-project.ino — main orchestration (setup and loop)
- cw-transceiver.cpp / .h — core CW logic: input capture, debounce, buzzer, translation, history
- network.cpp / .h — Wi‑Fi & TCP FSM, peer negotiation, heartbeat and message handling
- blinker.cpp / .h — converts text to Morse and blinks LED
- display.cpp / .h — SSD1306 UI, splash bitmap, display caching and refresh
- bitmap.h — splash bitmap (optional)

Startup sequence
1. Serial.begin(115200)
2. initNetwork() — starts async Wi‑Fi scan (STA mode) with randomized delay
3. initDisplay() — initializes SSD1306, shows bitmap splash (3s)
4. initCWTransceiver() — configures buttons and buzzer
5. initBlinker() — configures LED and default message

Main loop (non-blocking)
- updateCWTransceiver() — every ~5 ms (input responsiveness)
- updateDisplay() — every ~500 ms (UI)
- updateBlinker() — every ~100 ms (LED Morse)
- updateNetwork() — every ~100 ms (network FSM)
- yield() — to allow ESP8266 background tasks

---

## Timings and constants

CW transceiver
- DEBOUNCE_TIME = 25 ms  
- SHORT_PRESS = 150 ms (≤ ⇒ dot)  
- LONG_PRESS = 400 ms (long press threshold; used ×5 in code to toggle mode)  
- LETTER_GAP = 800 ms (end of letter detection)  
- INACTIVITY_TIMEOUT = 5000 ms

Blinker
- DOT_TIME = 300 ms  
- DASH_TIME = 600 ms  
- SYMBOL_GAP = 300 ms  
- LETTER_GAP = 600 ms  
- WORD_GAP = 1800 ms

Network
- SCAN_INTERVAL = 500 ms  
- SCAN_TIMEOUT = 5000 ms  
- CONNECT_TIMEOUT = 5000 ms  
- HEARTBEAT_INTERVAL = 1000 ms  
- HEARTBEAT_TIMEOUT = 3000 ms  
- RETRY backoff initial = 10000 ms (increases on failures)

Recommendation: align blinker DOT/DASH values with SHORT_PRESS/LONG_PRESS for consistent audio/visual feedback.

---

## Module APIs and behavior

### cw-transceiver
Public functions
- initCWTransceiver()  
- updateCWTransceiver()  
- captureInput(InputSource source, unsigned long duration)  
- getConnectionState() → FREE | TX | RX  
- getMode() → DIDACTIC | MORSE  
- getCurrentSymbol(), getHistoryTX(), getHistoryRX()

Behavior summary
- Reads LOCAL (D5) and REMOTE (D6) with INPUT_PULLUP; applies debounce.
- Activates buzzer (D8) while a key is pressed.
- Classifies press duration into dot ('.') or dash ('-') using SHORT_PRESS threshold.
- If LOCAL press starts and occupyNetwork() returns true, sets state to TX and calls sendDuration(duration).
- For REMOTE durations (received via network), sets state to RX and populates currentSymbol accordingly.
- After LETTER_GAP without presses, translateMorse() maps symbol to a character and appends to TX or RX history (30-char circular buffer behavior).

Notes
- currentSymbol supports up to 6 elements per letter; adjust buffer if needed.
- translateMorse() returns '\0' for unknown codes — you may want a visible fallback like '?'.

### network
Public functions
- initNetwork()  
- updateNetwork()  
- occupyNetwork() — currently returns isConnected()  
- isConnected()  
- sendDuration(unsigned long duration)  
- getNetworkStrength() → "###%" or " OFF"

Behavior summary
- FSM states: SCANNING → CONNECTING → CONNECTED / AP_MODE / DISCONNECTED.
- Performs async Wi‑Fi scan to find SSID "morse-transceiver". If none found after attempts, starts softAP (AP+STA).
- Establishes TCP connection on port 5000; protocol: plain text messages terminated by '\n'.
- Heartbeat: sends "alive" every 1s; heartbeat timeout 3s → disconnect.
- Handles messages:
  - "alive" → heartbeat update
  - "duration:<ms>" → captureInput(REMOTE, ms)
  - "request_tx" → replies "ok" or "busy" based on connection state
  - "mac:<mac>" → role negotiation by MAC comparison (determine who should be STA vs AP)

Notes and improvements
- occupyNetwork() returns true only when connected (or AP_MODE with client). If team wants an explicit reservation/handshake, extend protocol (request_tx negotiation).
- Consider adding authentication or configurable SSID/password.

### blinker
Public functions
- initBlinker()  
- setBlinkerMessage(const char* newMessage)  
- updateBlinker()

Behavior summary
- Converts ASCII message to Morse using PROGMEM table and constructs morseMessage string using '.' '-' '/' (letter separator) and ' ' (word separator).
- Blinks LED (D4) according to DOT_TIME/DASH_TIME and gaps.
- Buffer morseMessage fixed at 100 bytes — messages longer than that are truncated.

Notes
- Check strcpy_P usage to avoid buffer overflow on systems with long Morse codes; increase buffer if you plan long messages.

### display
Public functions
- initDisplay() — initializes SSD1306, shows splash bitmap, prepares UI
- updateDisplay() — refreshes UI (throttled to 100 ms internally; project calls every 500 ms)

Behavior summary
- Uses Adafruit_SSD1306 (128×64, I2C address 0x3C). Wire.begin(D2, D1) used for SDA/SCL.
- Shows splash bitmap on startup for DISPLAY_INIT_DURATION (3s) then clears and enters main UI.
- Main UI layout:
  - Vertical divider at x = 64 (left: history; right: symbol/letter)
  - Top-right: connection indicator (TX upper / RX lower)
  - Top-right corner: Wi‑Fi strength (cached string, 4 chars)
  - Left-top: TX history (3 lines, 10 chars each)
  - Left-bottom: RX history (3 lines, 10 chars each)
  - Right: big symbol/letter area (textSize 6)
  - DIDACTIC mode: shows translated letter briefly and blinking cursor when idle
  - MORSE mode: shows current symbol as composed; shows last letter briefly after entry
- Display code caches previous values (history, symbol, state, mode, network strength) and skips redraws unless content changed.
- Network strength updated every NETWORK_UPDATE_INTERVAL (5s) via getNetworkStrength().

Notes
- initDisplay() blocks for 3s to show the splash; async network scan continues in background.
- If using a different OLED library or address, update display.cpp accordingly.

---

## Build, flash and recommended workflow

Recommended environments
- PlatformIO (VS Code) — preferred for CI and reproducible builds
- Arduino IDE — simpler for quick testing

Dependencies
- ESP8266 core (ESP8266WiFi family)  
- Adafruit_GFX and Adafruit_SSD1306 for the display (or alternate SSD1306 driver if preferred)

Build/flash steps (PlatformIO)
1. Place all source files under `firmware/` or `/src`.  
2. Create `platformio.ini` for your board (e.g., Wemos D1 mini or NodeMCU).  
3. `pio run -t upload` to compile and flash.  
4. Open Serial Monitor at 115200 to follow logs.

Build/flash steps (Arduino IDE)
1. Copy files into a single sketch folder (`morse-project.ino` + .cpp/.h).  
2. Select board and COM port.  
3. Compile and upload.  
4. Monitor serial at 115200.

CI/Release suggestions
- Use GitHub Actions + PlatformIO for automated build on PRs and to produce firmware artifacts (.bin) on tags/releases.
- Publish binaries via GitHub Releases and tag using semantic versioning (vMAJOR.MINOR.PATCH).

---

## Tests and troubleshooting checklist

Startup checks
- Serial logs show network scan, display init, CW transceiver init and blinker message.

Local input
- Press and hold local button (D5): buzzer should be ON; release logs Duration and symbol.
- Confirm short vs long mapping: ≤150 ms => dot, >150 ms => dash.

Letter detection
- After no key activity for LETTER_GAP (800 ms), currentSymbol should be translated and appended to history.

Network pairing
- Two units with same firmware should discover and negotiate via SSID `morse-transceiver`.
- Confirm TCP connect and exchange of `mac:` and `alive` messages; durations sent should appear on peer as remote symbols.

Display issues
- If OLED stays blank: check I2C wiring (SDA/SCL), address 0x3C, and Adafruit_SSD1306 library configuration.

Common fixes
- Wi‑Fi never connects / stays in AP: examine serial scan logs; adjust SSID/PASS or enable a known AP for testing.
- Buzzer silent: use transistor driver and check wiring/polarity.
- Spurious button events: increase DEBOUNCE_TIME or add hardware RC filter.

---

## Limitations and recommended improvements

- Security: SSID is open by default (PASS empty). Make SSID and PASS configurable and optionally add simple authentication.
- Backoff logic: reset retryDelay on success and cap max backoff to avoid unbounded growth.
- Logging: add log levels (DEBUG/INFO/WARN) to reduce Serial spam in production and to speed real-time behavior.
- Protocol robustness: validate and clamp received `duration` values; handle partial lines and malformed packets more defensively.
- UI/feedback: unify DOT/DASH timings between blinker and input thresholds for consistent user experience.
- History handling: consider exposing history over the network or adding a serial/HTTP status endpoint for remote monitoring.

---

## License and credits

- Add an explicit LICENSE file (e.g., MIT) before public distribution.  
- Credit dependencies (Adafruit GFX / SSD1306 / ESP8266 core) in docs.

---
