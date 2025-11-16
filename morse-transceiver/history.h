// File: history.h
// Description: History store for TX and RX (letters and symbol traces)
// - Keeps recent TX and RX lines/letters suitable for display
// - Provides atomic snapshot access and a version counter for redraw optimization
// Created: 2025-11-15
#ifndef HISTORY_H
#define HISTORY_H

#include <Arduino.h>

// Initialize history module
void history_init();

// Push events (called by telegrapher / network-state)
// Letters are single ASCII characters (A-Z, 0-9, punctuation)
// Symbols are '.' or '-' and stored as part of current running line
void history_pushTXLetter(char c);
void history_pushRXLetter(char c);
void history_pushTXSymbol(char sym); // '.' or '-'
void history_pushRXSymbol(char sym);

// Query snapshot (atomic-ish): copy current TX/RX visible lines into provided buffers.
// Buffers must be preallocated by caller.
// - outTxTop/outTxMid/outTxBot: target buffers for three TX rows (each must hold at least bufLen chars).
// - outRxTop/outRxMid/outRxBot: target buffers for three RX rows.
// - bufLen: length of each provided buffer (same for all).
// Returns current history version after copy.
unsigned long history_getSnapshot(char* outTxTop, char* outTxMid, char* outTxBot,
                                  char* outRxTop, char* outRxMid, char* outRxBot,
                                  size_t bufLen);

// Convenience getters (atomic-ish)
unsigned long history_getVersion();
void history_getTXLine(int index, char* outBuf, size_t bufLen); // index 0..2 top..bot
void history_getRXLine(int index, char* outBuf, size_t bufLen); // index 0..2 top..bot

#endif // HISTORY_H