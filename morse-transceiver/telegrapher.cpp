// File: telegrapher.cpp
// Description: Telegrapher implementation — timing, classification, mode toggle and event dispatch
// Corrected: safe queue handling, includes aligned to transceiver_defs.h, robust timing handling
// Modified: 2025-11-15 (corrected)

#include "telegrapher.h"
#include "morse-telecom.h"
#include "network-state.h"
#include <Arduino.h>
#include <string.h>

// LOG FLAGS
#ifndef LOG_TG_EVENTS
  #define LOG_TG_EVENTS 1
#endif
#ifndef LOG_TG_DEBUG
  #define LOG_TG_DEBUG  1
#endif

static tg_local_symbol_cb_t cb_local_symbol = nullptr;
static tg_local_down_cb_t cb_local_down = nullptr;
static tg_local_up_cb_t cb_local_up = nullptr;
static tg_mode_toggle_cb_t cb_mode_toggle = nullptr;

static tg_remote_symbol_cb_t cb_remote_symbol = nullptr;
static tg_remote_down_cb_t cb_remote_down = nullptr;
static tg_remote_up_cb_t cb_remote_up = nullptr;

// Key event queue (ISR -> telegrapher)
#define TG_KEY_Q_SZ 32
static volatile TG_KeyEvent keyQ[TG_KEY_Q_SZ];
static volatile uint8_t keyQ_head = 0;
static volatile uint8_t keyQ_tail = 0;
static volatile uint8_t keyQ_count = 0;

// internal state for local press handling
static bool localPressed = false;
static unsigned long localPress_us = 0;
static unsigned long lastLocalRelease_ms = 0;
static char symbolBuffer[32];
static uint8_t symbolIndex = 0;

// hold detection state
static bool modeHoldReported = false;
static bool inHoldMode = false;
static unsigned long holdStart_ms = 0;

// remote pressed state
static bool remotePressed = false;
static unsigned long remotePressStart_ms = 0;

// logging helpers
static void tlog(bool flag, const char* fmt, ...) {
#if defined(ARDUINO)
  if (!flag) return;
  va_list ap; va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
#endif
}

// ISR-safe enqueue (morse-key should call this)
bool telegrapher_pushKeyEvent(const TG_KeyEvent* ev) {
  if (!ev) return false;
  noInterrupts();
  uint8_t next = (keyQ_tail + 1) % TG_KEY_Q_SZ;
  if (keyQ_count >= TG_KEY_Q_SZ) {
    // drop oldest to make room
    keyQ_head = (keyQ_head + 1) % TG_KEY_Q_SZ;
    keyQ_count--;
  }
  keyQ[keyQ_tail] = *ev;
  keyQ_tail = next;
  keyQ_count++;
  interrupts();
  return true;
}

// dequeue helper (non-ISR context)
static bool keyq_dequeue(TG_KeyEvent* out) {
  if (!out) return false;
  noInterrupts();
  if (keyQ_count == 0) { interrupts(); return false; }
  *out = keyQ[keyQ_head];
  keyQ_head = (keyQ_head + 1) % TG_KEY_Q_SZ;
  keyQ_count--;
  interrupts();
  return true;
}

// Public lifecycle
void telegrapher_init() {
  noInterrupts();
  keyQ_head = keyQ_tail = keyQ_count = 0;
  interrupts();

  localPressed = false;
  localPress_us = 0;
  lastLocalRelease_ms = 0;
  symbolBuffer[0] = '\0';
  symbolIndex = 0;
  modeHoldReported = false;
  inHoldMode = false;
  holdStart_ms = 0;
  remotePressed = false;
  remotePressStart_ms = 0;

  cb_local_symbol = nullptr;
  cb_local_down = nullptr;
  cb_local_up = nullptr;
  cb_mode_toggle = nullptr;
  cb_remote_symbol = nullptr;
  cb_remote_down = nullptr;
  cb_remote_up = nullptr;

  tlog(LOG_TG_EVENTS, "%lu - telegrapher initialized", millis());
}

void telegrapher_onLocalSymbol(tg_local_symbol_cb_t cb) { cb_local_symbol = cb; }
void telegrapher_onLocalDown(tg_local_down_cb_t cb) { cb_local_down = cb; }
void telegrapher_onLocalUp(tg_local_up_cb_t cb) { cb_local_up = cb; }
void telegrapher_onModeToggle(tg_mode_toggle_cb_t cb) { cb_mode_toggle = cb; }

void telegrapher_onRemoteSymbol(tg_remote_symbol_cb_t cb) { cb_remote_symbol = cb; }
void telegrapher_onRemoteDown(tg_remote_down_cb_t cb) { cb_remote_down = cb; }
void telegrapher_onRemoteUp(tg_remote_up_cb_t cb) { cb_remote_up = cb; }

bool telegrapher_isInHoldMode() { return inHoldMode; }

// Handle remote events called by morse-telecom callbacks
void telegrapher_handleRemoteDown() {
  unsigned long now = millis();
  if (remotePressed) return;
  remotePressed = true;
  remotePressStart_ms = now;
  tlog(LOG_TG_EVENTS, "%lu - telegrapher: remote down", now);
  ns_notifyRemoteDown();
  if (cb_remote_down) cb_remote_down();
}

void telegrapher_handleRemoteUp() {
  unsigned long now = millis();
  if (!remotePressed) return;
  unsigned long dur = now - remotePressStart_ms;
  remotePressed = false;
  tlog(LOG_TG_EVENTS, "%lu - telegrapher: remote up dur=%lu", now, dur);
  ns_notifyRemoteUp();
  if (cb_remote_up) cb_remote_up();
}

void telegrapher_handleRemoteSymbol(char sym, unsigned long dur_ms) {
  tlog(LOG_TG_EVENTS, "%lu - telegrapher: remote symbol %c dur=%lu", millis(), sym, dur_ms);
  ns_notifyRemoteSymbol(sym, dur_ms);
  if (cb_remote_symbol) cb_remote_symbol(sym, dur_ms);
}

// Internal helper: finalize buffered local morse sequence into a letter (notify history/translator externally)
static void finalizeLocalBuffer() {
  if (symbolIndex == 0) return;
  tlog(LOG_TG_DEBUG, "%lu - telegrapher: finalize buffer \"%s\"", millis(), symbolBuffer);
  symbolIndex = 0;
  symbolBuffer[0] = '\0';
}

// classify duration (ms) to symbol or ignore
static bool classify_duration_to_symbol(unsigned long dur_ms, char* outSym) {
  if (!outSym) return false;
  if (dur_ms <= DOT_MAX) {
    *outSym = '.';
    return true;
  } else if (dur_ms <= DASH_MAX) {
    *outSym = '-';
    return true;
  }
  return false; // too long -> not a dot/dash
}

// Process a local complete press-release (dur_ms in ms)
static void processLocalPressDuration(unsigned long dur_ms) {
  // Very long presses: treat as control / finalize
  if (dur_ms >= LONG_PRESS_MS) {
    tlog(LOG_TG_EVENTS, "%lu - telegrapher: local very long press dur=%lu -> finalize", millis(), dur_ms);
    finalizeLocalBuffer();
    return;
  }

  // Durations between DASH_MAX and LONG_PRESS_MS: ignore per policy
  if (dur_ms > DASH_MAX) {
    tlog(LOG_TG_DEBUG, "%lu - telegrapher: local press too long for dot/dash dur=%lu ignored", millis(), dur_ms);
    return;
  }

  char sym;
  if (!classify_duration_to_symbol(dur_ms, &sym)) {
    tlog(LOG_TG_DEBUG, "%lu - telegrapher: classify failed dur=%lu", millis(), dur_ms);
    return;
  }

  // Append to local symbol buffer (for DIDACTIC mode)
  if (symbolIndex < (sizeof(symbolBuffer) - 1)) {
    symbolBuffer[symbolIndex++] = sym;
    symbolBuffer[symbolIndex] = '\0';
  }

  // Emit symbol to registered local symbol callback and notify network/state
  if (cb_local_symbol) cb_local_symbol(sym, dur_ms);
  ns_requestLocalSymbol(sym, dur_ms);
  tlog(LOG_TG_EVENTS, "%lu - telegrapher: local symbol %c dur=%lu emitted", millis(), sym, dur_ms);
}

// Main update: drain key queue and manage hold detection/gap finalization
void telegrapher_update() {
  unsigned long now_ms = millis();

  // 1) process queued key events
  TG_KeyEvent ev;
  while (keyq_dequeue(&ev)) {
    if (ev.down) {
      // Key down edge
      if (localPressed) {
        tlog(LOG_TG_DEBUG, "%lu - telegrapher: duplicate local down ignored", now_ms);
      } else {
        localPressed = true;
        localPress_us = ev.t_us;
        holdStart_ms = now_ms;
        modeHoldReported = false;
        tlog(LOG_TG_EVENTS, "%lu - telegrapher: local down (us=%lu)", now_ms, ev.t_us);
        ns_requestLocalDown();
        if (cb_local_down) cb_local_down();
      }
    } else {
      // Key up edge
      if (!localPressed) {
        tlog(LOG_TG_DEBUG, "%lu - telegrapher: stray local up ignored", now_ms);
      } else {
        localPressed = false;
        unsigned long dur_ms = 0;
        if (ev.t_us >= localPress_us) dur_ms = (ev.t_us - localPress_us + 500) / 1000;
        lastLocalRelease_ms = now_ms;
        tlog(LOG_TG_EVENTS, "%lu - telegrapher: local up dur_ms=%lu", now_ms, dur_ms);
        ns_requestLocalUp();
        if (cb_local_up) cb_local_up();
        processLocalPressDuration(dur_ms);
      }
    }
  }

  // 2) hold / mode toggle detection
  if (localPressed && !modeHoldReported) {
    if ((now_ms - holdStart_ms) >= MODE_HOLD_MS) {
      modeHoldReported = true;
      inHoldMode = !inHoldMode;
      tlog(LOG_TG_EVENTS, "%lu - telegrapher: MODE TOGGLE, inHoldMode=%d", now_ms, (int)inHoldMode);
      if (cb_mode_toggle) cb_mode_toggle();
    }
  }

  // 3) letter gap detection: finalize buffered symbols if silence long enough
  if (symbolIndex > 0) {
    if ((now_ms - lastLocalRelease_ms) >= LETTER_GAP_MS) {
      tlog(LOG_TG_EVENTS, "%lu - telegrapher: letter gap reached -> finalize", now_ms);
      finalizeLocalBuffer();
    }
  }
}