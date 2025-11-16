// File: blinker.h v1.3
// Description: Non-blocking Morse LED blinker API using translator (looping, isolated)
// Last modification: API stable; isolated playback (no side effects)
// Modified: 2025-11-15 05:30
// Created: 2025-11-15

#ifndef BLINKER_H
#define BLINKER_H

#include <Arduino.h>

// Initialize blinker module.
// ledPin: Arduino digital pin to use for LED; pass 255 to use LED_BUILTIN.
// initialPhrase: optional C-string; if non-null the blinker will build and start looping it.
void initBlinker(uint8_t ledPin = 255, const char* initialPhrase = nullptr);

// Build and start blinking a phrase (letters, digits, spaces). Replaces previous phrase and starts looping.
// This only builds an internal morse sequence (via translator) and starts playback.
// It does NOT interact with history, network, buzzer, or other modules.
void startBlinker(const char* phrase);

// Stop blinking immediately and clear phrase.
void stopBlinker();

// Non-blocking update; call frequently from main loop to drive playback.
void updateBlinker();

#endif // BLINKER_H
