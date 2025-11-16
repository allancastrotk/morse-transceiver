// File: translator.h v1.1
// Description: Stateless Morse translator (Morse <-> ASCII) with compact lookup table
// Last modification: expose mode control API and align header format with project conventions
// Modified: 2025-11-16
// Created: 2025-11-15

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize translator (no-op but provided for symmetry)
void translator_init(void);

// Mode control
void translator_setModeMorse(void);
void translator_setModeDidatic(void);
bool translator_isDidatic(void);

// Translate a morse sequence (null-terminated, e.g. ".-") to an ASCII uppercase character.
// Returns '\0' if unknown.
char translator_morseToChar(const char* morse);

// Translate an ASCII character to a morse sequence.
// - letter: input char (case-insensitive for letters)
// - outBuf: buffer to receive null-terminated morse string (e.g., ".-") or didactic annotation in DIDATIC mode
// - bufLen: length of outBuf
// Returns true if a mapping was written to outBuf, false if unknown or bufLen too small.
bool translator_charToMorse(char letter, char* outBuf, size_t bufLen);

// Convenience: translate a full morse word (symbols separated by space) into a C-string of letters.
// - morseWord: e.g. "... --- ..." or ".- -..."
// - outBuf: target buffer for ASCII (no separators); bufLen: capacity
// Returns number of characters written (0 if none or error). Unknown sequences are written as '?'.
size_t translator_morseWordToAscii(const char* morseWord, char* outBuf, size_t bufLen);

#ifdef __cplusplus
}
#endif

#endif // TRANSLATOR_H
