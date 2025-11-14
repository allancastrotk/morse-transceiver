#include <Arduino.h>
#include <string.h>
#include "morse_state.h"

static char _lastTranslatedBuf[256] = {0};
static bool _modeSwitching = false;

const char* getLastTranslated() {
  return _lastTranslatedBuf;
}

bool isModeSwitching() {
  return _modeSwitching;
}

void setLastTranslated(const char* s) {
  if (!s) { _lastTranslatedBuf[0]=0; return; }
  strncpy(_lastTranslatedBuf, s, sizeof(_lastTranslatedBuf)-1);
  _lastTranslatedBuf[sizeof(_lastTranslatedBuf)-1]=0;
}

void setModeSwitching(bool v) {
  _modeSwitching = v;
}
