// File: morse-key.cpp v2.1-fixed
// Description: Morse key hardware interface. Debounce reforçado, proteção contra retrigger,
//              logs TRACE para diagnóstico, envia eventos ao telegrapher apenas quando estáveis.
// Modified: 2025-11-17
// Created: 2025-11-15

#include "morse-key.h"
#include "telegrapher.h"
#include <Arduino.h>

// ====== LOG FLAGS ======
#define LOG_MK_INFO    1
#define LOG_MK_ACTION  1
#define LOG_MK_NERD    0

typedef enum { MK_LOG_INFO, MK_LOG_ACTION, MK_LOG_NERD } mk_log_cat_t;
static void mk_log_cat(mk_log_cat_t cat, const char* fmt, ...) {
  if ((cat == MK_LOG_INFO && !LOG_MK_INFO) ||
      (cat == MK_LOG_ACTION && !LOG_MK_ACTION) ||
      (cat == MK_LOG_NERD && !LOG_MK_NERD)) return;
  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  const char* prefix = (cat == MK_LOG_INFO) ? "[INFO]" :
                       (cat == MK_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - morse-key - %s %s\n", millis(), prefix, body);
}

// ====== Config ======
static int keyPin = -1;
static bool pullup = true;
static void (*dbgCb)(bool, unsigned long) = nullptr;

static bool lastStableState = false;   // state that was emitted
static bool lastReadState = false;     // last raw read
static unsigned long lastChangeMs = 0; // last raw transition time
static unsigned long lastEmitMs = 0;   // last time we emitted an event

// Minimum stable time to consider new state valid
static const unsigned long DEBOUNCE_MS = 60; // aumentado de 40 -> 60ms

// Minimum time between successive emitted events to avoid retrigger from odd hardware
static const unsigned long MIN_EMIT_GAP_MS = 40;

// ====== API ======
void morse_key_init(int pin, bool usePullup) {
  keyPin = pin;
  pullup = usePullup;
  if (pullup) pinMode(keyPin, INPUT_PULLUP);
  else pinMode(keyPin, INPUT);

  lastStableState = digitalRead(keyPin) == LOW;
  lastReadState = lastStableState;
  lastChangeMs = millis();
  lastEmitMs = 0;

  mk_log_cat(MK_LOG_INFO, "initialized pin=%d pullup=%s stable=%d", keyPin, pullup ? "true" : "false", (int)lastStableState);
}

void morse_key_setDebugCallback(void (*cb)(bool, unsigned long)) {
  dbgCb = cb;
}

// ====== Processing ======
void morse_key_process() {
  bool rawState = (digitalRead(keyPin) == LOW); // pressed = LOW if pullup
  unsigned long nowMs = millis();

  // TRACE raw readings for diagnostics (low-noise unless needed)
  #if LOG_MK_NERD
  Serial.printf("%lu - TRACE morse-key raw=%d lastRead=%d lastStable=%d\n", nowMs, rawState, lastReadState, lastStableState);
  #endif

  if (rawState != lastReadState) {
    // raw changed, reset debounce timer
    lastReadState = rawState;
    lastChangeMs = nowMs;
    return; // wait for stability period
  }

  // raw is equal to lastReadState; check if stable long enough
  if ((nowMs - lastChangeMs) < DEBOUNCE_MS) {
    return; // still within debounce window
  }

  // Now rawState has been stable for DEBOUNCE_MS
  if (lastStableState == lastReadState) {
    // nothing new to emit
    return;
  }

  // Protect against extremely rapid emits (hardware chatter): require a small gap
  if (lastEmitMs != 0 && (nowMs - lastEmitMs) < MIN_EMIT_GAP_MS) {
    // ignore as potential bounce/retrigger
    #if LOG_MK_NERD
    Serial.printf("%lu - TRACE morse-key ignored rapid retrigger (gap=%lums)\n", nowMs, nowMs - lastEmitMs);
    #endif
    return;
  }

  // Accept the new stable state and emit event
  lastStableState = lastReadState;
  lastEmitMs = nowMs;

  TG_KeyEvent ev;
  ev.down = lastStableState;
  ev.t_us = micros();

  // Diagnostic log
  if (lastStableState) {
    Serial.printf("%lu - morse-key - [ACTION] pressed at us=%lu\n", nowMs, ev.t_us);
  } else {
    Serial.printf("%lu - morse-key - [ACTION] released at us=%lu\n", nowMs, ev.t_us);
  }

  // push to telegrapher
  telegrapher_pushKeyEvent(&ev);

  // debug callback if present
  if (dbgCb) dbgCb(lastStableState, ev.t_us);
}