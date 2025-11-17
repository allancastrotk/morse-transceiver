// File:        morse-telecom.cpp v1.2
// Project:     Morse Transceiver
// Description: Lightweight framing/parser; send helpers and remote event callbacks
// Last modification: added MAC-based origin filter to avoid RX echo
// Modified:    2025-11-16
// Created:     2025-11-15
//
// License:     MIT License

#include "morse-telecom.h"
#include "network-connect.h" // nc_enqueueOutgoing(...)
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <string.h>

// ====== LOG FLAGS ======
#define LOG_TELECOM_INFO   1
#define LOG_TELECOM_ACTION 1
#define LOG_TELECOM_NERD   0

static mt_remote_down_cb_t cb_remote_down = nullptr;
static mt_remote_up_cb_t cb_remote_up = nullptr;
static mt_remote_symbol_cb_t cb_remote_symbol = nullptr;

// local queue (optional)
#define MT_LOCAL_Q_SZ 8
struct MTLocalQ { char line[64]; unsigned long ts; };
static MTLocalQ localQ[MT_LOCAL_Q_SZ];
static int lqHead = 0;
static int lqTail = 0;
static int lqCount = 0;

// cache local MAC
static String localMac;

static void logf(bool flag, const char* fmt, ...) {
  if (!flag) return;
  va_list ap; va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
}

static bool localq_flush_one() {
  if (lqCount == 0) return true;
  nc_enqueueOutgoing(localQ[lqHead].line);
  logf(LOG_TELECOM_ACTION, "%lu - [ACTION] telecom flush -> nc_enqueue: %s",
       millis(), localQ[lqHead].line);
  lqHead = (lqHead + 1) % MT_LOCAL_Q_SZ;
  lqCount--;
  return true;
}

void morse_telecom_init() {
  lqHead = lqTail = lqCount = 0;
  cb_remote_down = nullptr;
  cb_remote_up = nullptr;
  cb_remote_symbol = nullptr;
  localMac = WiFi.macAddress();
  logf(LOG_TELECOM_INFO, "%lu - [INFO] morse-telecom initialized (MAC=%s)",
       millis(), localMac.c_str());
}

void morse_telecom_update() {
  if (lqCount > 0) localq_flush_one();
}

// ====== Sending helpers ======
void morse_telecom_sendDown() {
  char buf[64];
  snprintf(buf, sizeof(buf), "DOWN;src:%s", localMac.c_str());
  nc_enqueueOutgoing(buf);
  logf(LOG_TELECOM_ACTION, "%lu - [ACTION] telecom sendDown queued: %s", millis(), buf);
}

void morse_telecom_sendUp() {
  char buf[64];
  snprintf(buf, sizeof(buf), "UP;src:%s", localMac.c_str());
  nc_enqueueOutgoing(buf);
  logf(LOG_TELECOM_ACTION, "%lu - [ACTION] telecom sendUp queued: %s", millis(), buf);
}

void morse_telecom_sendSymbol(char sym, unsigned long dur_ms) {
  if (sym != '.' && sym != '-') return;
  char buf[64];
  snprintf(buf, sizeof(buf), "sym:%c;dur:%lu;src:%s", sym, dur_ms, localMac.c_str());
  nc_enqueueOutgoing(buf);
  logf(LOG_TELECOM_ACTION, "%lu - [ACTION] telecom sendSymbol queued: %s", millis(), buf);
}

// ====== Incoming line handler ======
void morse_telecom_handleIncomingLine(const char* line) {
  if (!line || line[0] == '\0') return;

  // filtro: se veio de nós mesmos, ignorar
  if (strstr(line, localMac.c_str())) {
    logf(LOG_TELECOM_NERD, "%lu - [NERD] Ignored self-originated line: %s", millis(), line);
    return;
  }

  logf(LOG_TELECOM_ACTION, "%lu - [ACTION] telecom RX: %s", millis(), line);

  if (strcmp(line, "alive") == 0 || strcmp(line, "alive_ack") == 0) {
    return;
  }
  if (strncmp(line, "DOWN", 4) == 0) {
    if (cb_remote_down) cb_remote_down();
    return;
  }
  if (strncmp(line, "UP", 2) == 0) {
    if (cb_remote_up) cb_remote_up();
    return;
  }
  if (strncmp(line, "sym:", 4) == 0 || strncmp(line, "r_sym:", 6) == 0) {
    const char* p = strchr(line, ':');
    if (!p) return;
    p++;
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

  logf(LOG_TELECOM_NERD, "%lu - [NERD] telecom RX unknown: %s", millis(), line);
}

// ====== Callbacks registration ======
void morse_telecom_onRemoteDown(mt_remote_down_cb_t cb) { cb_remote_down = cb; }
void morse_telecom_onRemoteUp(mt_remote_up_cb_t cb) { cb_remote_up = cb; }
void morse_telecom_onRemoteSymbol(mt_remote_symbol_cb_t cb) { cb_remote_symbol = cb; }