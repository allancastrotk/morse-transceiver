# Morse Transceiver

A CW (Morse) transceiver based on ESP8266 that lets you send/receive Morse signals locally (button + buzzer) and remotely over Wi‑Fi/TCP between two units. Provides visual feedback via an OLED display (SSD1306) and an LED that blinks Morse messages.

---

## Repository Contents
- `morse-project.ino` — main setup and loop (orchestrates modules)  
- `cw-transceiver.cpp` / `.h` — core CW logic (input, buzzer, translation, history)  
- `network.cpp` / `.h` — asynchronous Wi‑Fi management, TCP and messaging protocol  
- `blinker.cpp` / `.h` — converts text → Morse and blinks LED  
- `display.cpp` / `.h` — OLED UI (Adafruit SSD1306)  
- `bitmap.h` (optional) — image used for the splash screen  

---

## Functional Overview
- Local input: physical key/button on D5 (`INPUT_PULLUP`)  
- Remote input: `duration:<ms>` messages received over TCP represent remote key presses  
- Buzzer on D8: ON while a local/remote press is active  
- Connection states: `FREE`, `TX`, `RX`  
- Simple text-based TCP protocol (port 5000): `alive`, `duration:<ms>`, `request_tx`, `ok`/`busy`, `mac:<mac>`  
- Blinker (D4) continuously flashes Morse messages (default `"SEMPRE ALERTA"`)  
- Non-blocking design using `millis()` + `yield()`  

---

## Display Details (SSD1306 OLED)
- Driver: Adafruit_SSD1306 (Adafruit GFX)  
- Resolution: 128x64  
- I2C address: `0x3C`  
- I2C pins: SDA = D2, SCL = D1  
- Init behavior: splash for 3000 ms, then main UI  
- Update cadence: 100 ms (UI), 5000 ms (network strength)  
- Layout:
  - Left: TX/RX history  
  - Right: current symbol or translated letter  
  - Top-right: TX/RX indicator + Wi‑Fi strength  
- Modes:
  - **DIDACTIC:** large translated letter + blinking cursor  
  - **MORSE:** shows typed Morse symbol (e.g., ".-")  

---

## Wiring Diagram (Connections)

| Component        | ESP8266 Pin | Notes                          |
|------------------|-------------|--------------------------------|
| Local Button     | D5          | INPUT_PULLUP, shorts to GND    |
| Remote Input     | D6          | INPUT_PULLUP                   |
| Buzzer (+)       | D8          | Use transistor if needed       |
| LED (Anode)      | D4          | Through 220 Ω resistor to GND  |
| OLED SDA         | D2          | I2C data                       |
| OLED SCL         | D1          | I2C clock                      |
| OLED VCC         | 3.3V/5V     | Depends on module              |
| OLED GND         | GND         | Common ground                  |
| Button other pin | GND         |                                |
| LED Cathode      | GND         |                                |
| Buzzer (-)       | GND         |                                |



---

## Build & Flash
- Environment: Arduino IDE (ESP8266 core) or PlatformIO  
- Select correct ESP8266 board (NodeMCU, Wemos D1 mini, etc.)  
- Serial monitor: 115200 baud  
- Dependencies: ESP8266 core, Adafruit_GFX, Adafruit_SSD1306  

Steps:
1. Put all project files in a folder  
2. Open in Arduino IDE or configure `platformio.ini`  
3. Select board and port  
4. Compile and upload  
5. Open Serial Monitor at 115200  

---

## Quick Usage & Tests
1. Startup: Wi‑Fi scan, splash, blinker starts  
2. Button test: press D5 → buzzer ON; release → duration logged  
   - ≤150 ms → dot (`.`)  
   - >150 ms → dash (`-`)  
3. Translation: after 800 ms gap, symbol → letter  
4. Network test: two units discover each other via SSID `morse-transceiver` and exchange durations  
5. Blinker: LED flashes message; change with `setBlinkerMessage("TEXT")`  

---

## TCP Protocol
- Port: 5000  
- Messages: `alive`, `duration:<ms>`, `request_tx`, `ok`/`busy`, `mac:<mac>`  
- Heartbeat: every 1s; timeout after 3s  

---

## Public APIs
- **cw-transceiver:** `initCWTransceiver()`, `updateCWTransceiver()`, `getConnectionState()`, `getMode()`, `getCurrentSymbol()`, `getHistoryTX()`, `getHistoryRX()`  
- **network:** `initNetwork()`, `updateNetwork()`, `occupyNetwork()`, `isConnected()`, `sendDuration()`, `getNetworkStrength()`  
- **blinker:** `initBlinker()`, `setBlinkerMessage()`, `updateBlinker()`  
- **display:** `initDisplay()`, `updateDisplay()`  

---

## Timing Constants
- `DEBOUNCE_TIME` = 25 ms  
- `SHORT_PRESS` = 150 ms  
- `LONG_PRESS` = 400 ms  
- `LETTER_GAP` = 800 ms  
- `INACTIVITY_TIMEOUT` = 5000 ms  
- Blinker: DOT=300 ms, DASH=600 ms, SYMBOL_GAP=300 ms, LETTER_GAP=600 ms, WORD_GAP=1800 ms  

---

## Common Issues
- Display not showing → check SDA/SCL wiring and I2C address  
- Wi‑Fi stuck in AP_MODE → check SSID logs  
- Buzzer silent → verify wiring, use transistor if needed  
- Debounce problems → adjust `DEBOUNCE_TIME`  
- Failed translation → add fallback `'?'`  

---

## Suggested Improvements
- Configurable SSID password  
- Reset retryDelay on reconnection  
- Add log levels (DEBUG/INFO)  
- Validate duration values  
- Synchronize blinker timings with CW thresholds  
- Notify display when `occupyNetwork()` fails  

---

## License
Add a `LICENSE` file (e.g., MIT).  

---

## Contributing
Open issues or PRs for:
- Bug fixes  
- Support for different displays  
- Improved network protocol (authentication, reconnection)  
- UX suggestions (visual/audio feedback)  
