// File: blinker.cpp (corrigido)
// Non-blocking Morse LED blinker using translator
// Created: 2025-11-15 (corrigido)

#include "blinker.h"
#include "translator.h"
#include <string.h>
#include <ctype.h>

static uint8_t BLINKER_LED_PIN = LED_BUILTIN;

// Timings (ms)
static const unsigned long DOT_TIME   = 120;
static const unsigned long DASH_TIME  = 360;
static const unsigned long GAP_SYMBOL = 120;
static const unsigned long GAP_LETTER = 360;
static const unsigned long GAP_WORD   = 800;

static char morseBuffer[512];
static size_t morseLength = 0u;

// Playback state
static size_t playPos = 0u;
static bool phaseOn = false;             // currently LED ON (dot/dash) or OFF (gaps)
static unsigned long phaseUntil = 0u;
static bool running = false;

// safe millis compare (wrap-aware)
static inline bool timeNotReached(unsigned long now, unsigned long until) {
  return (long)(now - until) < 0;
}

static void safeAppend(const char* s) {
  if (!s || !*s) return;
  size_t cur = strlen(morseBuffer);
  size_t add = strlen(s);
  if (cur + add + 1 >= sizeof(morseBuffer)) {
    // not enough room, drop trailing content (keep as much as fits)
    size_t room = sizeof(morseBuffer) - 1 - cur;
    if (room > 0) {
      strncat(morseBuffer, s, room);
      morseBuffer[sizeof(morseBuffer) - 1] = '\0';
    }
  } else {
    strcat(morseBuffer, s);
  }
}

void initBlinker(uint8_t ledPin) {
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
}

void startBlinker(const char* phrase) {
  if (!phrase) return;
  morseBuffer[0] = '\0';
  morseLength = 0;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  running = false;

  char morseToken[32];

  for (size_t i = 0; phrase[i] != '\0'; ++i) {
    char c = phrase[i];
    if (c == ' ') {
      // word separator: use '/' then a space to simplify parsing
      safeAppend("/ ");
      continue;
    }
    if (!translator_charToMorse(c, morseToken, sizeof(morseToken))) {
      // skip unknown characters
      continue;
    }
    safeAppend(morseToken);
    safeAppend(" "); // letter separator
  }

  morseLength = strlen(morseBuffer);
  if (morseLength > 0) {
    running = true;
    playPos = 0;
    phaseOn = false;
    phaseUntil = 0;
    digitalWrite(BLINKER_LED_PIN, LOW);
  }
}

void stopBlinker() {
  running = false;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  morseBuffer[0] = '\0';
  morseLength = 0;
  digitalWrite(BLINKER_LED_PIN, LOW);
}

void updateBlinker() {
  if (!running || morseLength == 0) return;
  unsigned long now = millis();

  // if we're inside a timed phase and not yet reached, return
  if (phaseUntil != 0 && timeNotReached(now, phaseUntil)) return;

  // just finished an ON phase
  if (phaseOn) {
    phaseOn = false;
    digitalWrite(BLINKER_LED_PIN, LOW);
    // symbol gap after dot/dash
    phaseUntil = now + GAP_SYMBOL;
    return;
  }

  // OFF phase finished or starting fresh: advance to next symbol
  if (playPos >= morseLength) {
    // reached end -> loop with word gap
    playPos = 0;
    phaseUntil = now + GAP_WORD;
    digitalWrite(BLINKER_LED_PIN, LOW);
    return;
  }

  // read next char and advance safely
  char c = morseBuffer[playPos++];
  if (c == ' ') {
    // letter gap
    phaseUntil = now + GAP_LETTER;
    digitalWrite(BLINKER_LED_PIN, LOW);
    return;
  }
  if (c == '/') {
    // word gap marker
    phaseUntil = now + GAP_WORD;
    digitalWrite(BLINKER_LED_PIN, LOW);
    // optionally skip following space if present
    if (playPos < morseLength && morseBuffer[playPos] == ' ') ++playPos;
    return;
  }
  if (c == '.') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DOT_TIME;
    return;
  }
  if (c == '-') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DASH_TIME;
    return;
  }

  // unknown char -> continue immediately
  phaseUntil = now;
}