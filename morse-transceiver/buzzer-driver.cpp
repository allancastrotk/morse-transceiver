// File: buzzer-driver.cpp
// Implementation of buzzer-driver.h
// - Non-blocking state machine for single beeps and patterns
// - Uses tone()/noTone() when available
// - Safe no-op when initialized with pin == 0
// Modified: 2025-11-15

#include "buzzer-driver.h"
#include <Arduino.h>

#ifndef LOG_BUZZER_DRIVER
  #define LOG_BUZZER_DRIVER 0
#endif

static uint8_t bd_pin = 0;
static bool bd_enabled = false;

// Single-beep state
static bool bd_playing = false;
static unsigned long bd_playUntil = 0;
static unsigned int bd_playFreq = 1000;

// Pattern state
static const unsigned long* bd_pat_ptr = nullptr;
static size_t bd_pat_len = 0;
static size_t bd_pat_idx = 0;
static unsigned long bd_pat_until = 0;
static bool bd_pat_loop = false;
static unsigned int bd_pat_freq = 1000;
static bool bd_pat_phase_on = true; // true = ON phase, false = OFF phase

static void bd_log(const char* fmt, ...) {
#if LOG_BUZZER_DRIVER
  va_list ap; va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
#endif
}

static inline void bd_startTone(unsigned int freq) {
  if (!bd_enabled) return;
#if defined(ARDUINO)
  // tone/noTone are widely available on Arduino cores; call them
  tone(bd_pin, freq);
#else
  // fallback: toggle pin with PWM is platform-specific; still attempt tone if present
  tone(bd_pin, freq);
#endif
  bd_log("buzzer-driver: startTone %uHz", freq);
}

static inline void bd_stopTone() {
  if (!bd_enabled) return;
  noTone(bd_pin);
  bd_log("buzzer-driver: stopTone");
}

void buzzer_driver_init(uint8_t pin) {
  bd_pin = pin;
  if (bd_pin == 0) {
    bd_enabled = false;
    return;
  }
  pinMode(bd_pin, OUTPUT);
  digitalWrite(bd_pin, LOW);
  bd_enabled = true;

  // clear states
  bd_playing = false;
  bd_playUntil = 0;
  bd_pat_ptr = nullptr;
  bd_pat_len = 0;
  bd_pat_idx = 0;
  bd_pat_until = 0;
  bd_pat_loop = false;
  bd_pat_freq = 1000;
  bd_pat_phase_on = true;

  bd_log("%lu - buzzer-driver initialized on pin %u", millis(), bd_pin);
}

void buzzer_driver_update() {
  if (!bd_enabled) return;
  unsigned long now = millis();

  // Single beep has priority
  if (bd_playing) {
    if (now >= bd_playUntil) {
      bd_stopTone();
      bd_playing = false;
      bd_playUntil = 0;
      bd_log("%lu - buzzer-driver beep finished", now);
    }
    return;
  }

  // Pattern playback
  if (bd_pat_ptr && bd_pat_len > 0) {
    if (now >= bd_pat_until) {
      // Advance to next index
      bd_pat_idx++;
      if (bd_pat_idx >= bd_pat_len) {
        if (bd_pat_loop) {
          bd_pat_idx = 0;
        } else {
          // Finished pattern
          bd_pat_ptr = nullptr;
          bd_pat_len = 0;
          bd_pat_idx = 0;
          bd_pat_phase_on = true;
          bd_stopTone();
          bd_log("%lu - buzzer-driver pattern finished", now);
          return;
        }
      }

      unsigned long dur = bd_pat_ptr[bd_pat_idx];
      bd_pat_phase_on = ((bd_pat_idx & 1) == 0); // even => ON, odd => OFF
      if (bd_pat_phase_on) bd_startTone(bd_pat_freq);
      else bd_stopTone();

      // Guard: zero durations become immediate transitions
      bd_pat_until = now + dur;
      bd_log("%lu - buzzer-driver pattern idx=%u phase_on=%d dur=%lu", now, (unsigned)bd_pat_idx, (int)bd_pat_phase_on, dur);
    }
  }
}

void buzzer_driver_beep(unsigned long duration_ms, unsigned int freq_hz) {
  if (!bd_enabled || duration_ms == 0) return;
  // abort any pattern
  bd_pat_ptr = nullptr;
  bd_pat_len = 0;
  bd_pat_idx = 0;
  // start single tone
  bd_playFreq = freq_hz;
  bd_startTone(bd_playFreq);
  bd_playUntil = millis() + duration_ms;
  bd_playing = true;
  bd_log("%lu - buzzer-driver beep start dur=%lu freq=%u", millis(), duration_ms, freq_hz);
}

void buzzer_driver_toneOn(unsigned int freq_hz) {
  if (!bd_enabled) return;
  // abort others
  bd_playing = false;
  bd_pat_ptr = nullptr;
  bd_pat_len = 0;
  bd_pat_idx = 0;
  bd_playUntil = 0;
  bd_startTone(freq_hz);
}

void buzzer_driver_toneOff() {
  if (!bd_enabled) return;
  bd_playing = false;
  bd_pat_ptr = nullptr;
  bd_pat_len = 0;
  bd_pat_idx = 0;
  bd_playUntil = 0;
  bd_stopTone();
}

void buzzer_driver_playPattern(const unsigned long* pattern, size_t patternLen, bool loopPattern, unsigned int freq_hz) {
  if (!bd_enabled || !pattern || patternLen == 0) return;
  // stop single beep
  bd_playing = false;
  bd_playUntil = 0;
  // set pattern state
  bd_pat_ptr = pattern;
  bd_pat_len = patternLen;
  bd_pat_idx = 0;
  bd_pat_loop = loopPattern;
  bd_pat_freq = freq_hz;
  // start first phase immediately
  unsigned long now = millis();
  unsigned long firstDur = bd_pat_ptr[0];
  bd_pat_phase_on = true;
  if (firstDur > 0) {
    bd_startTone(bd_pat_freq);
    bd_pat_until = now + firstDur;
  } else {
    bd_pat_until = now;
  }
  bd_log("%lu - buzzer-driver pattern start freq=%u len=%u firstDur=%lu", now, bd_pat_freq, (unsigned)bd_pat_len, firstDur);
}

void buzzer_driver_playClick() {
  buzzer_driver_beep(50, 2000);
}

void buzzer_driver_playAck() {
  buzzer_driver_beep(150, 1500);
}

void buzzer_driver_onStateChange(ConnectionState state) {
  if (!bd_enabled) return;
  switch (state) {
    case TX:
      // short click to acknowledge local TX
      buzzer_driver_playClick();
      break;
    case RX:
      // different tone for RX
      buzzer_driver_playAck();
      break;
    case FREE:
    default:
      // no sound for FREE
      break;
  }
}