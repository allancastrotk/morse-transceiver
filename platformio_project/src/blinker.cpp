// File: blinker.cpp v1.4
// Description: Independent non-blocking Morse LED blinker (visual decoration only)
// Last modification: unified log flags (INFO/ACTION/NERD) for consistency
// Modified: 2025-11-16
// Created: 2025-11-15

#include "blinker.h"
#include "translator.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>

// ====== LOG FLAGS ======
#define LOG_BLINKER_INFO   0   // init, phrase build
#define LOG_BLINKER_ACTION 0   // start/stop events
#define LOG_BLINKER_NERD   0   // phase transitions, detailed debug

// ====== CONFIG ======
static uint8_t BLINKER_LED_PIN = LED_BUILTIN;

// Timings (ms)
static const unsigned long DOT_TIME   = 300;
static const unsigned long DASH_TIME  = 600;
static const unsigned long GAP_SYMBOL = 300;
static const unsigned long GAP_LETTER = 600;
static const unsigned long GAP_WORD   = 1800;

// Internal buffer
static char morseBuffer[512];
static size_t morseLength = 0;

// Playback state
static size_t playPos = 0;
static bool phaseOn = false;
static unsigned long phaseUntil = 0;
static bool running = false;

// ====== HELPERS ======
static inline bool timeNotReached(unsigned long now, unsigned long until) {
  return (long)(now - until) < 0;
}

static void safeAppend(const char* s) {
  if (!s || !*s) return;
  size_t cur = strlen(morseBuffer);
  size_t add = strlen(s);
  if (cur + add + 1 >= sizeof(morseBuffer)) {
    size_t room = sizeof(morseBuffer) - 1 - cur;
    if (room > 0) {
      strncat(morseBuffer, s, room);
      morseBuffer[sizeof(morseBuffer) - 1] = '\0';
    }
  } else {
    strcat(morseBuffer, s);
  }
}

static void buildMorseFromPhrase(const char* phrase) {
  morseBuffer[0] = '\0';
  if (!phrase) { morseLength = 0; return; }

  char morseToken[32];
  for (size_t i = 0; phrase[i] != '\0'; ++i) {
    char c = phrase[i];
    if (c == ' ') { safeAppend("/ "); continue; }
    if (!translator_charToMorse(c, morseToken, sizeof(morseToken))) continue;
    safeAppend(morseToken);
    safeAppend(" ");
  }
  morseLength = strlen(morseBuffer);
  if (LOG_BLINKER_INFO) Serial.printf("%lu - blinker built morse: %s\n", millis(), morseBuffer);
}

// ====== PUBLIC API ======
void initBlinker(uint8_t ledPin, const char* initialPhrase) {
  if (ledPin != 255) BLINKER_LED_PIN = ledPin;
  pinMode(BLINKER_LED_PIN, OUTPUT);
  digitalWrite(BLINKER_LED_PIN, LOW);

  translator_init();

  morseBuffer[0] = '\0';
  morseLength = 0;
  running = false;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;

  if (LOG_BLINKER_INFO) Serial.printf("%lu - blinker init on pin %u\n", millis(), BLINKER_LED_PIN);

  if (initialPhrase && initialPhrase[0] != '\0') {
    buildMorseFromPhrase(initialPhrase);
    if (morseLength > 0) {
      running = true;
      playPos = 0;
      phaseOn = false;
      phaseUntil = 0;
    }
  }
}

void startBlinker(const char* phrase) {
  if (!phrase || phrase[0] == '\0') return;
  buildMorseFromPhrase(phrase);
  if (morseLength == 0) return;
  running = true;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  digitalWrite(BLINKER_LED_PIN, LOW);
  if (LOG_BLINKER_ACTION) Serial.printf("%lu - blinker started phrase: %s\n", millis(), phrase);
}

void stopBlinker() {
  running = false;
  morseBuffer[0] = '\0';
  morseLength = 0;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  digitalWrite(BLINKER_LED_PIN, LOW);
  if (LOG_BLINKER_ACTION) Serial.printf("%lu - blinker stopped\n", millis());
}

void updateBlinker() {
  if (!running || morseLength == 0) return;
  unsigned long now = millis();

  if (phaseUntil != 0 && timeNotReached(now, phaseUntil)) return;

  if (phaseOn) {
    phaseOn = false;
    digitalWrite(BLINKER_LED_PIN, LOW);
    phaseUntil = now + GAP_SYMBOL;
    if (LOG_BLINKER_NERD) Serial.printf("%lu - LED OFF, gap %lu ms\n", millis(), GAP_SYMBOL);
    return;
  }

  if (playPos >= morseLength) {
    playPos = 0;
    phaseUntil = now + GAP_WORD;
    digitalWrite(BLINKER_LED_PIN, LOW);
    if (LOG_BLINKER_NERD) Serial.printf("%lu - sequence end -> loop gap %lu ms\n", millis(), GAP_WORD);
    return;
  }

  char c = morseBuffer[playPos++];
  if (c == ' ') { phaseUntil = now + GAP_LETTER; return; }
  if (c == '/') { phaseUntil = now + GAP_WORD; if (playPos < morseLength && morseBuffer[playPos] == ' ') ++playPos; return; }
  if (c == '.') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DOT_TIME;
    if (LOG_BLINKER_NERD) Serial.printf("%lu - DOT\n", millis());
    return;
  }
  if (c == '-') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DASH_TIME;
    if (LOG_BLINKER_NERD) Serial.printf("%lu - DASH\n", millis());
    return;
  }
}