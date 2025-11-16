// File: morse-key.cpp
// Implementation of morse-key.h
// ISR-safe, non-blocking. Forwards events to telegrapher_pushKeyEvent()
// Modified: 2025-11-15

#include "morse-key.h"
#include "telegrapher.h" // telegrapher_pushKeyEvent(...)
#include <Arduino.h>

// LOG flag
#ifndef LOG_MK
  #define LOG_MK 1
#endif

static uint8_t key_pin = 0xFF;
static bool key_enabled = false;
static bool use_pullup = true;

// ISR debounce (microseconds)
static const unsigned long ISR_DEBOUNCE_US = 10000UL; // 10 ms
volatile static unsigned long last_isr_us = 0;

// optional debug callback (must be very fast)
static mk_dbg_cb_t dbg_cb = nullptr;

static inline void mk_log(const char* fmt, ...) {
#if LOG_MK
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
#endif
}

// ISR: captures edge timestamps and forwards a TG_KeyEvent to telegrapher
static void IRAM_ATTR mk_isr_handler() {
  unsigned long now_us = micros();
  // simple ISR debounce
  if ((now_us - last_isr_us) < ISR_DEBOUNCE_US) {
    last_isr_us = now_us;
    return;
  }
  last_isr_us = now_us;

  int state = digitalRead(key_pin); // LOW = pressed for typical straight key wiring with pullup
  bool down = (state == LOW);

  // Build TG_KeyEvent and forward to telegrapher (telegrapher_pushKeyEvent is ISR-safe)
  TG_KeyEvent ev;
  ev.down = down;
  ev.t_us = now_us;

  // Forward
  telegrapher_pushKeyEvent(&ev);

  // Optional lightweight debug callback
  if (dbg_cb) dbg_cb(down, now_us);
}

// Public API
void morse_key_init(uint8_t pin, bool pullup) {
  key_pin = pin;
  use_pullup = pullup;
  if (use_pullup) pinMode(key_pin, INPUT_PULLUP);
  else pinMode(key_pin, INPUT);
  // attachInterrupt must be done after pinMode
#if defined(ARDUINO_ARCH_ESP8266) || defined(ESP8266)
  attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
#else
  attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
#endif
  key_enabled = true;
  last_isr_us = micros();
  mk_log("%lu - morse-key initialized on pin %d", millis(), key_pin);
}

void morse_key_setEnabled(bool enabled) {
  if (!key_enabled && enabled) {
    // reattach
#if defined(ARDUINO_ARCH_ESP8266) || defined(ESP8266)
    attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
#else
    attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
#endif
    key_enabled = true;
    mk_log("%lu - morse-key enabled", millis());
  } else if (key_enabled && !enabled) {
    detachInterrupt(digitalPinToInterrupt(key_pin));
    key_enabled = false;
    mk_log("%lu - morse-key disabled", millis());
  }
}

void morse_key_setDebugCallback(mk_dbg_cb_t cb) {
  dbg_cb = cb;
}