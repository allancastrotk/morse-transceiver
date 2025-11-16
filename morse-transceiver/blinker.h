// File: blinker.h
// Non-blocking Morse LED blinker API (corrigido)
// Created: 2025-11-15 (corrigido)

#ifndef BLINKER_H
#define BLINKER_H

#include <Arduino.h>

// Initialize blinker module. If ledPin == 255 uses LED_BUILTIN.
void initBlinker(uint8_t ledPin = 255);

// Start blinking a phrase (letters, digits, spaces). Replaces previous phrase.
// phrase may be NULL or empty to do nothing.
void startBlinker(const char* phrase);

// Stop blinking immediately.
void stopBlinker();

// Call frequently from the main loop to drive the non-blocking state machine.
void updateBlinker();

#endif // BLINKER_H