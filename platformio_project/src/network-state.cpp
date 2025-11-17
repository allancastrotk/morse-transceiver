// File: network-state.cpp v1.8
// Description: Link state manager (TX/RX/FREE/CONTENTION)
// Last modification: add tracing for lastActivityAt and guards to detect callback re-entry
// Modified: 2025-11-20
// Created: 2025-11-15

#include "network-state.h"
#include <Arduino.h>

#define LOG_STATE_INIT    1
#define LOG_STATE_UPDATE  1
#define LOG_STATE_EVENTS  1
#define LOG_STATE_TRACE   1

// Primary idle timeout used both to detect end-of-letter/word logic and to decide when to release TX.
// For quicker interactive testing you may temporarily reduce this value.
#ifndef STATE_ACTIVITY_TIMEOUT_MS
static const unsigned long STATE_ACTIVITY_TIMEOUT_MS = 5000; // idle -> FREE (ms)
#else
static const unsigned long STATE_ACTIVITY_TIMEOUT_MS = STATE_ACTIVITY_TIMEOUT_MS;
#endif

#ifndef STATE_MIN_TX_MS
static const unsigned long STATE_MIN_TX_MS = 40;
#else
static const unsigned long STATE_MIN_TX_MS = STATE_MIN_TX_MS;
#endif

#ifndef STATE_MIN_RX_MS
static const unsigned long STATE_MIN_RX_MS = 40;
#else
static const unsigned long STATE_MIN_RX_MS = STATE_MIN_RX_MS;
#endif

static ConnectionState currentState = FREE;
static unsigned long lastStateChangeAt = 0;
static unsigned long lastActivityAt = 0;

static bool localPressed = false;
static unsigned long localPressAt = 0;

static bool remotePressed = false;
static unsigned long remotePressAt = 0;

static ns_state_cb_t cb_stateChange = nullptr;
static ns_local_send_cb_t cb_localDown = nullptr;
static ns_local_send_cb_t cb_localUp = nullptr;
static ns_local_symbol_cb_t cb_localSymbol = nullptr;
static ns_remote_symbol_cb_t cb_remoteSymbol = nullptr;

// Temporary guards to detect recursive calls from callbacks.
// These are intentionally simple and only for debugging; they can be removed once root cause is found.
static bool __ns_guard_requestLocalDown = false;
static bool __ns_guard_requestLocalUp = false;
static bool __ns_guard_requestLocalSymbol = false;
static bool __ns_guard_notifyRemoteDown = false;
static bool __ns_guard_notifyRemoteUp = false;
static bool __ns_guard_notifyRemoteSymbol = false;

// Helper: update lastActivityAt with trace tag
static void traceUpdateActivity(unsigned long now, const char *source) {
  unsigned long old = lastActivityAt;
  lastActivityAt = now;
  if (LOG_STATE_TRACE) {
    Serial.printf("%lu - TRACE lastActivityAt updated: old=%lu -> new=%lu (source=%s)\n",
                  millis(), old, lastActivityAt, source);
  }
}

static void doStateChange(ConnectionState s) {
  ConnectionState prev = currentState;
  if (s == currentState) return;
  if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE doStateChange from=%d -> to=%d\n", millis(), (int)prev, (int)s);

  currentState = s;
  lastStateChangeAt = millis();
  // update activity because state transitions count as activity
  traceUpdateActivity(lastStateChangeAt, "doStateChange");

  if (LOG_STATE_EVENTS) Serial.printf("%lu - STATE -> %s\n", lastStateChangeAt,
        (s == TX) ? "TX" : (s == RX) ? "RX" : (s == CONTENTION) ? "CONTENTION" : "FREE");

  if (cb_stateChange) {
    // call callback and trace return -- this helps detect callbacks that re-enter ns_ APIs
    cb_stateChange(s);
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE cb_stateChange returned (state now=%d)\n", millis(), (int)currentState);
  }
}

static void resolveContention() {
  int decision = millis() % 2;
  if (decision == 0) {
    doStateChange(TX);
    if (LOG_STATE_EVENTS) Serial.printf("%lu - contention resolved: local wins (TX)\n", millis());
  } else {
    doStateChange(RX);
    if (LOG_STATE_EVENTS) Serial.printf("%lu - contention resolved: remote wins (RX)\n", millis());
  }
}

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

void ns_requestLocalDown() {
  if (__ns_guard_requestLocalDown) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_requestLocalDown ignored\n", millis());
    return;
  }
  __ns_guard_requestLocalDown = true;

  unsigned long now = millis();
  if (localPressed) {
    __ns_guard_requestLocalDown = false;
    return;
  }
  localPressed = true;
  localPressAt = now;
  traceUpdateActivity(now, "localDown");
  if (LOG_STATE_EVENTS) Serial.printf("%lu - local down requested\n", now);
  if (cb_localDown) cb_localDown();

  if (remotePressed) {
    doStateChange(CONTENTION);
  } else {
    doStateChange(TX);
  }

  // Force immediate re-evaluation (helps if updateNetworkState isn't called fast enough by loop)
  updateNetworkState();

  __ns_guard_requestLocalDown = false;
}

void ns_requestLocalUp() {
  if (__ns_guard_requestLocalUp) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_requestLocalUp ignored\n", millis());
    return;
  }
  __ns_guard_requestLocalUp = true;

  unsigned long now = millis();
  if (!localPressed) {
    __ns_guard_requestLocalUp = false;
    return;
  }
  localPressed = false;
  unsigned long dur = now - localPressAt;

  // start/refresh idle timeout window
  traceUpdateActivity(now, "localUp");

  if (dur < STATE_MIN_TX_MS) {
    if (LOG_STATE_EVENTS) Serial.printf("%lu - local up ignored (glitch dur=%lu)\n", now, dur);
    // even if ignored as glitch, trigger re-eval
    updateNetworkState();
    __ns_guard_requestLocalUp = false;
    return;
  }

  if (LOG_STATE_EVENTS) Serial.printf("%lu - local up requested dur=%lu (state=%d localPressed=%d remotePressed=%d lastActivityAt=%lu)\n",
                                      now, dur, (int)currentState, (int)localPressed, (int)remotePressed, lastActivityAt);
  if (cb_localUp) cb_localUp();

  // If remote is pressing, immediately switch to RX
  if (remotePressed) {
    if (LOG_STATE_EVENTS) Serial.printf("%lu - remote pressed during local up -> switching to RX\n", now);
    doStateChange(RX);
    // re-eval and exit
    updateNetworkState();
    __ns_guard_requestLocalUp = false;
    return;
  }

  // Immediate timeout safeguard: if enough time already passed, free channel immediately
  if (!localPressed && !remotePressed) {
    unsigned long now2 = millis();
    if (now2 - lastActivityAt >= STATE_ACTIVITY_TIMEOUT_MS) {
      if (LOG_STATE_UPDATE) Serial.printf("%lu - ns_requestLocalUp: immediate activity timeout -> FREE\n", now2);
      doStateChange(FREE);
      // re-eval and exit
      updateNetworkState();
      __ns_guard_requestLocalUp = false;
      return;
    }
  }

  // Otherwise, if we were TX or just left contention, keep TX and allow timeout to move to FREE
  if (currentState == TX || currentState == CONTENTION) {
    if (currentState != TX) {
      doStateChange(TX);
    } else {
      if (LOG_STATE_EVENTS) Serial.printf("%lu - local released; maintaining TX until idle timeout (%lums)\n",
                                         now, STATE_ACTIVITY_TIMEOUT_MS);
    }
  }

  // Force re-evaluation immediately after handling local up
  updateNetworkState();

  __ns_guard_requestLocalUp = false;
}

void ns_requestLocalSymbol(char sym, unsigned long dur_ms) {
  if (__ns_guard_requestLocalSymbol) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_requestLocalSymbol ignored\n", millis());
    return;
  }
  __ns_guard_requestLocalSymbol = true;

  unsigned long now = millis();
  traceUpdateActivity(now, "localSymbol");
  if (LOG_STATE_EVENTS) Serial.printf("%lu - local symbol: %c dur=%lu\n", millis(), sym, dur_ms);
  if (cb_localSymbol) cb_localSymbol(sym, dur_ms);

  // Force immediate re-evaluation
  updateNetworkState();

  __ns_guard_requestLocalSymbol = false;
}

void ns_notifyRemoteDown() {
  if (__ns_guard_notifyRemoteDown) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_notifyRemoteDown ignored\n", millis());
    return;
  }
  __ns_guard_notifyRemoteDown = true;

  unsigned long now = millis();
  if (remotePressed) {
    __ns_guard_notifyRemoteDown = false;
    return;
  }
  remotePressed = true;
  remotePressAt = now;
  traceUpdateActivity(now, "remoteDown");
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote down\n", now);

  if (localPressed) {
    doStateChange(CONTENTION);
  } else {
    doStateChange(RX);
  }

  // Force immediate re-evaluation
  updateNetworkState();

  __ns_guard_notifyRemoteDown = false;
}

void ns_notifyRemoteUp() {
  if (__ns_guard_notifyRemoteUp) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_notifyRemoteUp ignored\n", millis());
    return;
  }
  __ns_guard_notifyRemoteUp = true;

  unsigned long now = millis();
  if (!remotePressed) {
    __ns_guard_notifyRemoteUp = false;
    return;
  }
  unsigned long dur = now - remotePressAt;
  remotePressed = false;
  traceUpdateActivity(now, "remoteUp");
  if (dur < STATE_MIN_RX_MS) {
    if (LOG_STATE_EVENTS) Serial.printf("%lu - remote up ignored (glitch dur=%lu)\n", now, dur);
    updateNetworkState();
    __ns_guard_notifyRemoteUp = false;
    return;
  }
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote up dur=%lu\n", now, dur);

  if (currentState == RX || currentState == CONTENTION) {
    if (localPressed) doStateChange(TX);
    else doStateChange(FREE);
  }

  // Force immediate re-evaluation
  updateNetworkState();

  __ns_guard_notifyRemoteUp = false;
}

void ns_notifyRemoteSymbol(char sym, unsigned long dur_ms) {
  if (__ns_guard_notifyRemoteSymbol) {
    if (LOG_STATE_TRACE) Serial.printf("%lu - TRACE recursive call to ns_notifyRemoteSymbol ignored\n", millis());
    return;
  }
  __ns_guard_notifyRemoteSymbol = true;

  unsigned long now = millis();
  traceUpdateActivity(now, "remoteSymbol");
  if (LOG_STATE_EVENTS) Serial.printf("%lu - remote symbol: %c dur=%lu\n", millis(), sym, dur_ms);
  if (cb_remoteSymbol) cb_remoteSymbol(sym, dur_ms);

  // Force immediate re-evaluation
  updateNetworkState();

  __ns_guard_notifyRemoteSymbol = false;
}


void updateNetworkState() {
  unsigned long now = millis();

  if (LOG_STATE_UPDATE) Serial.printf("%lu - updateNetworkState() called (state=%d lastActivityAt=%lu elapsed=%lu)\n",
                                      now, (int)currentState, lastActivityAt, now - lastActivityAt);

  if (currentState == CONTENTION) { resolveContention(); return; }

  static unsigned long lastDbg = 0;
  if (now - lastDbg >= 1000) {
    lastDbg = now;
    if (LOG_STATE_UPDATE) Serial.printf("%lu - DEBUG state=%s localPressed=%d remotePressed=%d lastActivityAt=%lu elapsed=%lu\n",
                                        now,
                                        (currentState==TX)?"TX":(currentState==RX)?"RX":
                                        (currentState==CONTENTION)?"CONTENTION":"FREE",
                                        (int)localPressed, (int)remotePressed, lastActivityAt, now - lastActivityAt);
  }

  // Regular idle timeout behavior
  if (currentState != FREE && (now - lastActivityAt >= STATE_ACTIVITY_TIMEOUT_MS)) {
    if (!localPressed && !remotePressed) {
      if (LOG_STATE_UPDATE) Serial.printf("%lu - activity timeout -> FREE (elapsed=%lu)\n", now, now - lastActivityAt);
      doStateChange(FREE);
      return;
    }
  }

  // Additional guarded release: if no explicit activity-source trace happened for timeout,
  // try to free again and print diagnostics about the last trace source.
  // This requires trace logs to have populated lastActivityAt updates (we already do that).
  static unsigned long lastObservedActivity = 0;
  static unsigned int freeAttempts = 0;

  if (lastObservedActivity != lastActivityAt) {
    // keep the snapshot of the most recent observed update
    lastObservedActivity = lastActivityAt;
    freeAttempts = 0; // reset attempts counter if activity moved
  } else {
    // no observed change since last call; increment an internal timer
    if (now - lastObservedActivity >= STATE_ACTIVITY_TIMEOUT_MS) {
      // If nobody pressed and still TX, force FREE and add diagnostic
      if (currentState == TX && !localPressed && !remotePressed) {
        freeAttempts++;
        if (LOG_STATE_UPDATE) Serial.printf("%lu - diagnostic forced FREE attempt #%u (no new activity observed for %lums)\n",
                                           now, freeAttempts, now - lastObservedActivity);
        doStateChange(FREE);
        return;
      }
    }
  }

  // Fallback: if somehow TX persisted far beyond expected, force FREE (double timeout)
  if (currentState == TX && !localPressed && !remotePressed && (now - lastActivityAt >= (STATE_ACTIVITY_TIMEOUT_MS * 2))) {
    if (LOG_STATE_UPDATE) Serial.printf("%lu - fallback double-timeout -> FREE (elapsed=%lu)\n", now, now - lastActivityAt);
    doStateChange(FREE);
    return;
  }
}