// File: morse-telecom.cpp v1.0
// Description: Lightweight framing/parser; send helpers and remote event callbacks
// Last modification: local queue, RX parser and integration with network-connect
// Modified: 2025-11-15 03:36
// Created: 2025-11-15

#include "morse-telecom.h"
#include "network-connect.h" // nc_enqueueOutgoing(...)
#include <Arduino.h>
#include <string.h>

// LOG FLAGS (set to 1 as requested)
#define LOG_TELECOM 1
#define LOG_TELECOM_RX 1
#define LOG_TELECOM_TX 1

static mt_remote_down_cb_t cb_remote_down = nullptr;
static mt_remote_up_cb_t cb_remote_up = nullptr;
static mt_remote_symbol_cb_t cb_remote_symbol = nullptr;

// small local send queue (optional extra buffering before handing to network-connect)
// In typical use we call nc_enqueueOutgoing immediately, but keep a tiny local queue for resilience.
#define MT_LOCAL_Q_SZ 8
struct MTLocalQ { char line[64]; unsigned long ts; };
static MTLocalQ localQ[MT_LOCAL_Q_SZ];
static int lqHead = 0;
static int lqTail = 0;
static int lqCount = 0;

static void logf(bool flag, const char* fmt, ...) {
  if (!flag) return;
  va_list ap; va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
}

static void localq_push(const char* line) {
  if (!line || !*line) return;
  unsigned long now = millis();
  if (lqCount >= MT_LOCAL_Q_SZ) {
    // drop oldest
    lqHead = (lqHead + 1) % MT_LOCAL_Q_SZ;
    lqCount--;
  }
  strncpy(localQ[lqTail].line, line, sizeof(localQ[lqTail].line) - 1);
  localQ[lqTail].line[sizeof(localQ[lqTail].line) - 1] = '\0';
  localQ[lqTail].ts = now;
  lqTail = (lqTail + 1) % MT_LOCAL_Q_SZ;
  lqCount++;
  logf(LOG_TELECOM_TX, "%lu - telecom localQ enq: %s (q=%d)", now, line, lqCount);
}

static bool localq_flush_one() {
  if (lqCount == 0) return true;
  // try to push to network-connect immediately
  nc_enqueueOutgoing(localQ[lqHead].line);
  logf(LOG_TELECOM_TX, "%lu - telecom flush -> nc_enqueue: %s", millis(), localQ[lqHead].line);
  lqHead = (lqHead + 1) % MT_LOCAL_Q_SZ;
  lqCount--;
  return true;
}

void morse_telecom_init() {
  lqHead = lqTail = lqCount = 0;
  cb_remote_down = nullptr;
  cb_remote_up = nullptr;
  cb_remote_symbol = nullptr;
  Serial.printf("%lu - morse-telecom initialized\n", millis());
}

void morse_telecom_update() {
  // flush one local queued item per update to avoid bursts
  if (lqCount > 0) localq_flush_one();
}

// Sending helpers
void morse_telecom_sendDown() {
  // format: DOWN
  const char* ln = "DOWN";
  // first try to send directly via network-connect
  nc_enqueueOutgoing(ln);
  logf(LOG_TELECOM_TX, "%lu - telecom sendDown queued: %s", millis(), ln);
}

void morse_telecom_sendUp() {
  const char* ln = "UP";
  nc_enqueueOutgoing(ln);
  logf(LOG_TELECOM_TX, "%lu - telecom sendUp queued: %s", millis(), ln);
}

void morse_telecom_sendSymbol(char sym, unsigned long dur_ms) {
  // validate symbol
  if (sym != '.' && sym != '-') return;
  char buf[64];
  snprintf(buf, sizeof(buf), "sym:%c;dur:%lu", sym, dur_ms);
  // push locally then to network
  // prefer direct enqueue, but push into local queue if needed
  nc_enqueueOutgoing(buf);
  logf(LOG_TELECOM_TX, "%lu - telecom sendSymbol queued: %s", millis(), buf);
}

// Incoming line handler — to be called by network-connect when a line arrives.
// Lightweight parser: recognizes alive/alive_ack, DOWN/UP, sym: and r_sym:
void morse_telecom_handleIncomingLine(const char* line) {
  if (!line) return;
  if (line[0] == '\0') return;

  logf(LOG_TELECOM_RX, "%lu - telecom RX: %s", millis(), line);

  if (strcmp(line, "alive") == 0) {
    // network-connect handles heartbeat reply; no-op here
    return;
  }
  if (strcmp(line, "alive_ack") == 0) {
    return;
  }
  if (strcmp(line, "DOWN") == 0) {
    if (cb_remote_down) cb_remote_down();
    return;
  }
  if (strcmp(line, "UP") == 0) {
    if (cb_remote_up) cb_remote_up();
    return;
  }
  // parse sym: or r_sym:
  if (strncmp(line, "sym:", 4) == 0 || strncmp(line, "r_sym:", 6) == 0) {
    // find symbol and dur
    const char* p = strchr(line, ':');
    if (!p) return;
    p++; // after colon
    char sym = p[0];
    const char* durp = strstr(line, "dur:");
    unsigned long dur = 0;
    if (durp) {
      durp += 4;
      dur = strtoul(durp, NULL, 10);
    }
    if (cb_remote_symbol) cb_remote_symbol(sym, dur);
    return;
  }

  // unknown message: ignore or log
  logf(LOG_TELECOM_RX, "%lu - telecom RX unknown: %s", millis(), line);
}

// Callbacks registration
void morse_telecom_onRemoteDown(mt_remote_down_cb_t cb) { cb_remote_down = cb; }
void morse_telecom_onRemoteUp(mt_remote_up_cb_t cb) { cb_remote_up = cb; }
void morse_telecom_onRemoteSymbol(mt_remote_symbol_cb_t cb) { cb_remote_symbol = cb; }