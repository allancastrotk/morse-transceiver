/* blinker.cpp — PowerTune Morse Transceiver v6.1
   Indica pequenas mensagens em LED sem bloquear o loop principal
*/

#include "blinker.h"
#include <Arduino.h>
#include <string.h>

#define LED_PIN LED_BUILTIN
#define DOT_TIME 120
#define DASH_TIME 360
#define GAP_SYMBOL 120
#define GAP_LETTER 360
#define GAP_WORD   800

static char morseBuffer[512];
static int morseIndex = 0;
static int morseLength = 0;

static bool busyOffDelay = false;
static unsigned long offUntil = 0;

struct MorseMap { char c; const char* code; };
static const MorseMap morseTable[] = {
  { 'A', ".-" },   { 'B', "-..." }, { 'C', "-.-." }, { 'D', "-.." },
  { 'E', "." },    { 'F', "..-." }, { 'G', "--." },  { 'H', "...." },
  { 'I', ".." },   { 'J', ".---" }, { 'K', "-.-" },  { 'L', ".-.." },
  { 'M', "--" },   { 'N', "-." },   { 'O', "---" },  { 'P', ".--." },
  { 'Q', "--.-" }, { 'R', ".-." },  { 'S', "..." },  { 'T', "-" },
  { 'U', "..-" },  { 'V', "...-" }, { 'W', ".--" },  { 'X', "-..-" },
  { 'Y', "-.--" }, { 'Z', "--.." },
  { '0', "-----" },{ '1', ".----" },{ '2', "..---" },{ '3', "...--" },
  { '4', "....-" },{ '5', "....." },{ '6', "-...." },{ '7', "--..." },
  { '8', "---.." },{ '9', "----." }
};
static const int morseCount = sizeof(morseTable) / sizeof(MorseMap);

static const char* charToMorse(char c) {
  c = toupper(c);
  for (int i = 0; i < morseCount; i++) if (morseTable[i].c == c) return morseTable[i].code;
  return "";
}

void initBlinker() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void startBlinker(const char* phrase) {
  morseBuffer[0] = '\0';
  morseIndex = 0;
  morseLength = 0;
  if (!phrase) return;
  for (int i = 0; phrase[i]; ++i) {
    char c = phrase[i];
    if (c == ' ') {
      strcat(morseBuffer, "/ ");
      continue;
    }
    const char* code = charToMorse(c);
    if (code && *code) {
      strcat(morseBuffer, code);
      strcat(morseBuffer, " ");
    }
  }
  morseLength = strlen(morseBuffer);
  busyOffDelay = false;
  offUntil = 0;
  digitalWrite(LED_PIN, LOW);
}

void updateBlinker() {
  unsigned long now = millis();
  if (morseLength == 0) return;

  if (busyOffDelay) {
    if (now >= offUntil) busyOffDelay = false;
    else return;
  }

  if (morseIndex >= morseLength) {
    morseIndex = 0;
    busyOffDelay = true;
    offUntil = now + GAP_WORD;
    return;
  }

  char c = morseBuffer[morseIndex++];

  if (c == ' ') {
    busyOffDelay = true;
    offUntil = now + GAP_LETTER;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (c == '/') {
    busyOffDelay = true;
    offUntil = now + GAP_WORD;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (c == '.') {
    digitalWrite(LED_PIN, HIGH);
    unsigned long tstart = now;
    while (millis() - tstart < DOT_TIME) { yield(); }
    digitalWrite(LED_PIN, LOW);
    busyOffDelay = true;
    offUntil = millis() + GAP_SYMBOL;
    return;
  } else if (c == '-') {
    digitalWrite(LED_PIN, HIGH);
    unsigned long tstart = now;
    while (millis() - tstart < DASH_TIME) { yield(); }
    digitalWrite(LED_PIN, LOW);
    busyOffDelay = true;
    offUntil = millis() + GAP_SYMBOL;
    return;
  }
}