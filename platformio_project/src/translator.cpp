// File: translator.cpp v1.3
// Description: Stateless Morse <-> ASCII translator with compact lookup tables
// Last modification: ignore unknown morse tokens (no '?'), default mode DIDATIC
// Modified: 2025-11-18
// Created: 2025-11-15

#include "translator.h"
#include <string.h>
#include <ctype.h>
#include <Arduino.h>
#include <stdarg.h>

// ====== LOG FLAGS ======
#define LOG_TRANSLATOR_INFO   1
#define LOG_TRANSLATOR_ACTION 1
#define LOG_TRANSLATOR_NERD   1

// Mode enum (translator output style)
typedef enum { TRANSLATOR_MODE_MORSE = 0, TRANSLATOR_MODE_DIDATIC = 1 } translator_mode_t;
static translator_mode_t tr_mode = TRANSLATOR_MODE_DIDATIC; // default: DIDATIC

// Internal log category enum
typedef enum { TR_LOG_INFO, TR_LOG_ACTION, TR_LOG_NERD } tr_log_cat_t;

// centralized logger for translator (single-line, timestamped)
static void tr_log_cat(tr_log_cat_t cat, const char* fmt, ...) {
  if ((cat == TR_LOG_INFO && !LOG_TRANSLATOR_INFO) ||
      (cat == TR_LOG_ACTION && !LOG_TRANSLATOR_ACTION) ||
      (cat == TR_LOG_NERD && !LOG_TRANSLATOR_NERD)) {
    return;
  }

  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  const char* prefix = (cat == TR_LOG_INFO) ? "[INFO]" :
                       (cat == TR_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - translator - %s %s\n", millis(), prefix, body);
}

// Static mapping table: ASCII -> Morse
// Only uppercase letters, digits and a subset of punctuation supported
struct Mapping { char ch; const char* morse; };

static const Mapping mappingTable[] = {
  { 'A', ".-"   }, { 'B', "-..." }, { 'C', "-.-." }, { 'D', "-.."  }, { 'E', "."    },
  { 'F', "..-." }, { 'G', "--."  }, { 'H', "...." }, { 'I', ".."   }, { 'J', ".---" },
  { 'K', "-.-"  }, { 'L', ".-.." }, { 'M', "--"   }, { 'N', "-."   }, { 'O', "---"  },
  { 'P', ".--." }, { 'Q', "--.-" }, { 'R', ".-."  }, { 'S', "..."  }, { 'T', "-"    },
  { 'U', "..-"  }, { 'V', "...-" }, { 'W', ".--"  }, { 'X', "-..-" }, { 'Y', "-.--" },
  { 'Z', "--.." },

  { '0', "-----" }, { '1', ".----" }, { '2', "..---" }, { '3', "...--" }, { '4', "....-" },
  { '5', "....." }, { '6', "-...." }, { '7', "--..." }, { '8', "---.." }, { '9', "----." },

  { '.', ".-.-.-" }, { ',', "--..--" }, { '?', "..--.." }, { '\'', ".----." },
  { '!', "-.-.--" }, { '/', "-..-." }, { '(', "-.--." }, { ')', "-.--.-" },
  { '&', ".-..." }, { ':', "---..." }, { ';', "-.-.-." }, { '=', "-...-" },
  { '+', ".-.-." }, { '-', "-....-" }, { '_', "..--.-" }, { '"', ".-..-." },
  { '$', "...-..-" }, { '@', ".--.-." }
};

static const size_t MAPPING_COUNT = sizeof(mappingTable) / sizeof(mappingTable[0]);

void translator_init(void) {
  tr_mode = TRANSLATOR_MODE_DIDATIC; // default DIDATIC at boot
#if LOG_TRANSLATOR_INFO
  tr_log_cat(TR_LOG_INFO, "initialized mode=%s", (tr_mode == TRANSLATOR_MODE_MORSE) ? "MORSE" : "DIDATIC");
#endif
}

// Mode control API
void translator_setModeMorse(void) {
  if (tr_mode != TRANSLATOR_MODE_MORSE) {
    tr_mode = TRANSLATOR_MODE_MORSE;
#if LOG_TRANSLATOR_ACTION
    tr_log_cat(TR_LOG_ACTION, "mode set -> MORSE");
#endif
  }
}

void translator_setModeDidatic(void) {
  if (tr_mode != TRANSLATOR_MODE_DIDATIC) {
    tr_mode = TRANSLATOR_MODE_DIDATIC;
#if LOG_TRANSLATOR_ACTION
    tr_log_cat(TR_LOG_ACTION, "mode set -> DIDATIC");
#endif
  }
}

bool translator_isDidatic(void) {
  return (tr_mode == TRANSLATOR_MODE_DIDATIC);
}

// Helper: lookup morse by char. Returns pointer or nullptr
static const char* lookupMorseByChar(char c) {
  for (size_t i = 0; i < MAPPING_COUNT; ++i) {
    if (mappingTable[i].ch == c) return mappingTable[i].morse;
  }
  return nullptr;
}

// Helper: lookup char by morse string. Returns '\0' if not found
static char lookupCharByMorse(const char* morse) {
  if (!morse || !*morse) return '\0';
  for (size_t i = 0; i < MAPPING_COUNT; ++i) {
    if (strcmp(mappingTable[i].morse, morse) == 0) return mappingTable[i].ch;
  }
  return '\0';
}

// Morse -> ASCII (single letter)
char translator_morseToChar(const char* morse) {
  if (!morse) return '\0';
  char ch = lookupCharByMorse(morse);
#if LOG_TRANSLATOR_NERD
  if (ch == '\0') tr_log_cat(TR_LOG_NERD, "morseToChar: unknown morse '%s' -> ignored", morse);
  else tr_log_cat(TR_LOG_NERD, "morseToChar: '%s' -> '%c'", morse, ch);
#endif
  return ch; // '\0' if not recognized
}

// ASCII -> Morse
bool translator_charToMorse(char letter, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return false;
  char up = (char)toupper((unsigned char)letter);
  const char* m = lookupMorseByChar(up);
  if (!m) {
#if LOG_TRANSLATOR_NERD
    tr_log_cat(TR_LOG_NERD, "charToMorse: unsupported char '%c'", letter);
#endif
    return false;
  }

  if (tr_mode == TRANSLATOR_MODE_DIDATIC) {
    // Format: "<morse> (<ASCII>)", e.g. ".- (A)"
    int needed = snprintf(nullptr, 0, "%s (%c)", m, up) + 1;
    if ((size_t)needed > bufLen) return false;
    snprintf(outBuf, bufLen, "%s (%c)", m, up);
#if LOG_TRANSLATOR_NERD
    tr_log_cat(TR_LOG_NERD, "charToMorse DIDATIC: '%c' -> \"%s\"", up, outBuf);
#endif
    return true;
  }

  // MORSE mode: compact morse string only
  size_t need = strlen(m) + 1;
  if (bufLen < need) return false;
  strncpy(outBuf, m, bufLen);
  outBuf[bufLen - 1] = '\0';
#if LOG_TRANSLATOR_NERD
  tr_log_cat(TR_LOG_NERD, "charToMorse: '%c' -> \"%s\"", up, outBuf);
#endif
  return true;
}

// Morse-word -> ASCII (robust)
// Accepts:
//  - Compact per-letter strings: "...", "-.", ".-"
//  - Spaced symbols for one letter: ". . .", "- . ." -> collapsed to "..." or "-.."
//  - Multiple letters separated by spaces: ".- -." -> "AN" (each token is a letter)
size_t translator_morseWordToAscii(const char* morseWord, char* outBuf, size_t bufLen) {
  if (!morseWord || !outBuf || bufLen == 0) return 0;

  const char* p = morseWord;
  const size_t MAX_TOKENS = 32;
  const size_t TOKEN_SZ = 16;
  char tokens[MAX_TOKENS][TOKEN_SZ];
  size_t tokenCount = 0;

  // Tokenize by spaces
  while (*p && tokenCount < MAX_TOKENS) {
    while (*p == ' ') p++;
    if (!*p) break;
    size_t ti = 0;
    while (*p && *p != ' ' && ti + 1 < TOKEN_SZ) {
      tokens[tokenCount][ti++] = *p++;
    }
    tokens[tokenCount][ti] = '\0';
    while (*p && *p == ' ') p++;
    tokenCount++;
  }

  size_t written = 0;
  if (tokenCount == 0) {
    outBuf[0] = '\0';
    return 0;
  }

  // Check if all tokens are single symbols (spaced per-letter)
  bool allLenOne = true;
  for (size_t i = 0; i < tokenCount; ++i) {
    if (strlen(tokens[i]) != 1) { allLenOne = false; break; }
  }

  if (allLenOne) {
    // Collapse into single morse token
    char collapsed[TOKEN_SZ * MAX_TOKENS];
    size_t ci = 0;
    for (size_t i = 0; i < tokenCount; ++i) {
      if (ci + 1 < sizeof(collapsed)) collapsed[ci++] = tokens[i][0];
    }
    collapsed[ci] = '\0';

    char ch = lookupCharByMorse(collapsed);
    if (ch != '\0') {
      if (written + 1 < bufLen) {
        outBuf[written++] = ch;
      }
    }
    if (written < bufLen) outBuf[written] = '\0';
#if LOG_TRANSLATOR_NERD
    tr_log_cat(TR_LOG_NERD, "morseWordToAscii: \"%s\" (spaced symbols collapsed -> \"%s\") -> \"%s\"",
               morseWord, collapsed, outBuf);
#endif
    return written;
  }

  // Otherwise: each token is a compact letter morse string
  for (size_t ti = 0; ti < tokenCount; ++ti) {
    char ch = lookupCharByMorse(tokens[ti]);
    if (ch != '\0') { // only write recognized
      if (written + 1 < bufLen) {
        outBuf[written++] = ch;
      } else {
        break;
      }
    }
  }

  if (written < bufLen) outBuf[written] = '\0';
  else outBuf[bufLen - 1] = '\0';

#if LOG_TRANSLATOR_NERD
  tr_log_cat(TR_LOG_NERD, "morseWordToAscii: \"%s\" -> \"%s\"", morseWord, outBuf);
#endif

  return written;
}