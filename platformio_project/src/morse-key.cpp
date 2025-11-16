// File: morse-key.cpp v1.4
// Description: Straight key driver — logs formatted as "<millis> - morse-key - <action>".
// Last modification: emit press as "pressed" and release as "duration <N> ms"
// Modified: 2025-11-16
// Created: 2025-11-15

#include "morse-key.h"
#include "telegrapher.h" // telegrapher_pushKeyEvent(...), TG_KeyEvent
#include <Arduino.h>
#include <stdarg.h>
#include <string.h> // memcpy

// ====== LOG FLAGS ======
#define LOG_MORSE_KEY_INFO   0
#define LOG_MORSE_KEY_ACTION 1
#define LOG_MORSE_KEY_NERD   0

#ifndef IRAM_ATTR
  #define IRAM_ATTR
#endif

#ifndef ISR_DEBOUNCE_US
  // Increased default debounce to reduce bouncing-related duplicate edges
  #define ISR_DEBOUNCE_US 20000UL // 20 ms
#endif

#ifndef MK_EVENT_Q_SZ
  #define MK_EVENT_Q_SZ 32
#endif

typedef struct {
  bool down;
  unsigned long t_us;   // micros() captured in ISR
  unsigned long t_ms;   // millis() filled in process()
} MK_Event;

static uint8_t key_pin = 0xFF;
static bool key_enabled = false;
static bool use_pullup = true;

volatile static unsigned long last_isr_us = 0;
static mk_dbg_cb_t dbg_cb = nullptr;

static volatile MK_Event mk_q[MK_EVENT_Q_SZ];
static volatile uint8_t mk_q_head = 0;
static volatile uint8_t mk_q_tail = 0;
static volatile uint8_t mk_q_count = 0;

static unsigned long last_press_ms = 0;
static bool last_press_valid = false;

// Diagnostic counters
static volatile unsigned long mk_dropped_events = 0;
static volatile unsigned long mk_duplicate_isr = 0;

// Keep last ISR raw state to filter identical consecutive reads (reduces bounce noise)
static volatile int last_isr_state = -1;

// Internal log category enum (internal only)
typedef enum { MK_LOG_INFO, MK_LOG_ACTION, MK_LOG_NERD } mk_log_cat_t;

// centralized logger for morse-key (safe outside ISR)
// categories map to project LOG flags:
//   MK_LOG_INFO  -> LOG_MORSE_KEY_INFO
//   MK_LOG_ACTION-> LOG_MORSE_KEY_ACTION
//   MK_LOG_NERD  -> LOG_MORSE_KEY_NERD
static void mk_log_cat(mk_log_cat_t cat, const char* fmt, ...) {
  // Filter by category flags (respect requested naming)
  if ((cat == MK_LOG_INFO  && !LOG_MORSE_KEY_INFO)  ||
      (cat == MK_LOG_ACTION && !LOG_MORSE_KEY_ACTION)||
      (cat == MK_LOG_NERD   && !LOG_MORSE_KEY_NERD)) {
    return;
  }

  char body[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  const char* prefix = (cat == MK_LOG_INFO) ? "[INFO]" :
                       (cat == MK_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - morse-key - %s %s\n", millis(), prefix, body);
}

// Backwards-compatible simple mk_log mapped to INFO (legacy callers)
static void mk_log(const char* fmt, ...) {
#if (LOG_MORSE_KEY_INFO || LOG_MORSE_KEY_ACTION || LOG_MORSE_KEY_NERD)
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
#endif
}

static void IRAM_ATTR mk_isr_handler() {
  unsigned long now_us = micros();

  // ISR debounce (fast check)
  if ((now_us - last_isr_us) < ISR_DEBOUNCE_US) {
    last_isr_us = now_us;
    mk_duplicate_isr++;
    return;
  }
  last_isr_us = now_us;

  int state = digitalRead(key_pin);

  // Simple ISR-level filter: ignore if same raw state as last ISR (reduces bounce spikes)
  if (state == last_isr_state) {
    mk_duplicate_isr++;
    return;
  }
  last_isr_state = state;

  bool down = use_pullup ? (state == LOW) : (state == HIGH);

  // Forward minimal event to telegrapher immediately (ISR-safe struct)
  TG_KeyEvent ev;
  ev.down = down;
  ev.t_us = now_us;
  telegrapher_pushKeyEvent(&ev);

  // Enqueue into local ring buffer (drop oldest if full)
  uint8_t next_tail = (uint8_t)((mk_q_tail + 1) % MK_EVENT_Q_SZ);

  noInterrupts();
  if (mk_q_count >= MK_EVENT_Q_SZ) {
    // drop oldest to make room and account for diagnostic
    mk_q_head = (uint8_t)((mk_q_head + 1) % MK_EVENT_Q_SZ);
    mk_q_count--;
    mk_dropped_events++;
    // cannot log here (ISR context); we report later from morse_key_process
  }

  mk_q[mk_q_tail].down = down;
  mk_q[mk_q_tail].t_us = now_us;
  mk_q[mk_q_tail].t_ms = 0;
  mk_q_tail = next_tail;
  mk_q_count++;
  interrupts();

  if (dbg_cb) dbg_cb(down, now_us);
}

void morse_key_init(uint8_t pin, bool pullup) {
  key_pin = pin;
  use_pullup = pullup;

  if (use_pullup) {
    pinMode(key_pin, INPUT_PULLUP);
  } else {
    pinMode(key_pin, INPUT);
  }

  last_isr_us = micros();
  last_isr_state = digitalRead(key_pin);
  attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
  key_enabled = true;

#if LOG_MORSE_KEY_INFO
  // Show human-friendly pin label + GPIO number and pullup state
  mk_log_cat(MK_LOG_INFO, "initialized on %s -> GPIO%u pullup %s",
             "D5", (unsigned)key_pin, use_pullup ? "on" : "off");
#endif
}

void morse_key_setEnabled(bool enabled) {
  noInterrupts();
  if (!key_enabled && enabled) {
    attachInterrupt(digitalPinToInterrupt(key_pin), mk_isr_handler, CHANGE);
    key_enabled = true;
#if LOG_MORSE_KEY_ACTION
    mk_log_cat(MK_LOG_ACTION, "enabled");
#endif
  } else if (key_enabled && !enabled) {
    detachInterrupt(digitalPinToInterrupt(key_pin));
    key_enabled = false;
#if LOG_MORSE_KEY_ACTION
    mk_log_cat(MK_LOG_ACTION, "disabled");
#endif
  }
  interrupts();
}

void morse_key_setDebugCallback(mk_dbg_cb_t cb) {
  dbg_cb = cb;
}

void morse_key_process(void) {
  // If diagnostic events happened in ISR that we cannot log there, emit summary early.
  if (mk_dropped_events != 0 || mk_duplicate_isr != 0) {
#if LOG_MORSE_KEY_NERD
    mk_log_cat(MK_LOG_NERD, "ISR diagnostics: dropped=%lu duplicates=%lu",
               mk_dropped_events, mk_duplicate_isr);
#endif
    // Reset diagnostics after reporting
    mk_dropped_events = 0;
    mk_duplicate_isr = 0;
  }

  while (true) {
    // Fast path: check count without disabling interrupts
    if (mk_q_count == 0) return;

    noInterrupts();
    if (mk_q_count == 0) { interrupts(); return; }

    // Copy head event to local stack variable (safe) then advance head
    MK_Event ev_local;
    memcpy(&ev_local, (const void*)&mk_q[mk_q_head], sizeof(MK_Event));
    mk_q_head = (uint8_t)((mk_q_head + 1) % MK_EVENT_Q_SZ);
    mk_q_count--;
    interrupts();

    ev_local.t_ms = millis();

#if LOG_MORSE_KEY_ACTION
    if (ev_local.down) {
      // Pressed: log the press moment
      last_press_ms = ev_local.t_ms;
      last_press_valid = true;
      mk_log_cat(MK_LOG_ACTION, "pressed");
    } else {
      // Released: log duration if matched with a previous press
      if (last_press_valid) {
        unsigned long dur = ev_local.t_ms - last_press_ms;
        mk_log_cat(MK_LOG_ACTION, "release %lu ms", dur);
        last_press_valid = false;
      } else {
        // No matched press recorded; log release as unknown duration
        mk_log_cat(MK_LOG_ACTION, "duration UNKNOWN");
      }
    }
#endif
  }
}
