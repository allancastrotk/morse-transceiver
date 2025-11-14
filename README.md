# morse-transceiver

A CW (Morse) transceiver based on ESP8266 that lets you send/receive Morse signals locally (button + buzzer) and remotely over Wi‑Fi/TCP between two units. Provides visual feedback via an OLED display (SSD1306) and an LED that blinks Morse messages.

---

## Repository contents (brief)
- `morse-project.ino` — main setup and loop (orchestrates modules)  
- `cw-transceiver.cpp` / `.h` — core CW logic (input, buzzer, translation, history)  
- `network.cpp` / `.h` — asynchronous Wi‑Fi management, TCP and messaging protocol  
- `blinker.cpp` / `.h` — converts text → Morse and blinks LED  
- `display.cpp` / `.h` — OLED UI (Adafruit SSD1306)  
- `bitmap.h` (optional) — image used for the splash screen

---

## Functional overview
- Local input: physical key/button on D5 (INPUT_PULLUP).  
- Remote input: `duration:<ms>` messages received over TCP represent remote key presses.  
- Buzzer on D8: ON while a local/remote press is active.  
- Connection states: `FREE`, `TX`, `RX`. When a local transmission starts and the network is available, the unit occupies the network and sends the duration to the peer.  
- Simple text-based TCP protocol (port 5000): `alive`, `duration:<ms>`, `request_tx`, `ok`/`busy`, `mac:<mac>`. Heartbeat every 1s.  
- Blinker (D4) continuously flashes Morse messages (default `"SEMPRE ALERTA"`).  
- Non-blocking design using `millis()` + `yield()` (ESP8266-friendly).

---

## Display details (SSD1306 OLED)
- Driver: Adafruit_SSD1306 (Adafruit GFX)  
- Resolution: 128x64 (SCREEN_WIDTH=128, SCREEN_HEIGHT=64)  
- I2C address: `0x3C` (OLED_ADDRESS)  
- I2C pins used: SDA = D2, SCL = D1 (Wire.begin(D2, D1)) — verify for your board variant  
- Init behavior: shows a bitmap splash (from `bitmap.h`) for 3000 ms (DISPLAY_INIT_DURATION) then clears and shows main UI  
- Update cadence: internal throttle at 100 ms (DISPLAY_UPDATE_INTERVAL) and network strength refresh every 5000 ms (NETWORK_UPDATE_INTERVAL)
- UI layout summary:
  - Vertical divider at x=64 splits left history / right symbol area
  - Top-right: connection indicator `TX` (upper) or `RX` (lower)
  - Top-right corner: Wi‑Fi strength (4 chars, e.g., ` 75%` or ` OFF`)
  - Left top: TX history shown over three 10-char lines
  - Left bottom: RX history shown over three 10-char lines
  - Right large area: current symbol or translated letter (textSize 6).  
  - DIDACTIC mode: shows large translated letter briefly and a blinking cursor when idle.  
  - MORSE mode: shows the typed Morse symbol (e.g., ".-") while being entered; shows most recent letter briefly after entry.

Notes:
- `initDisplay()` blocks for 3s while showing the splash; network scanning runs asynchronously during that time.  
- Display code caches previous values and skips redraws when nothing changed to reduce SPI/I2C traffic.

---

## Minimal schematic / pinout
- D5 — LOCAL button (INPUT_PULLUP; button shorts to GND)  
- D6 — REMOTE input (INPUT_PULLUP)  
- D8 — buzzer (digital ON/OFF) — passive buzzer may need a driver/transistor  
- D4 — LED blinker (GPIO2; active HIGH)  
- D2 = SDA, D1 = SCL — I2C for SSD1306 (verify board pin mapping)  
- Power: 5V/USB or 3.3V depending on board (use proper regulator)

Notes:
- Buttons should short to GND (INPUT_PULLUP).  
- Use a current-limiting resistor for external LEDs (e.g., 220 Ω).  
- Use a transistor for the buzzer if it draws more current than a GPIO can safely source.

---

## Build and flash

Suggested environment:
- Arduino IDE with ESP8266 core installed, or PlatformIO (VS Code)  
- Select the correct ESP8266 board (NodeMCU, Wemos D1 mini, etc)  
- Serial monitor: 115200 baud

Dependencies:
- ESP8266 core (includes `ESP8266WiFi`, `WiFiClient`, `ESP8266WiFiMulti`)  
- Adafruit_GFX and Adafruit_SSD1306 libraries for the display

Steps:
1. Put all project files in a folder (the `.ino` plus `.cpp`/`.h`).  
2. Open in Arduino IDE or configure `platformio.ini` with `platform: espressif8266`.  
3. Select the correct board and port.  
4. Compile and upload.  
5. Open Serial Monitor at 115200 to follow logs.

---

## Quick usage and tests

1. Startup: on power-up the device starts an async Wi‑Fi scan, shows the splash on the display, and starts the blinker. Check serial logs.  
2. Buzzer and local input test:
   - Press and hold the local button (D5): buzzer should be ON while pressed; on release the code logs the Duration to Serial.
   - Short/long press: duration ≤ 150 ms → dot (`.`); >150 ms → dash (`-`).  
3. Letter translation:
   - After release, wait `LETTER_GAP` (800 ms) without new presses; the current symbol is translated to a letter and appended to history (TX for local, RX for remote).  
4. Network test (two units):
   - Two devices running the firmware can discover each other using SSID `morse-transceiver` (open SSID by default). They negotiate roles by MAC and establish TCP on port 5000.
   - One unit sends `duration:<ms>\n` to the peer; the receiver converts it into a remote symbol.  
5. Blinker:
   - LED on D4 blinks the default message. Change it with `setBlinkerMessage("TEXT")`.

---

## TCP protocol and messages

Port: `5000`

Messages (each line ends with `\n`):
- `alive` — heartbeat sent every 1s  
- `duration:<ms>` — conveys a local press duration to the peer  
- `request_tx` — request to start TX; response: `ok` or `busy`  
- `mac:<mac>` — role negotiation based on MAC comparison

Heartbeat:
- Sends `alive` every `HEARTBEAT_INTERVAL` (1s).  
- If `HEARTBEAT_TIMEOUT` (3s) passes without receiving `alive`, the peer is disconnected and the state becomes `DISCONNECTED`.

---

## Public APIs (integrator summary)

cw-transceiver
- `initCWTransceiver()`  
- `updateCWTransceiver()` — call frequently (e.g., every 5 ms)  
- `getConnectionState()` → `FREE|TX|RX`  
- `getMode()` → `DIDACTIC|MORSE`  
- `getCurrentSymbol()`, `getHistoryTX()`, `getHistoryRX()`

network
- `initNetwork()`  
- `updateNetwork()` — call periodically (e.g., every 100 ms)  
- `occupyNetwork()` — returns true if the network is available for TX  
- `isConnected()`  
- `sendDuration(unsigned long duration)`  
- `getNetworkStrength()` → `"###%"` or `" OFF"`

blinker
- `initBlinker()`  
- `setBlinkerMessage(const char* newMessage)`  
- `updateBlinker()` — call periodically (e.g., every 100 ms)

display
- `initDisplay()` — initializes SSD1306, shows splash, then UI  
- `updateDisplay()` — call regularly (project calls every 500 ms)

---

## Values and timing constants

- `DEBOUNCE_TIME` = 25 ms  
- `SHORT_PRESS` = 150 ms (≤ → `.`)  
- `LONG_PRESS` = 400 ms (special: press ≥ `LONG_PRESS * 5` toggles mode)  
- `LETTER_GAP` (cw-transceiver) = 800 ms  
- `INACTIVITY_TIMEOUT` = 5000 ms

Blinker:
- `DOT_TIME` = 300 ms  
- `DASH_TIME` = 600 ms  
- `SYMBOL_GAP` = 300 ms  
- `LETTER_GAP` = 600 ms  
- `WORD_GAP` = 1800 ms

Recommendation: align blinker DOT/DASH with `SHORT_PRESS` / `LONG_PRESS` for coherent audio/visual feedback.

---

## Common issues and fixes

- Display not showing: verify SDA/SCL wiring (D2/D1) and the Adafruit SSD1306 library; confirm I2C address `0x3C`.  
- Wi‑Fi stuck in AP_MODE: check scan logs; default SSID is open `morse-transceiver`.  
- Buzzer silent: verify wiring on D8; passive buzzer might need a transistor.  
- Debounce problems: adjust `DEBOUNCE_TIME` if spurious presses/releases occur.  
- Failed translation: `translateMorse()` returns `'\0'` for unknown codes; add a fallback (`'?'`) if preferred.

---

## Suggested improvements
- Make SSID password configurable (empty PASS is insecure by default).  
- Reset `retryDelay` on successful reconnection and cap backoff.  
- Add log levels (DEBUG/INFO) to reduce `Serial.print` noise in production.  
- Validate/limit `duration` values received over TCP.  
- Synchronize blinker timings with cw-transceiver thresholds.  
- Notify the display when `occupyNetwork()` fails in `captureInput()`.

---

## License
Add a `LICENSE` file (e.g., MIT) to the repository if you want explicit permissions.

---

## Contributing
Open issues or PRs for:
- bug fixes  
- adding support for different displays  
- improving the network protocol (authentication, robust reconnection)  
- UX suggestions (visual/audio feedback)
