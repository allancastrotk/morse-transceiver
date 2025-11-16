// File: blinker.cpp v1.3
// Description: Non-blocking Morse LED blinker implementation using translator (looping, isolated)
// Last modification: isolated playback; translator used only during build phase; structured logs
// Modified: 2025-11-15 05:30
// Created: 2025-11-15

#include "blinker.h"
#include "translator.h"
#include <string.h>
#include <stdarg.h>

// ====== LOG FLAGS ======
// Set 0 to disable, 1 to enable
#define LOG_BLINKER_INIT    1   // log initialization and initial phrase
#define LOG_BLINKER_BUILD   1   // log translation/build (ASCII -> morse buffer)
#define LOG_BLINKER_RUN     1   // log start/stop events
#define LOG_BLINKER_PHASE   1   // log phase transitions (dot/dash/gaps/loop)
#define LOG_BLINKER_VERBOSE 0   // extra verbose debug (rarely used)

// ====== LOG HELPERS ======
static void blog_init(const char* fmt, ...) {
#if LOG_BLINKER_INIT
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); Serial.println();
#else
  (void)fmt;
#endif
}
static void blog_build(const char* fmt, ...) {
#if LOG_BLINKER_BUILD
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); Serial.println();
#else
  (void)fmt;
#endif
}
static void blog_run(const char* fmt, ...) {
#if LOG_BLINKER_RUN
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); Serial.println();
#else
  (void)fmt;
#endif
}
static void blog_phase(const char* fmt, ...) {
#if LOG_BLINKER_PHASE
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); Serial.println();
#else
  (void)fmt;
#endif
}
static void blog_v(const char* fmt, ...) {
#if LOG_BLINKER_VERBOSE
  va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); Serial.println();
#else
  (void)fmt;
#endif
}

// ====== CONFIG ======
static uint8_t BLINKER_LED_PIN = LED_BUILTIN;

// Timings (ms) - original readable values
static const unsigned long DOT_TIME   = 300;
static const unsigned long DASH_TIME  = 600;
static const unsigned long GAP_SYMBOL = 300;
static const unsigned long GAP_LETTER = 600;
static const unsigned long GAP_WORD   = 1800;

// Internal morse buffer format:
// letters separated by single space, words separated by "/ " (slash + space)
static char morseBuffer[512];
static size_t morseLength = 0u;

// Playback state (internal only)
static size_t playPos = 0u;
static bool phaseOn = false;             // LED is ON (dot/dash) or OFF (gaps)
static unsigned long phaseUntil = 0u;
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

// Build morseBuffer once using translator; no side effects with other modules.
static void buildMorseFromPhrase(const char* phrase) {
  morseBuffer[0] = '\0';
  if (!phrase) {
    morseLength = 0;
    blog_build("%lu - blinker build: empty phrase", millis());
    return;
  }

  char morseToken[32];
  for (size_t i = 0; phrase[i] != '\0'; ++i) {
    char c = phrase[i];
    if (c == ' ') {
      safeAppend("/ ");
      continue;
    }
    if (!translator_charToMorse(c, morseToken, sizeof(morseToken))) {
      blog_build("%lu - blinker build: unknown char '%c' skipped", millis(), c);
      continue;
    }
    safeAppend(morseToken);
    safeAppend(" "); // letter separator
  }
  morseLength = strlen(morseBuffer);
  blog_build("%lu - blinker built morse (len=%u): %s", millis(), (unsigned)morseLength, morseBuffer);
}

// ====== PUBLIC API ======
void initBlinker(uint8_t ledPin, const char* initialPhrase) {
  if (ledPin != 255) BLINKER_LED_PIN = ledPin;
  pinMode(BLINKER_LED_PIN, OUTPUT);
  digitalWrite(BLINKER_LED_PIN, LOW);

  // translator used only to build morse sequence (no runtime interaction)
  translator_init();

  morseBuffer[0] = '\0';
  morseLength = 0;
  running = false;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;

  blog_init("%lu - blinker init on pin %u", millis(), (unsigned)BLINKER_LED_PIN);

  if (initialPhrase && initialPhrase[0] != '\0') {
    buildMorseFromPhrase(initialPhrase);
    blog_init("%lu - blinker initial phrase set: \"%s\" -> morseLen=%u", millis(), initialPhrase, (unsigned)morseLength);
    if (morseLength > 0) {
      running = true;
      playPos = 0;
      phaseOn = false;
      phaseUntil = 0;
    }
  }
}

void startBlinker(const char* phrase) {
  if (!phrase || phrase[0] == '\0') {
    blog_run("%lu - blinker start called with empty phrase - ignored", millis());
    return;
  }
  buildMorseFromPhrase(phrase);
  if (morseLength == 0) {
    blog_run("%lu - blinker start: morseLength==0 for phrase \"%s\"", millis(), phrase);
    return;
  }
  running = true;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  digitalWrite(BLINKER_LED_PIN, LOW);
  blog_run("%lu - blinker started phrase: \"%s\"", millis(), phrase);
}

void stopBlinker() {
  running = false;
  morseBuffer[0] = '\0';
  morseLength = 0;
  playPos = 0;
  phaseOn = false;
  phaseUntil = 0;
  digitalWrite(BLINKER_LED_PIN, LOW);
  blog_run("%lu - blinker stopped", millis());
}

void updateBlinker() {
  if (!running || morseLength == 0) return;
  unsigned long now = millis();

  // if inside a timed phase and not yet reached, return
  if (phaseUntil != 0 && timeNotReached(now, phaseUntil)) return;

  // just finished an ON phase (dot or dash)
  if (phaseOn) {
    phaseOn = false;
    digitalWrite(BLINKER_LED_PIN, LOW);
    phaseUntil = now + GAP_SYMBOL;
    blog_phase("%lu - blinker ON->OFF, symbol gap %lu ms", millis(), (unsigned long)GAP_SYMBOL);
    return;
  }

  // OFF phase finished or starting fresh: advance to next symbol
  if (playPos >= morseLength) {
    // reached end -> loop with end-of-word gap
    playPos = 0;
    phaseUntil = now + GAP_WORD;
    digitalWrite(BLINKER_LED_PIN, LOW);
    blog_phase("%lu - blinker sequence end -> loop gap %lu ms", millis(), (unsigned long)GAP_WORD);
    return;
  }

  // read next char and advance safely
  char c = morseBuffer[playPos++];
  if (c == ' ') {
    // letter gap
    phaseUntil = now + GAP_LETTER;
    digitalWrite(BLINKER_LED_PIN, LOW);
    blog_phase("%lu - blinker letter gap %lu ms", millis(), (unsigned long)GAP_LETTER);
    return;
  }
  if (c == '/') {
    // word gap marker
    phaseUntil = now + GAP_WORD;
    digitalWrite(BLINKER_LED_PIN, LOW);
    blog_phase("%lu - blinker word gap %lu ms", millis(), (unsigned long)GAP_WORD);
    // skip following space if present (we produced "/ ")
    if (playPos < morseLength && morseBuffer[playPos] == ' ') ++playPos;
    return;
  }
  if (c == '.') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DOT_TIME;
    blog_phase("%lu - blinker DOT at bufPos=%u dur=%lu ms", millis(), (unsigned)(playPos - 1), (unsigned long)DOT_TIME);
    return;
  }
  if (c == '-') {
    digitalWrite(BLINKER_LED_PIN, HIGH);
    phaseOn = true;
    phaseUntil = now + DASH_TIME;
    blog_phase("%lu - blinker DASH at bufPos=%u dur=%lu ms", millis(), (unsigned)(playPos - 1), (unsigned long)DASH_TIME);
    return;
  }

  // unknown char -> continue immediately
  blog_v("%lu - blinker unknown char '%c' at pos %u - skipping", millis(), c, (unsigned)(playPos - 1));
  phaseUntil = now;
}
