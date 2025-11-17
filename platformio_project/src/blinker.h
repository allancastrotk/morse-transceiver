// File: blinker.h v1.4
// Description: Independent non-blocking Morse LED blinker API (visual decoration only)
// Last modification: unified log flags (INFO/ACTION/NERD) for consistency with other modules
// Modified: 2025-11-16
// Created: 2025-11-15

#ifndef BLINKER_H
#define BLINKER_H

#include <Arduino.h>

// ====== LOG FLAGS (defined in blinker.cpp) ======
// LOG_BLINKER_INFO   -> initialization, phrase build
// LOG_BLINKER_ACTION -> start/stop events
// LOG_BLINKER_NERD   -> phase transitions, detailed debug

// Initialize blinker module.
// ledPin: Arduino digital pin to use for LED; pass 255 to use LED_BUILTIN.
// initialPhrase: optional C-string; if non-null the blinker will build and start looping it.
// Notes:
// - Translator is used only to build the Morse sequence.
// - Blinker is independent: does NOT interact with history, network, buzzer, or other modules.
void initBlinker(uint8_t ledPin = 255, const char* initialPhrase = nullptr);

// Build and start blinking a phrase (letters, digits, spaces).
// Replaces previous phrase and starts looping.
// Internal Morse sequence is built via translator and played back on LED only.
void startBlinker(const char* phrase);

// Stop blinking immediately and clear phrase.
void stopBlinker();

// Non-blocking update; call frequently from main loop to drive playback.
// Must be called continuously in loop() for LED blinking to work.
void updateBlinker();

#endif // BLINKER_H