// File: translator.h
// Description: Stateless Morse translator (Morse <-> ASCII)
// - Provides translation from Morse sequence (e.g. ".-") to ASCII letter and vice-versa
// - Small, deterministic lookup table for A-Z, 0-9 and common punctuation
// - Safe, thread-light (suitable for embedded use)
// Created: 2025-11-15

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <Arduino.h>

// Initialize translator (no-op but provided for symmetry)
void translator_init();

// Translate a morse sequence (null-terminated, e.g. ".-") to an ASCII uppercase character.
// Returns '\0' if unknown.
char translator_morseToChar(const char* morse);

// Translate an ASCII character to a morse sequence.
// - letter: input char (case-insensitive for letters)
// - outBuf: buffer to receive null-terminated morse string (e.g., ".-")
// - bufLen: length of outBuf
// Returns true if a mapping was written to outBuf, false if unknown or bufLen too small.
bool translator_charToMorse(char letter, char* outBuf, size_t bufLen);

// Convenience: translate a full morse word (symbols separated by space) into a C-string of letters.
// - morseWord: e.g. "... --- ..." or ".- -..."
// - outBuf: target buffer for ASCII (no separators); bufLen: capacity
// Returns number of characters written (0 if none or error). Unknown sequences are written as '?'.
size_t translator_morseWordToAscii(const char* morseWord, char* outBuf, size_t bufLen);

#endif