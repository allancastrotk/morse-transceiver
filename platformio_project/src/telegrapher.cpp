// File: telegrapher.cpp v2.1
// Description: Morse telegrapher state machine. Classifies key press durations into dot/dash,
//              handles finalize gaps, long-press detection, and dispatches callbacks.
// Last modification: fixed cursor logic, ignore too-long presses, long-press threshold 3s,
//                    finalize fires once per cycle.
// Modified: 2025-11-18
// Created: 2025-11-15

#include "telegrapher.h"
#include <Arduino.h>

// ====== LOG FLAGS ======
#define LOG_TELEG_INFO    1
#define LOG_TELEG_ACTION  1
#define LOG_TELEG_NERD    0

typedef enum { T_LOG_INFO, T_LOG_ACTION, T_LOG_NERD } t_log_cat_t;
static void telegrapher_log_cat(t_log_cat_t cat, const char* fmt, ...) {
  if ((cat == T_LOG_INFO && !LOG_TELEG_INFO) ||
      (cat == T_LOG_ACTION && !LOG_TELEG_ACTION) ||
      (cat == T_LOG_NERD && !LOG_TELEG_NERD)) return;
  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  const char* prefix = (cat == T_LOG_INFO) ? "[INFO]" :
                       (cat == T_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - telegrapher - %s %s\n", millis(), prefix, body);
}

// ====== Timing thresholds ======
static const unsigned long DOT_THRESHOLD_MS   = 200;   // <=200ms = dot
static const unsigned long DASH_THRESHOLD_MS  = 600;   // <=600ms = dash
static const unsigned long LETTER_GAP_MS      = 500;   // gap to finalize
static const unsigned long LONG_PRESS_MS      = 3000;  // 3s to toggle mode

// ====== State ======
static bool isDown = false;
static unsigned long downStartMs = 0;
static bool longPressFired = false;
static unsigned long lastUpMs = 0;
static bool finalizePending = false;

// ====== Callbacks ======
static tg_local_symbol_cb_t cbLocalSymbol = nullptr;
static tg_simple_cb_t cbLocalDown = nullptr;
static tg_simple_cb_t cbLocalUp = nullptr;
static tg_simple_cb_t cbFinalize = nullptr;
static tg_simple_cb_t cbLongPress = nullptr;
static tg_simple_cb_t cbModeToggle = nullptr;

static tg_remote_symbol_cb_t cbRemoteSymbol = nullptr;
static tg_remote_simple_cb_t cbRemoteDown = nullptr;
static tg_remote_simple_cb_t cbRemoteUp = nullptr;

// ====== API ======
void telegrapher_init(void) {
  isDown = false;
  downStartMs = 0;
  longPressFired = false;
  lastUpMs = millis();
  finalizePending = false;
  telegrapher_log_cat(T_LOG_INFO, "initialized");
}

void telegrapher_update(void) {
  unsigned long now = millis();
  if (isDown) {
    if (!longPressFired && (now - downStartMs >= LONG_PRESS_MS)) {
      longPressFired = true;
      if (cbLongPress) cbLongPress();
      if (cbModeToggle) cbModeToggle();
      telegrapher_log_cat(T_LOG_ACTION, "long press -> callback fired");
    }
  } else {
    if (finalizePending && (now - lastUpMs) >= LETTER_GAP_MS) {
      finalizePending = false; // dispara só uma vez
      if (cbFinalize) cbFinalize();
      telegrapher_log_cat(T_LOG_ACTION, "letter gap reached -> finalize");
    }
  }
}

void telegrapher_pushKeyEvent(const TG_KeyEvent* ev) {
  if (!ev) return;
  if (ev->down) {
    // key down
    isDown = true;
    downStartMs = millis();
    longPressFired = false;
    if (cbLocalDown) cbLocalDown();
    telegrapher_log_cat(T_LOG_ACTION, "key down us=%lu", ev->t_us);
  } else {
    // key up
    isDown = false;
    lastUpMs = millis();
    finalizePending = true; // rearmar finalize

    unsigned long durMs = (millis() - downStartMs);

    if (durMs <= DASH_THRESHOLD_MS) {
      char sym = (durMs <= DOT_THRESHOLD_MS) ? '.' : '-';
      if (cbLocalSymbol) cbLocalSymbol(sym, durMs);
      if (cbLocalUp) cbLocalUp();
      telegrapher_log_cat(T_LOG_ACTION, "key up dur_ms=%lu symbol=%c", durMs, sym);
    } else {
      // ignorar pressões muito longas
      if (cbLocalUp) cbLocalUp();
      telegrapher_log_cat(T_LOG_ACTION, "key up dur_ms=%lu ignored (too long)", durMs);
    }
  }
}

// ====== Callback registration ======
void telegrapher_onLocalSymbol(tg_local_symbol_cb_t cb) { cbLocalSymbol = cb; }
void telegrapher_onLocalDown(tg_simple_cb_t cb) { cbLocalDown = cb; }
void telegrapher_onLocalUp(tg_simple_cb_t cb) { cbLocalUp = cb; }
void telegrapher_onFinalize(tg_simple_cb_t cb) { cbFinalize = cb; }
void telegrapher_onLongPress(tg_simple_cb_t cb) { cbLongPress = cb; }
void telegrapher_onModeToggle(tg_simple_cb_t cb) { cbModeToggle = cb; }

void telegrapher_onRemoteSymbol(tg_remote_symbol_cb_t cb) { cbRemoteSymbol = cb; }
void telegrapher_onRemoteDown(tg_remote_simple_cb_t cb) { cbRemoteDown = cb; }
void telegrapher_onRemoteUp(tg_remote_simple_cb_t cb) { cbRemoteUp = cb; }
