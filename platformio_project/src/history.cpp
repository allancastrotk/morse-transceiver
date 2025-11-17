// File: history.cpp v2.3
// Description: TX/RX history storage with unified 29-char circular buffer, versioning, and snapshot slicing.
// Last modification: conditioned TX updates to TX/FREE states, RX updates only in RX state;
//                    overflow logs aggregated into NERD category.
// Modified: 2025-11-18
// Created: 2025-11-15

#include "history.h"
#include "translator.h"
#include "network-state.h"
#include <string.h>
#include <stdarg.h>
#include <Arduino.h>

// ====== LOG FLAGS ======
#define LOG_HISTORY_INFO     1
#define LOG_HISTORY_ACTION   1
#define LOG_HISTORY_NERD     0   // inclui overflow agora

typedef enum { H_LOG_INFO, H_LOG_ACTION, H_LOG_NERD } h_log_cat_t;
static void history_log_cat(h_log_cat_t cat, const char* fmt, ...) {
  if ((cat == H_LOG_INFO && !LOG_HISTORY_INFO) ||
      (cat == H_LOG_ACTION && !LOG_HISTORY_ACTION) ||
      (cat == H_LOG_NERD && !LOG_HISTORY_NERD)) {
    return;
  }
  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  const char* prefix = (cat == H_LOG_INFO) ? "[INFO]" :
                       (cat == H_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - history - %s %s\n", millis(), prefix, body);
}

// ====== Configuration ======
static const size_t HISTORY_VISIBLE = 29;
static const size_t HISTORY_BUF_SZ  = HISTORY_VISIBLE + 1;

// ====== Internal storage ======
static char txBuf[HISTORY_BUF_SZ];
static char rxBuf[HISTORY_BUF_SZ];
static size_t txLen = 0;
static size_t rxLen = 0;

static volatile unsigned long version = 1;

// ====== Helpers ======
static inline void bumpVersion() {
  noInterrupts();
  version++;
  if (version == 0) version = 1;
  interrupts();
#if LOG_HISTORY_NERD
  history_log_cat(H_LOG_NERD, "version bumped -> %lu", version);
#endif
}

static void pushChar(char* buf, size_t& len, char c, const char* tag) {
  if (!buf) return;
  if (len < HISTORY_VISIBLE) {
    buf[len++] = c;
    buf[len] = '\0';
  } else {
    memmove(buf, buf + 1, HISTORY_VISIBLE - 1);
    buf[HISTORY_VISIBLE - 1] = c;
    buf[HISTORY_VISIBLE] = '\0';
#if LOG_HISTORY_NERD
    history_log_cat(H_LOG_NERD, "%s overflow -> scroll", tag);
#endif
  }
  bumpVersion();
#if LOG_HISTORY_ACTION
  history_log_cat(H_LOG_ACTION, "%s updated content=\"%s\"", tag, buf);
#endif
}

static inline void slice10(const char* src, size_t len, size_t start, char* dst) {
  if (!dst) return;
  if (start >= len) { dst[0] = '\0'; return; }
  size_t n = (len - start >= 10) ? 10 : (len - start);
  strncpy(dst, src + start, n);
  dst[n] = '\0';
}

static inline void slice9(const char* src, size_t len, size_t start, char* dst) {
  if (!dst) return;
  if (start >= len) { dst[0] = '\0'; return; }
  size_t n = (len - start >= 9) ? 9 : (len - start);
  strncpy(dst, src + start, n);
  dst[n] = '\0';
}

// ====== Public API ======
void history_init() {
  txBuf[0] = rxBuf[0] = '\0';
  txLen = rxLen = 0;
  noInterrupts();
  version = 1;
  interrupts();
#if LOG_HISTORY_INFO
  history_log_cat(H_LOG_INFO, "initialized (VISIBLE=%u)", (unsigned)HISTORY_VISIBLE);
#endif
}

// TX: só grava se estado for TX ou FREE
void history_pushTXSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  if (!translator_isDidatic() && (ns_getState() == TX || ns_getState() == FREE)) {
    pushChar(txBuf, txLen, sym, "TX");
  }
}

void history_pushTXLetter(char c) {
  if (c == '\0') return;
  if (translator_isDidatic() && (ns_getState() == TX || ns_getState() == FREE)) {
    pushChar(txBuf, txLen, c, "TX");
  }
}

// RX: só grava se estado for RX
void history_pushRXSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  if (ns_getState() == RX) {
    pushChar(rxBuf, rxLen, sym, "RX");
  }
}

void history_pushRXLetter(char c) {
  if (c == '\0') return;
  if (ns_getState() == RX) {
    pushChar(rxBuf, rxLen, c, "RX");
  }
}

void history_onModeChange(bool didatic) {
#if LOG_HISTORY_ACTION
  history_log_cat(H_LOG_ACTION, "mode changed -> %s (no reset)", didatic ? "DIDATIC" : "MORSE");
#endif
}

unsigned long history_getSnapshot(char* outTxTop, char* outTxMid, char* outTxBot,
                                  char* outRxTop, char* outRxMid, char* outRxBot,
                                  size_t /*bufLen*/) {
  noInterrupts();
  unsigned long v = version;

  slice10(txBuf, txLen, 0, outTxTop);
  slice10(txBuf, txLen, 10, outTxMid);
  slice9(txBuf, txLen, 20, outTxBot);

  slice10(rxBuf, rxLen, 0, outRxTop);
  slice10(rxBuf, rxLen, 10, outRxMid);
  slice9(rxBuf, rxLen, 20, outRxBot);

  interrupts();

#if LOG_HISTORY_NERD
  history_log_cat(H_LOG_NERD, "snapshot v=%lu txTop=\"%s\" rxTop=\"%s\"",
                  v, outTxTop ? outTxTop : "(null)", outRxTop ? outRxTop : "(null)");
#endif
  return v;
}

unsigned long history_getVersion() {
  noInterrupts();
  unsigned long v = version;
  interrupts();
  return v;
}

void history_getTXLine(int index, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return;
  outBuf[0] = '\0';
  if (index == 0) {
    strncpy(outBuf, txBuf, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  } else if (index == 1) {
    const char* src = (txLen > 10) ? (txBuf + 10) : "";
    strncpy(outBuf, src, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  } else if (index == 2) {
    const char* src = (txLen > 20) ? (txBuf + 20) : "";
    strncpy(outBuf, src, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  }
}

void history_getRXLine(int index, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return;
  outBuf[0] = '\0';
  if (index == 0) {
    strncpy(outBuf, rxBuf, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  } else if (index == 1) {
    const char* src = (rxLen > 10) ? (rxBuf + 10) : "";
    strncpy(outBuf, src, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  } else if (index == 2) {
    const char* src = (rxLen > 20) ? (rxBuf + 20) : "";
    strncpy(outBuf, src, bufLen - 1);
    outBuf[bufLen - 1] = '\0';
  }
}