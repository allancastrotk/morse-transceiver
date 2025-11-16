// File: history.cpp
// Implementation of history.h
// - Simple circular storage for 3 TX and 3 RX lines
// - Each line is a small ring buffer (char array) with append semantics
// - Uses a version counter incremented on mutations so display can redraw only when needed
// Modified: 2025-11-15 (corrected buffer handling and atomic snapshot)

#include "history.h"
#include <string.h>

#ifndef LOG_HISTORY
  #define LOG_HISTORY 0
#endif

// Configuration: characters per line (must fit display)
static const size_t LINE_LEN = 32; // max chars per stored line (including NUL)
static const int LINES_N = 3;      // number of lines per direction

// Internal storage
static char txLines[LINES_N][LINE_LEN];
static char rxLines[LINES_N][LINE_LEN];

// Current lengths (number of characters currently in each line, excluding NUL)
static size_t txLen[LINES_N];
static size_t rxLen[LINES_N];

// Rolling pointers: which line is "top" (0 = top-most recent)
static int txTopIdx = 0;
static int rxTopIdx = 0;

// Version counter (volatile because updated from different contexts)
static volatile unsigned long version = 1; // increment on changes

// Helpers
static inline void h_initLines() {
  for (int i = 0; i < LINES_N; ++i) {
    txLines[i][0] = '\0';
    rxLines[i][0] = '\0';
    txLen[i] = 0;
    rxLen[i] = 0;
  }
  txTopIdx = 0;
  rxTopIdx = 0;
  version = 1;
}

static inline void bumpVersion() {
  version++;
  if (version == 0) version = 1;
}

// Append a single character to a fixed-size line buffer with simple left-shift on overflow.
// buf: destination buffer (size LINE_LEN), len: current length (by-ref), ch: char to append
static void pushCharToLine(char buf[LINE_LEN], size_t &len, char ch) {
  if (!buf) return;
  // If there is room for one more char + NUL
  if (len + 1 < LINE_LEN) {
    buf[len] = ch;
    len++;
    buf[len] = '\0';
    return;
  }
  // No room: drop the oldest char (shift left by one) and append at end-1
  memmove(buf, buf + 1, LINE_LEN - 2);        // preserve up to LINE_LEN-2 chars
  buf[LINE_LEN - 2] = ch;
  buf[LINE_LEN - 1] = '\0';
  len = LINE_LEN - 2 + 1; // chars stored including the new one
}

// Push a symbol ('.' or '-') into the current top line
static void pushSymbolToTop(char lines[LINES_N][LINE_LEN], size_t lens[LINES_N], int &topIdx, char sym) {
  pushCharToLine(lines[topIdx], lens[topIdx], sym);
}

// Start a new line entry for a letter: advance top index and append the letter as first char
static void pushLetterAsNewEntry(char lines[LINES_N][LINE_LEN], size_t lens[LINES_N], int &topIdx, char letter) {
  topIdx = (topIdx + 1) % LINES_N;
  lens[topIdx] = 0;
  lines[topIdx][0] = '\0';
  pushCharToLine(lines[topIdx], lens[topIdx], letter);
}

// Public API

void history_init() {
  h_initLines();
#if LOG_HISTORY
  Serial.printf("%lu - history initialized\n", millis());
#endif
}

void history_pushTXSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  pushSymbolToTop(txLines, txLen, txTopIdx, sym);
  bumpVersion();
}

void history_pushRXSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  pushSymbolToTop(rxLines, rxLen, rxTopIdx, sym);
  bumpVersion();
}

void history_pushTXLetter(char c) {
  if (c == '\0') return;
  pushLetterAsNewEntry(txLines, txLen, txTopIdx, c);
  bumpVersion();
}

void history_pushRXLetter(char c) {
  if (c == '\0') return;
  pushLetterAsNewEntry(rxLines, rxLen, rxTopIdx, c);
  bumpVersion();
}

// snapshot helpers
static void copyLogicalLine(char dest[], size_t destLen, char srcLines[LINES_N][LINE_LEN], int topIdx, int logicalIdx) {
  if (!dest || destLen == 0) return;
  int idx = (topIdx + logicalIdx) % LINES_N;
  if (idx < 0) idx += LINES_N;
  // safe copy
  strncpy(dest, srcLines[idx], destLen - 1);
  dest[destLen - 1] = '\0';
}

unsigned long history_getSnapshot(char* outTxTop, char* outTxMid, char* outTxBot,
                                  char* outRxTop, char* outRxMid, char* outRxBot,
                                  size_t bufLen) {
  if (bufLen == 0) return version;
  noInterrupts();
  unsigned long v = version;
  // copy TX
  copyLogicalLine(outTxTop, bufLen, txLines, txTopIdx, 0);
  copyLogicalLine(outTxMid, bufLen, txLines, txTopIdx, 1);
  copyLogicalLine(outTxBot, bufLen, txLines, txTopIdx, 2);
  // copy RX
  copyLogicalLine(outRxTop, bufLen, rxLines, rxTopIdx, 0);
  copyLogicalLine(outRxMid, bufLen, rxLines, rxTopIdx, 1);
  copyLogicalLine(outRxBot, bufLen, rxLines, rxTopIdx, 2);
  interrupts();
  return v;
}

unsigned long history_getVersion() {
  return version;
}

void history_getTXLine(int index, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return;
  if (index < 0 || index >= LINES_N) { outBuf[0] = '\0'; return; }
  noInterrupts();
  int idx = (txTopIdx + index) % LINES_N;
  if (idx < 0) idx += LINES_N;
  strncpy(outBuf, txLines[idx], bufLen - 1);
  outBuf[bufLen - 1] = '\0';
  interrupts();
}

void history_getRXLine(int index, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return;
  if (index < 0 || index >= LINES_N) { outBuf[0] = '\0'; return; }
  noInterrupts();
  int idx = (rxTopIdx + index) % LINES_N;
  if (idx < 0) idx += LINES_N;
  strncpy(outBuf, rxLines[idx], bufLen - 1);
  outBuf[bufLen - 1] = '\0';
  interrupts();
}