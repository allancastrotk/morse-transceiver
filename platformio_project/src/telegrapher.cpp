// File: telegrapher.cpp v1.9
// Description: Telegrapher module (non-blocking, ISR-safe), robust classification, clean init, long-press guard, letter finalize
// Last modification: clean default state on boot, remove dash upper bound, guard long-press for 5s after boot, finalize on release gap
// Modified: 2025-11-18
// Created: 2025-11-15

#include "telegrapher.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>

#ifndef TG_EVENT_Q_SZ
  #define TG_EVENT_Q_SZ 64
#endif

// ====== LOG FLAGS ======
#define LOG_TELEGRAPHER_INFO   0
#define LOG_TELEGRAPHER_ACTION 1
#define LOG_TELEGRAPHER_NERD   0

// minimum down duration to accept as real press (ignore bounces shorter than this)
static const unsigned long MIN_DOWN_MS = 35;

// thresholds
static const unsigned long DOT_MAX_MS    = 200;   // ≤150ms => '.'
static const unsigned long LETTER_GAP_MS = 500;   // silence after release to finalize letter
static const unsigned long LONG_PRESS_MS = 3000;  // long press to toggle mode
static unsigned long boot_ms = 0;                 // for initial long-press guard

// Internal log category enum
typedef enum { TG_LOG_INFO, TG_LOG_ACTION, TG_LOG_NERD } tg_log_cat_t;

// centralized logger for telegrapher (single-line, timestamped)
static void tele_log_cat(tg_log_cat_t cat, const char* fmt, ...) {
  if ((cat == TG_LOG_INFO && !LOG_TELEGRAPHER_INFO) ||
      (cat == TG_LOG_ACTION && !LOG_TELEGRAPHER_ACTION) ||
      (cat == TG_LOG_NERD && !LOG_TELEGRAPHER_NERD)) {
    return;
  }

  char body[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  const char* prefix = (cat == TG_LOG_INFO) ? "[INFO]" :
                       (cat == TG_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - telegrapher - %s %s\n", millis(), prefix, body);
}

// ISR-safe ring buffer for TG_KeyEvent
static volatile TG_KeyEvent tg_q[TG_EVENT_Q_SZ];
static volatile uint8_t tg_q_head = 0;
static volatile uint8_t tg_q_tail = 0;
static volatile uint8_t tg_q_count = 0;

// internal state
static bool local_state_down = false;
static unsigned long last_local_down_ms = 0;
static unsigned long last_local_up_ms = 0;
static unsigned long last_edge_us = 0;
static bool long_press_handled = false;

// local callbacks
static tg_local_symbol_cb_t cb_local_symbol = nullptr;
static tg_simple_cb_t cb_local_down = nullptr;
static tg_simple_cb_t cb_local_up = nullptr;
static tg_simple_cb_t cb_finalize = nullptr;
static tg_simple_cb_t cb_longpress = nullptr;

// remote callbacks
static tg_remote_symbol_cb_t cb_remote_symbol = nullptr;
static tg_remote_simple_cb_t cb_remote_down = nullptr;
static tg_remote_simple_cb_t cb_remote_up = nullptr;

// Current letter buffer for local symbols (stores spaced symbols like ". -" or compact)
static char letter_buf[32];
static size_t letter_len = 0;

// Helpers: letter buffer
static inline void letter_clear() {
  letter_len = 0;
  letter_buf[0] = '\0';
}

static inline void letter_push_symbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  // store with spaces to keep readability (". -" etc.)
  if (letter_len > 0) {
    if (letter_len + 1 < sizeof(letter_buf)) {
      letter_buf[letter_len++] = ' ';
    }
  }
  if (letter_len + 1 < sizeof(letter_buf)) {
    letter_buf[letter_len++] = sym;
    letter_buf[letter_len] = '\0';
  }
}

// Classification: no upper bound for dash
static inline char classifySymbol(unsigned long dur_ms) {
  return (dur_ms <= DOT_MAX_MS) ? '.' : '-';
}

void telegrapher_setLogLevel(bool enable) {
  (void)enable;
  // runtime control not implemented; keep compile-time flags
}

void telegrapher_pushKeyEvent(const TG_KeyEvent* ev) {
  // ISR context: keep small and safe
  uint8_t next = (uint8_t)((tg_q_tail + 1) % TG_EVENT_Q_SZ);

  noInterrupts();
  if (tg_q_count >= TG_EVENT_Q_SZ) {
    // drop oldest
    tg_q_head = (uint8_t)((tg_q_head + 1) % TG_EVENT_Q_SZ);
    tg_q_count--;
#if LOG_TELEGRAPHER_NERD
    tele_log_cat(TG_LOG_NERD, "overflow, dropping oldest event");
#endif
  }
  tg_q[tg_q_tail].down = ev->down;
  tg_q[tg_q_tail].t_us = ev->t_us;
  tg_q_tail = next;
  tg_q_count++;
  interrupts();
}

void telegrapher_init(void) {
  noInterrupts();
  tg_q_head = 0;
  tg_q_tail = 0;
  tg_q_count = 0;

  local_state_down = false;
  last_local_down_ms = 0;
  last_local_up_ms = 0;
  last_edge_us = 0;
  long_press_handled = false;

  cb_local_symbol = nullptr;
  cb_local_down = nullptr;
  cb_local_up = nullptr;
  cb_finalize = nullptr;
  cb_longpress = nullptr;

  cb_remote_symbol = nullptr;
  cb_remote_down = nullptr;
  cb_remote_up = nullptr;
  interrupts();

  boot_ms = millis();
  letter_clear();

#if LOG_TELEGRAPHER_INFO
  tele_log_cat(TG_LOG_INFO, "initialized");
#endif
}

// pop helper (non-ISR)
static bool tg_pop_event(TG_KeyEvent* out) {
  if (tg_q_count == 0) return false;
  noInterrupts();
  if (tg_q_count == 0) { interrupts(); return false; }
  memcpy(out, (const void*)&tg_q[tg_q_head], sizeof(TG_KeyEvent));
  tg_q_head = (uint8_t)((tg_q_head + 1) % TG_EVENT_Q_SZ);
  tg_q_count--;
  interrupts();
  return true;
}

// process a single edge (non-ISR)
static void process_edge_nonisr(bool down, unsigned long edge_us, unsigned long process_ms) {
  if (down) {
    if (local_state_down) return; // duplicate down
    local_state_down = true;
    last_local_down_ms = process_ms;
    last_edge_us = edge_us;
    long_press_handled = false; // allow detection for this press
    letter_clear();             // start fresh letter on new press boundary
    if (cb_local_down) cb_local_down();
#if LOG_TELEGRAPHER_ACTION
    tele_log_cat(TG_LOG_ACTION, "local down us=%lu", edge_us);
#endif
  } else {
    if (!local_state_down) return; // duplicate up
    local_state_down = false;
    last_local_up_ms = process_ms; // start letter-gap countdown
    unsigned long dur = process_ms - last_local_down_ms;

    // Reject short bounce
    if (dur < MIN_DOWN_MS) {
#if LOG_TELEGRAPHER_NERD
      tele_log_cat(TG_LOG_NERD, "ignored bounce dur=%lu", dur);
#endif
      if (cb_local_up) cb_local_up();
      return;
    }

    // If consumed as long-press, do not classify
    if (long_press_handled) {
      long_press_handled = false;
      if (cb_local_up) cb_local_up();
#if LOG_TELEGRAPHER_ACTION
      tele_log_cat(TG_LOG_ACTION, "local up (long-press consumed) dur_ms=%lu", dur);
#endif
      return;
    }

    // Classify symbol (no upper bound for dash)
    char sym = classifySymbol(dur);

    // Emit callbacks
    letter_push_symbol(sym);
    if (cb_local_symbol) cb_local_symbol(sym, dur);

#if LOG_TELEGRAPHER_ACTION
    tele_log_cat(TG_LOG_ACTION, "local up dur_ms=%lu symbol=%c", dur, sym);
#endif

    if (cb_local_up) cb_local_up();
  }
}

void telegrapher_update(void) {
  TG_KeyEvent ev;

  // Process all queued edges
  while (tg_pop_event(&ev)) {
    unsigned long now_ms = millis();
    process_edge_nonisr(ev.down, ev.t_us, now_ms);
  }

  // Long-press detection (guarded in first 5s after boot)
  if (local_state_down && !long_press_handled) {
    unsigned long now = millis();
    if ((now - boot_ms) > 5000 && (now - last_local_down_ms) >= LONG_PRESS_MS) {
#if LOG_TELEGRAPHER_ACTION
      tele_log_cat(TG_LOG_ACTION, "long press -> longpress callback");
#endif
      if (cb_longpress) cb_longpress();
      long_press_handled = true;
    }
  }

  // Finalize letter after silence since last release
  if (!local_state_down && last_local_up_ms != 0) {
    unsigned long now = millis();
    if ((now - last_local_up_ms) >= LETTER_GAP_MS) {
#if LOG_TELEGRAPHER_ACTION
      tele_log_cat(TG_LOG_ACTION, "letter gap reached -> finalize");
#endif
      if (cb_finalize) cb_finalize();
      last_local_up_ms = 0;
      letter_clear();
    }
  }
}

// remote handlers (non-ISR)
void telegrapher_handleRemoteDown(void) {
#if LOG_TELEGRAPHER_ACTION
  tele_log_cat(TG_LOG_ACTION, "remote down");
#endif
  if (cb_remote_down) cb_remote_down();
}

void telegrapher_handleRemoteUp(void) {
#if LOG_TELEGRAPHER_ACTION
  tele_log_cat(TG_LOG_ACTION, "remote up");
#endif
  if (cb_remote_up) cb_remote_up();
}

void telegrapher_handleRemoteSymbol(char sym, unsigned long dur_ms) {
#if LOG_TELEGRAPHER_ACTION
  tele_log_cat(TG_LOG_ACTION, "remote symbol %c dur=%lu", sym, dur_ms);
#endif
  if (cb_remote_symbol) cb_remote_symbol(sym, dur_ms);
}

// local callback registrations
void telegrapher_onLocalSymbol(tg_local_symbol_cb_t cb) { cb_local_symbol = cb; }
void telegrapher_onLocalDown(tg_simple_cb_t cb)         { cb_local_down = cb; }
void telegrapher_onLocalUp(tg_simple_cb_t cb)           { cb_local_up = cb; }
void telegrapher_onFinalize(tg_simple_cb_t cb)          { cb_finalize = cb; }
void telegrapher_onLongPress(tg_simple_cb_t cb)         { cb_longpress = cb; }

// compatibility shim
void telegrapher_onModeToggle(tg_simple_cb_t cb)        { cb_longpress = cb; }

// remote callback registrations
void telegrapher_onRemoteSymbol(tg_remote_symbol_cb_t cb) { cb_remote_symbol = cb; }
void telegrapher_onRemoteDown(tg_remote_simple_cb_t cb)   { cb_remote_down = cb; }
void telegrapher_onRemoteUp(tg_remote_simple_cb_t cb)     { cb_remote_up = cb; }