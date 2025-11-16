// File: morse-key.h v1.2
// Description: Hardware straight key driver with ISR edge capture, micros debounce and event queue
// Last modification: add duration logging on release; API stable
// Modified: 2025-11-16 00:00
// Created: 2025-11-15

#ifndef MORSE_KEY_H
#define MORSE_KEY_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the key hardware driver
// pin: Arduino digital pin number for the key (e.g., D5 on NodeMCU -> GPIO14)
// pullup: if true enable INPUT_PULLUP (typical for straight key wired to GND when pressed)
//         if false use INPUT (expect external pull-down and button ties to VCC when pressed)
void morse_key_init(uint8_t pin, bool pullup = true);

// Enable or disable the key ISR (useful for tests or power saving)
void morse_key_setEnabled(bool enabled);

// Optional: register a diagnostics callback (called from ISR context so must be very fast)
// The callback receives (down, t_us). Keep it short.
typedef void (*mk_dbg_cb_t)(bool down, unsigned long t_us);
void morse_key_setDebugCallback(mk_dbg_cb_t cb);

// Process pending events recorded by the ISR, emit safe logs with millis and compute press duration.
// Must be called periodically from the main loop (non-ISR context).
void morse_key_process(void);

#ifdef __cplusplus
}
#endif

#endif // MORSE_KEY_H
