// File: morse-key.h
// Description: Hardware key (straight key) driver for Morse project
// - ISR-driven edge capture with microsecond timestamps
// - Debounce in ISR using micros
// - Forwards KeyDown/KeyUp events to telegrapher via telegrapher_pushKeyEvent()
// Created: 2025-11-15

#ifndef MORSE_KEY_H
#define MORSE_KEY_H

#include <Arduino.h>

// Initialize the key hardware driver
// pin: Arduino digital pin number for the key (active LOW expected)
// pullup: if true enable INPUT_PULLUP, otherwise INPUT
void morse_key_init(uint8_t pin, bool pullup = true);

// Enable or disable the key ISR (useful for tests or power saving)
void morse_key_setEnabled(bool enabled);

// Optional: register a diagnostics callback (called from ISR context so must be very fast)
// The callback receives (down, t_us). Keep it short.
typedef void (*mk_dbg_cb_t)(bool down, unsigned long t_us);
void morse_key_setDebugCallback(mk_dbg_cb_t cb);

#endif