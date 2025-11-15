/* cw-transceiver.cpp — Morse Transceiver v6.1
   Implementa histórico com historyVersion para sincronizar display
*/

#include "cw-transceiver.h"
#include "network.h"
#include <Arduino.h>
#include <string.h>

// ====== LOG FLAGS: 0 = off, 1 = on ======
#define LOG_BUTTON  1
#define LOG_GAP     1
#define LOG_MODE    1
#define LOG_NETWORK 1
#define LOG_STATE   1

// ====== STATE ======
static char symbolBuffer[16];
static uint8_t symbolIndex;

static char historyTX[64];
static char historyRX[64];

static unsigned long historyVersion = 0; // incrementa a cada alteração em historyTX/historyRX

static ConnectionState connState = FREE;
static Mode currentMode = DIDACTIC;

static char lastTranslatedBuf[4];
static unsigned long lastTranslatedAt = 0;
static const unsigned long LAST_TRANSLATED_DURATION = 1500;

static bool _modeSwitching = false;

// button timing (local)
static unsigned long pressStart = 0;
static bool isPressed = false;
static unsigned long lastReleaseTime = 0;

// remote timing (DOWN/UP)
static unsigned long remotePressStart = 0;
static bool remoteIsPressed = false;

// activity timer (tracks last TX/RX event)
static unsigned long lastActivityAt = 0;

// helpers
static void safePushHistory(char* history, char c) {
  size_t len = strlen(history);
  if (len < (sizeof(history) - 1)) {
    history[len] = c;
    history[len + 1] = '\0';
  } else {
    memmove(history, history + 1, sizeof(history) - 2);
    history[sizeof(history) - 2] = c;
    history[sizeof(history) - 1] = '\0';
  }
  // sinaliza que o history mudou
  historyVersion++;
  if (LOG_STATE) Serial.printf("%lu - historyVersion -> %lu\n", millis(), historyVersion);
}

// getters
const char* getCurrentSymbol() { return symbolBuffer; }
const char* getHistoryTX() { return historyTX; }
const char* getHistoryRX() { return historyRX; }
unsigned long getHistoryVersion() { return historyVersion; }
ConnectionState getConnectionState() { return connState; }
Mode getMode() { return currentMode; }
const char* getLastTranslated() {
  if (millis() - lastTranslatedAt > LAST_TRANSLATED_DURATION) return "";
  return lastTranslatedBuf;
}
bool isModeSwitching() { return _modeSwitching; }

// translation table
static char translateSymbolBuffer(const char* s) {
  if (!s || !s[0]) return '\0';
  struct Pair { const char* m; char c; };
  static const Pair table[] = {
    {".-", 'A'},{"-...", 'B'},{"-.-.", 'C'},{"-..", 'D'},
    {".", 'E'},{"..-.", 'F'},{"--.", 'G'},{"....", 'H'},
    {"..", 'I'},{".---", 'J'},{"-.-", 'K'},{".-..", 'L'},
    {"--", 'M'},{"-.", 'N'},{"---", 'O'},{".--.", 'P'},
    {"--.-", 'Q'},{".-.", 'R'},{"...", 'S'},{"-", 'T'},
    {"..-", 'U'},{"...-", 'V'},{".--", 'W'},{"-..-", 'X'},
    {"-.--", 'Y'},{"--..", 'Z'}
  };
  for (auto &p : table) if (strcmp(p.m, s) == 0) return p.c;
  return '\0';
}

// push symbol locally (and in MORSE mode register immediately to TX history)
static void pushSymbolLocal(char s) {
  if (symbolIndex < sizeof(symbolBuffer) - 1) {
    symbolBuffer[symbolIndex++] = s;
    symbolBuffer[symbolIndex] = '\0';
  }
  if (currentMode == MORSE) safePushHistory(historyTX, s);
  // mark activity and set TX
  connState = TX;
  lastActivityAt = millis();
  if (LOG_STATE) Serial.printf("%lu - STATE -> TX (local symbol)\n", millis());
}

// finalize local letter: DIDACTIC -> translate & store; MORSE -> buffer already stored in history
static void finalizeLetterLocal() {
  if (symbolIndex == 0) return;
  if (currentMode == DIDACTIC) {
    char letter = translateSymbolBuffer(symbolBuffer);
    if (letter != '\0') {
      safePushHistory(historyTX, letter);
      lastTranslatedBuf[0] = letter;
      lastTranslatedBuf[1] = '\0';
      lastTranslatedAt = millis();
      if (LOG_GAP) {
        Serial.printf("%lu - Letra traduzida (local): %c\n", millis(), letter);
      }
    } else {
      if (LOG_GAP) {
        Serial.printf("%lu - Símbolo desconhecido (local): %s\n", millis(), symbolBuffer);
      }
    }
  } else {
    if (LOG_GAP) {
      Serial.printf("%lu - Final buffer (MORSE) local mantido: %s\n", millis(), symbolBuffer);
    }
  }
  symbolIndex = 0;
  symbolBuffer[0] = '\0';
}

// push remote symbol into RX history — imediato e sem debouncing extra
static void pushSymbolRemote(char s) {
  safePushHistory(historyRX, s);
  // mark activity and set RX so display and logic reflect remote activity
  connState = RX;
  lastActivityAt = millis();
  if (LOG_STATE) Serial.printf("%lu - STATE -> RX (remote symbol '%c')\n", millis(), s);
}

// ================== Lifecycle ==================
void initCWTransceiver() {
  pinMode(LOCAL_PIN, INPUT_PULLUP);
  pinMode(REMOTE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  symbolBuffer[0] = '\0'; symbolIndex = 0;
  historyTX[0] = '\0'; historyRX[0] = '\0';
  lastTranslatedBuf[0] = '\0';
  isPressed = false; remoteIsPressed = false;
  lastReleaseTime = 0; remotePressStart = 0;
  _modeSwitching = false;
  lastActivityAt = 0;
  connState = FREE;
  Serial.printf("%lu - CW Transceiver iniciado (v6.1)\n", millis());
}

// ================== cw -> network helpers ==================
void sendRemoteDown() {
  // send network DOWN immediately and mark state
  network_sendDown();
  connState = TX;
  lastActivityAt = millis();
  if (LOG_NETWORK) Serial.printf("%lu - network_sendDown() executed\n", millis());
  if (LOG_STATE) Serial.printf("%lu - STATE -> TX (sent DOWN)\n", millis());
}
void sendRemoteUp() {
  network_sendUp();
  // update lastActivity but keep TX until timeout
  lastActivityAt = millis();
  if (LOG_NETWORK) Serial.printf("%lu - network_sendUp() executed\n", millis());
}

// ================== network -> cw: remote events ==================
void injectRemoteDown() {
  if (remoteIsPressed) return;
  remoteIsPressed = true;
  remotePressStart = millis();
  // emulate local press: buzzer ON immediately
  digitalWrite(BUZZER_PIN, HIGH);
  connState = RX;
  lastActivityAt = millis();
  if (LOG_NETWORK) Serial.printf("%lu - injectRemoteDown()\n", millis());
  if (LOG_STATE) Serial.printf("%lu - STATE -> RX (injectRemoteDown)\n", millis());
}

void injectRemoteUp() {
  if (!remoteIsPressed) return;
  unsigned long now = millis();
  unsigned long dur = now - remotePressStart;
  remoteIsPressed = false;
  remotePressStart = 0;
  // emulate local release: buzzer OFF immediately
  digitalWrite(BUZZER_PIN, LOW);
  if (LOG_NETWORK) Serial.printf("%lu - injectRemoteUp dur=%lu\n", now, dur);

  // update activity timestamp (keep RX active for ACTIVITY_TIMEOUT_MS)
  lastActivityAt = now;

  // if very long press => finalize letter (treat as gap)
  if (dur > DASH_MAX) {
    if (LOG_GAP) Serial.printf("%lu - Remote finalize (long press)\n", now);
    return;
  }

  char s = (dur < DOT_MAX) ? '.' : '-';
  pushSymbolRemote(s);
  lastReleaseTime = now;
}

// ================== Main update loop (handles local input) ==================
void updateCWTransceiver() {
  unsigned long now = millis();
  bool readingLocal = (digitalRead(LOCAL_PIN) == LOW);

  // Mode hold detection (non-blocking)
  if (readingLocal) {
    if (!isPressed) pressStart = now;
    else if (!_modeSwitching && (now - pressStart >= MODE_HOLD_MS)) {
      _modeSwitching = true;
      currentMode = (currentMode == DIDACTIC) ? MORSE : DIDACTIC;
      if (currentMode == DIDACTIC) strcpy(lastTranslatedBuf, "DID");
      else strcpy(lastTranslatedBuf, "MOR");
      lastTranslatedAt = now;
      if (LOG_MODE) Serial.printf("%lu - MODO ALTERADO PARA: %s\n", now, currentMode == DIDACTIC ? "DIDACTIC" : "MORSE");
    }
  } else if (_modeSwitching) {
    _modeSwitching = false;
    isPressed = false;
    return;
  }

  // If we're in RX, short presses must be ignored for transmission.
  // Long press (MODE_HOLD_MS) still allowed to toggle local mode.
  if (readingLocal) {
    if (!isPressed) {
      // button just pressed
      pressStart = now;
      isPressed = true;
      // If not RX, local press should immediately act (buzzer + send DOWN).
      if (connState != RX) {
        digitalWrite(BUZZER_PIN, HIGH);
        sendRemoteDown();
        if (LOG_BUTTON) Serial.printf("%lu - Press local (handled)\n", now);
      } else {
        // In RX: do not send DOWN nor turn buzzer on for local press.
        if (LOG_BUTTON) Serial.printf("%lu - Press local ignored due RX (waiting for possible hold)\n", now);
      }
      return;
    } else {
      // button already pressed: check for hold to toggle mode
      if (!_modeSwitching && (now - pressStart >= MODE_HOLD_MS)) {
        _modeSwitching = true;
        currentMode = (currentMode == DIDACTIC) ? MORSE : DIDACTIC;
        if (currentMode == DIDACTIC) strcpy(lastTranslatedBuf, "DID");
        else strcpy(lastTranslatedBuf, "MOR");
        lastTranslatedAt = now;
        if (LOG_MODE) Serial.printf("%lu - MODO ALTERADO PARA: %s\n", now, currentMode == DIDACTIC ? "DIDACTIC" : "MORSE");
      }
      return;
    }
  }

  // Release (local)
  if (!readingLocal && isPressed) {
    isPressed = false;
    unsigned long dur = now - pressStart;
    lastReleaseTime = now;

    // If we were not allowed to transmit because connState==RX, we must not send UP or register symbol.
    if (connState != RX) {
      // Normal release: stop buzzer, send UP, process symbol/finalize as before
      digitalWrite(BUZZER_PIN, LOW);
      sendRemoteUp();
      if (LOG_BUTTON) Serial.printf("%lu - Release local dur=%lu\n", now, dur);

      if (dur >= MODE_HOLD_MS) {
        // was a mode hold; clear mode-switch flag and do nothing else
        _modeSwitching = false;
        return;
      }

      if (dur > DASH_MAX) {
        finalizeLetterLocal();
        lastActivityAt = now;
        connState = TX;
        if (LOG_STATE) Serial.printf("%lu - STATE -> TX (finalize local letter)\n", now);
        return;
      }

      char s = (dur < DOT_MAX) ? '.' : '-';
      pushSymbolLocal(s);
      return;
    } else {
      // We were in RX while pressed: treat as mode-hold if it was a long press, otherwise ignore.
      digitalWrite(BUZZER_PIN, LOW); // ensure we don't unintentionally sound buzzer
      if (_modeSwitching) {
        _modeSwitching = false;
        // Mode already toggled at hold detection; nothing else to do
        if (LOG_MODE) Serial.printf("%lu - Mode toggle applied while RX\n", now);
      } else {
        if (LOG_BUTTON) Serial.printf("%lu - Short release ignored due RX\n", now);
      }
      return;
    }
  }

  // letter gap: finalize after silence (local)
  if (!isPressed && symbolIndex > 0 && (now - lastReleaseTime >= LETTER_GAP_MS)) {
    finalizeLetterLocal();
  }

  // activity timeout: se nenhum TX/RX em ACTIVITY_TIMEOUT_MS, volta a FREE
  if (connState != FREE) {
    if (now - lastActivityAt >= ACTIVITY_TIMEOUT_MS) {
      if (LOG_STATE) Serial.printf("%lu - Activity timeout, STATE -> FREE\n", now);
      connState = FREE;
      // safety: ensure buzzer off
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}