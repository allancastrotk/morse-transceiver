// File: translator.cpp
// Implementation of translator.h
// - Compact static tables for common Morse code mappings
// - Stateless functions
// Modified: 2025-11-15

#include "translator.h"
#include <string.h>
#include <ctype.h>

// Static mapping table: ASCII -> Morse
// Only uppercase letters, digits and a few punctuation supported
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

  // common punctuation (small set)
  { '.', ".-.-.-" }, { ',', "--..--" }, { '?', "..--.." }, { '\'', ".----." },
  { '!', "-.-.--" }, { '/', "-..-." }, { '(', "-.--." }, { ')', "-.--.-" },
  { '&', ".-..." }, { ':', "---..." }, { ';', "-.-.-." }, { '=', "-...-" },
  { '+', ".-.-." }, { '-', "-....-" }, { '_', "..--.-" }, { '"', ".-..-." },
  { '$', "...-..-" }, { '@', ".--.-." }
};

static const size_t MAPPING_COUNT = sizeof(mappingTable) / sizeof(mappingTable[0]);

void translator_init() {
  // no-op for now; function kept for symmetry and future extension
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

char translator_morseToChar(const char* morse) {
  if (!morse) return '\0';
  return lookupCharByMorse(morse);
}

bool translator_charToMorse(char letter, char* outBuf, size_t bufLen) {
  if (!outBuf || bufLen == 0) return false;
  char up = (char)toupper((unsigned char)letter);
  const char* m = lookupMorseByChar(up);
  if (!m) return false;
  size_t need = strlen(m) + 1;
  if (bufLen < need) return false;
  strncpy(outBuf, m, bufLen);
  outBuf[bufLen - 1] = '\0';
  return true;
}

size_t translator_morseWordToAscii(const char* morseWord, char* outBuf, size_t bufLen) {
  if (!morseWord || !outBuf || bufLen == 0) return 0;
  size_t written = 0;
  const char* p = morseWord;
  char token[16];
  while (*p) {
    // skip leading spaces
    while (*p == ' ') p++;
    if (!*p) break;
    // copy token up to space
    size_t ti = 0;
    while (*p && *p != ' ' && ti < sizeof(token) - 1) {
      token[ti++] = *p++;
    }
    token[ti] = '\0';
    // if token longer than token buffer, skip to next space
    if (ti == sizeof(token) - 1 && *p && *p != ' ') {
      // skip rest of this token
      while (*p && *p != ' ') p++;
    }
    // translate token
    char ch = lookupCharByMorse(token);
    if (ch == '\0') ch = '?';
    if (written + 1 < bufLen) {
      outBuf[written++] = ch;
    } else {
      break; // no more room (reserve one for null)
    }
  }
  if (written < bufLen) outBuf[written] = '\0';
  else outBuf[bufLen - 1] = '\0';
  return written;
}