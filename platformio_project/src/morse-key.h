// File: morse-key.h v1.2
// Description: Morse key hardware interface. Handles pin setup, debounce,
//              press/release detection, and pushes events to telegrapher.
// Last modification: aligned with project standard; added debug callback.
// Modified: 2025-11-18
// Created: 2025-11-15

#ifndef MORSE_KEY_H
#define MORSE_KEY_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize key hardware
// pin: GPIO number
// usePullup: true = INPUT_PULLUP, false = INPUT
void morse_key_init(int pin, bool usePullup);

// Optional debug callback (pressed?, timestamp_us)
void morse_key_setDebugCallback(void (*cb)(bool, unsigned long));

// Process key state (call in loop)
void morse_key_process(void);

#ifdef __cplusplus
}
#endif

#endif // MORSE_KEY_H
