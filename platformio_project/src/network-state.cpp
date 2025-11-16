// File: network-state.cpp v1.0
// Description: Link state manager (TX/RX/FREE) coordinating local/remote events
// Last modification: activity timeouts, priority rules and callback dispatch
// Modified: 2025-11-15 03:36
// Created: 2025-11-15

#include "network-state.h"
#include <Arduino.h>

// LOG FLAGS (all ON as requested)
#define LOG_STATE_INIT  1
#define LOG_STATE_UPDATE 1
#define LOG_STATE_EVENTS 1

// Timers and thresholds (ms)
static const unsigned long STATE_ACTIVITY_TIMEOUT_MS = 5000; // idle -> FREE
static const unsigned long STATE_MIN_TX_MS = 40;  // ignore very brief glitches
static const unsigned long STATE_MIN_RX_MS = 40;

static ConnectionState currentState = FREE;
static unsigned long lastStateChangeAt = 0;
static unsigned long lastActivityAt = 0;

// When local requests start/end
static bool localPressed = false;
static unsigned long localPressAt = 0;

// When remote requests start/end
static bool remotePressed = false;
static unsigned long remotePressAt = 0;

// Callbacks
static ns_state_cb_t cb_stateChange = nullptr;
static ns_local_send_cb_t cb_localDown = nullptr;
static ns_local_send_cb_t cb_localUp = nullptr;
static ns_local_symbol_cb_t cb_localSymbol = nullptr;
static ns_remote_symbol_cb_t cb_remoteSymbol = nullptr;

// Helpers
static void doStateChange(ConnectionState s) {
  if (s == currentState) return;
  currentState = s;
  lastStateChangeAt = millis();
  lastActivityAt = lastStateChangeAt;
  if (LOG_STATE_EVENTS) Serial.printf("%lu - STATE -> %s\n", lastStateChangeAt,
        (s == TX) ? "TX" : (s == RX) ? "RX" : "FREE");
  if (cb_stateChange) cb_stateChange(s);
}

// Public API
void initNetworkState() {
  currentState = FREE;
  lastStateChangeAt = lastActivityAt = millis();
  localPressed = false;
  remotePressed = false;
  localPressAt = remotePressAt = 0;
  cb_stateChange = nullptr;
  cb_localDown = nullptr;
  cb_localUp = nullptr;
  cb_localSymbol = nullptr;
  cb_remoteSymbol = nullptr;
  if (LOG_STATE_INIT) Serial.printf("%lu - network-state initialized (FREE)\n", millis());
}

ConnectionState ns_getState() { return currentState; }
unsigned long ns_lastActivityMs() { return lastActivityAt; }

void ns_onStateChange(ns_state_cb_t cb) { cb_stateChange = cb; }
void ns_onLocalSendDown(ns_local_send_cb_t cb) { cb_localDown = cb; }
void ns_onLocalSendUp(ns_local_send_cb_t cb) { cb_localUp = cb; }
void ns_onLocalSymbol(ns_local_symbol_cb_t cb) { cb_localSymbol = cb; }
void ns_onRemoteSymbol(ns_remote_symbol_cb_t cb) { cb_remoteSymbol = cb; }

// Local requests (from CW)
// ns_requestLocalDown: the user pressed the local key and wants to start TX
void ns_requestLocalDown() {
  unsigned long now = millis();
  if (localPressed) return; // ignore duplicates
  localPressed = true;
  localPressAt = now;
  lastActivityAt = now;
  // Inform lower layer to emit physical DOWN on network (telecom will implement actual send)
  if (LOG_STATE_EVENTS) Serial.printf("%lu - local down requested\n", now);
  if (cb_localDown) cb_localDown();
  // Change state to TX only if not in RX (remote has priority); if in RX, ignore local TX until free
  if (currentState != RX) {
    doStateChange(TX);
  } else {
    if (LOG_STATE_EVENTS) Serial.printf("%lu - local down ignored because in RX\n", now);
  }
}

// ns_requestLocalUp: the user released the key and wants to stop TX
void ns_requestLocalUp() {
  unsigned long now = millis();
  if (!localPressed) return;
  localPressed = false;
  unsigned long dur = now - localPressAt;
  lastActivityAt = now;
  if (LOG_STATE_EVENTS) Serial.printf("%lu - local up requested dur=%lu\n", now, dur);
  if (cb_localUp) cb_localUp();
  // If we were in TX, transition to FREE (or remain RX if remote pressed meanwhile)
  if (currentState == TX) {
    if (remotePressed) {
      doStateChange(RX);
    } else {
      doStateChange(FREE);
    }
  }
}

// called by CW layer to notify a local symbol (dot/dash) — will be forwarded via callback
void ns_requestLocalSymbol(char sym, unsigned long dur_ms) {
  lastActivityAt = millis();
  if (LOG_STATE_EVENTS) Serial.printf("%lu - local symbol: %c dur=%lu\n", millis(), sym, dur_ms);
  if (cb_localSymbol) cb_localSymbol(sym, dur_ms);
}

// Remote notifications (from telecom)
// ns_notifyRemoteDown: remote started pressing — enter RX state (unless local holds TX)
void ns_notifyRemoteDown() {
  unsigned long now = millis();
  if (remotePressed) return;
  remotePressed = true;
  remotePressAt = now;
  lastActivityAt = now;
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote down\n", now);
  // Remote takes precedence: if we're TX locally, we keep TX; else switch to RX
  if (currentState != TX) doStateChange(RX);
}

// ns_notifyRemoteUp: remote released — exit RX or maintain TX
void ns_notifyRemoteUp() {
  unsigned long now = millis();
  if (!remotePressed) return;
  unsigned long dur = now - remotePressAt;
  remotePressed = false;
  lastActivityAt = now;
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote up dur=%lu\n", now, dur);
  // If currently RX, move to FREE unless local is pressed (then go to TX)
  if (currentState == RX) {
    if (localPressed) doStateChange(TX);
    else doStateChange(FREE);
  }
}

// ns_notifyRemoteSymbol: forward remote symbol to interested party
void ns_notifyRemoteSymbol(char sym, unsigned long dur_ms) {
  lastActivityAt = millis();
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote symbol: %c dur=%lu\n", millis(), sym, dur_ms);
  if (cb_remoteSymbol) cb_remoteSymbol(sym, dur_ms);
}

// Main update loop: handle timeouts and transitions
void updateNetworkState() {
  unsigned long now = millis();

  // Activity timeout: if no activity and not pressed by anyone, go FREE
  if (currentState != FREE && (now - lastActivityAt >= STATE_ACTIVITY_TIMEOUT_MS)) {
    if (!localPressed && !remotePressed) {
      if (LOG_STATE_UPDATE) Serial.printf("%lu - activity timeout -> FREE\n", now);
      doStateChange(FREE);
    }
  }

  // Debounce short glitches: if local pressed but duration < min TX, do nothing (handled by CW)
  // No heavy work here — network-state is intentionally lightweight
}