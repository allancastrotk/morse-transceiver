// File: buzzer-driver.h
// Description: Non-blocking buzzer driver for Morse project
// - Plays simple beeps and patterns without blocking the main loop
// - Can react to network-state changes via a user callback
// Created: 2025-11-15
#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include <Arduino.h>
#include "network-state.h"

// Initialize buzzer on pin (use 0 to disable)
void buzzer_driver_init(uint8_t pin);

// Call frequently from loop()
void buzzer_driver_update();

// Simple actions (non-blocking)
void buzzer_driver_beep(unsigned long duration_ms, unsigned int freq_hz = 1000); // single beep
void buzzer_driver_toneOn(unsigned int freq_hz = 1000);                           // start continuous tone
void buzzer_driver_toneOff();                                                     // stop tone

// Patterns: array of on/off durations (ms). patternLen is number of entries.
// pattern example: {100, 50, 100} -> beep 100ms, silence 50ms, beep 100ms
// If loopPattern is true the pattern repeats.
void buzzer_driver_playPattern(const unsigned long* pattern, size_t patternLen, bool loopPattern = false, unsigned int freq_hz = 1000);

// Convenience: play a short click (used on local press) and longer ack (remote)
void buzzer_driver_playClick();  // short click ~50ms
void buzzer_driver_playAck();    // ack ~150ms

// React to state change (network-state)
void buzzer_driver_onStateChange(ConnectionState state);

#endif // BUZZER_DRIVER_H