# Morse Transceiver — README (English)

A lightweight and modular **ESP8266 Morse CW Transceiver** firmware.
Supports **local keying (physical button)** and **remote keying (Wi-Fi/TCP)**, with real-time feedback via **buzzer**, **OLED SSD1306 display**, and a **LED blinker subsystem**.

Designed around a fully **non-blocking architecture** (`millis()` + `yield()`), ensuring responsiveness during multitasking operations.

---

## 🚀 Features

* **Local Morse key input** (ISR + debounce)
* **Remote TCP input/output** (simple text protocol)
* **Real-time OLED UI** with history, symbol preview, and network status
* **Non-blocking buzzer driver** for audio feedback
* **LED blinker** that converts text → Morse → blinking pattern
* **Modular architecture** with clear responsibilities
* **Robust circular history buffers** (TX/RX)
* **State machine for transmission coordination** (FREE / TX / RX)

---

## 🧩 Architecture Overview

The firmware is divided into independent, testable modules:

* **morse-key** → Captures physical button events
* **telegrapher** → Morse timing logic, symbol classification, character assembly
* **morse-telecom** → TCP text protocol (port 5000)
* **network-connect** → Wi-Fi scanning, reconnecting, socket handling
* **network-state** → TX/RX/FREE state machine
* **display-adapter** → OLED rendering, caching, symbol display
* **history** → Circular buffers for TX/RX text
* **buzzer-driver** → Non-blocking audio patterns
* **blinker** → Background LED Morse playback
* **translator** → ASCII ↔ Morse mapping

Each module exposes `init()` and `update()` methods for clean integration.

---

## 📡 Communication (TCP Protocol)

The transceiver exposes a simple TCP socket that exchanges text messages:

```
alive
request_tx
ok
busy
duration:<ms>
mac:<mac_address>
```

Used for remote keying and timing synchronization.

---

## 🖥️ Display Layout (SSD1306)

Left side → **TX/RX history** (29 visible characters)
Right side → **Current symbol/letter** (shown for 1.5s)
Top-right → **Wi-Fi indicator**

Supports caching to avoid unnecessary redraws.

---

## 🔧 Development Notes

* 100% **non-blocking**: no `delay()`
* Built around `millis()` timing
* Logs include timestamps
* All buffers are bounds-checked
* Modular callbacks prevent cross-dependencies
* Fixed **3-second splash screen**

---

## 🧪 Testing Checklist

* Button press → buzzer ON, release → duration logged
* ≤150ms → dot, >150ms → dash
* 800ms pause → character emitted
* Confirm TCP link stability
* Verify display history + symbol preview
* LED blinker runs independently

**Common issues include:**

* Blank OLED → check SDA/SCL
* Wi-Fi stuck → verify SSID/password
* No buzzer output → inspect transistor stage

---

## 📘 Recommended Improvements

* Configurable Wi-Fi credentials
* Log levels (DEBUG/INFO/WARN/ERROR)
* TCP packet validation and checksum
* Blinker timing alignment with telegrapher thresholds
* Export history over Wi-Fi
* Further isolation between modules

---

## 📄 License

**MIT License**

Credits to:

* Adafruit GFX / SSD1306
* ESP8266 Arduino Core

Author: **Allan**

---

## 📌 Summary

This firmware delivers a complete Morse transceiver stack for ESP8266, combining hardware interaction, wireless communication, display management, and real-time sig
