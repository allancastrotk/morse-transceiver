#include "cw-transceiver.h"
#include "network.h"

static ConnectionState connectionState = FREE;
static Mode mode = DIDACTIC;
static char historyTX[30] = "";
static char historyRX[30] = "";
static char currentSymbol[7] = "";
static unsigned long lastLocalPress = 0;
static unsigned long lastLocalRelease = 0;
static unsigned long lastRemotePress = 0;
static unsigned long lastRemoteRelease = 0;
static unsigned long lastActivity = 0;
static bool letterGapProcessed = false;

const char* morseCode[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",  // A-Z
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",  // 0-9
  ".-.-.-", "--..--", "..--..", "-.-.-", "-....-", "-..-"  // . , ? ; - /
};

const char* morseChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?;-/";

void initCWTransceiver() {
  pinMode(LOCAL_PIN, INPUT_PULLUP);
  pinMode(REMOTE_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  lastActivity = millis();
  Serial.print(millis());
  Serial.println(" - CW Transceiver inicializado");
}

void updateCWTransceiver() {
  unsigned long now = millis();
  handleButtonPress(LOCAL_INPUT);
  handleButtonPress(REMOTE);
  handleButtonRelease(LOCAL_INPUT);
  handleButtonRelease(REMOTE);
  handleInactivity();
  handleLetterGap();
}

void captureInput(InputSource source, unsigned long duration) {
  unsigned long now = millis();
  char symbol = (duration <= SHORT_PRESS) ? '.' : '-';
  size_t len = strlen(currentSymbol);
  if (len < 6) {
    currentSymbol[len] = symbol;
    currentSymbol[len + 1] = '\0';
    Serial.print(now);
    Serial.print(" - Simbolo: ");
    Serial.println(currentSymbol);
  }
  if (source == LOCAL_INPUT && connectionState == FREE && occupyNetwork()) {
    connectionState = TX;
    Serial.print(now);
    Serial.println(" - Exibindo estado: TX");
    sendDuration(duration);
  } else if (source == REMOTE && connectionState == FREE) {
    connectionState = RX;
    Serial.print(now);
    Serial.println(" - Exibindo estado: RX");
  }
  lastActivity = now;
  letterGapProcessed = false;
}

void handleButtonPress(InputSource source) {
  unsigned long now = millis();
  int pin = (source == LOCAL_INPUT) ? LOCAL_PIN : REMOTE_PIN;
  unsigned long& lastPress = (source == LOCAL_INPUT) ? lastLocalPress : lastRemotePress;
  if (digitalRead(pin) == LOW && now - lastPress > DEBOUNCE_TIME) {
    Serial.print(now);
    Serial.print(" - Press ");
    Serial.println(source == LOCAL_INPUT ? "local" : "remote");
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.print(now);
    Serial.println(" - Buzzer: ON");
    lastPress = now;
    lastActivity = now;
    letterGapProcessed = false;
    Serial.print(now);
    Serial.println(" - letterGapProcessed resetado para false");
  }
}

void handleButtonRelease(InputSource source) {
  unsigned long now = millis();
  int pin = (source == LOCAL_INPUT) ? LOCAL_PIN : REMOTE_PIN;
  unsigned long& lastPress = (source == LOCAL_INPUT) ? lastLocalPress : lastRemotePress;
  unsigned long& lastRelease = (source == LOCAL_INPUT) ? lastLocalRelease : lastRemoteRelease;
  if (digitalRead(pin) == HIGH && now - lastPress > DEBOUNCE_TIME && lastPress != 0) {
    unsigned long duration = now - lastPress;
    if (duration >= DEBOUNCE_TIME) {
      Serial.print(now);
      Serial.print(" - Duration ");
      Serial.print(source == LOCAL_INPUT ? "local" : "remote");
      Serial.print(": ");
      Serial.println(duration);
      if (source == LOCAL_INPUT && duration >= LONG_PRESS * 5) {
        mode = (mode == DIDACTIC) ? MORSE : DIDACTIC;
        Serial.print(now);
        Serial.print(" - Modo alterado para: ");
        Serial.println(mode == DIDACTIC ? "DIDACTIC" : "MORSE");
        currentSymbol[0] = '\0';
      } else {
        captureInput(source, duration);
      }
      digitalWrite(BUZZER_PIN, LOW);
      Serial.print(now);
      Serial.println(" - Buzzer: OFF");
      lastRelease = now;
      lastActivity = now;
      letterGapProcessed = false;
      Serial.print(now);
      Serial.println(" - letterGapProcessed resetado para false");
    }
    lastPress = 0;
  }
}

void handleInactivity() {
  unsigned long now = millis();
  if (now - lastActivity > INACTIVITY_TIMEOUT && connectionState != FREE) {
    connectionState = FREE;
    Serial.print(now);
    Serial.println(" - Inativo: FREE");
    Serial.print(now);
    Serial.println(" - Rede liberada por inatividade");
    lastLocalRelease = now;
    lastRemoteRelease = now;
  }
}

void handleLetterGap() {
  unsigned long now = millis();
  if (!letterGapProcessed && strlen(currentSymbol) > 0) {
    unsigned long lastRelease = (connectionState == TX) ? lastLocalRelease : lastRemoteRelease;
    if (now - lastRelease >= LETTER_GAP && lastRelease != 0) {
      char letter = translateMorse();
      if (letter != '\0') {
        updateHistory(letter);
        Serial.print(now);
        Serial.print(" - Historico atualizado (");
        Serial.print(connectionState == TX ? "TX" : "RX");
        Serial.print("): ");
        Serial.println(letter);
        Serial.print(now);
        Serial.print(" - Ultima letra traduzida: ");
        Serial.println(letter);
        Serial.print(now);
        Serial.println(" - Gap processado");
      }
      currentSymbol[0] = '\0';
      letterGapProcessed = true;
    }
  }
}

char translateMorse() {
  if (strlen(currentSymbol) == 0) return '\0';
  for (size_t i = 0; i < sizeof(morseCode) / sizeof(morseCode[0]); i++) {
    if (strcmp(currentSymbol, morseCode[i]) == 0) {
      return morseChars[i];
    }
  }
  return '\0';
}

void updateHistory(char letter) {
  char* history = (connectionState == TX) ? historyTX : historyRX;
  size_t len = strlen(history);
  if (len < 29) {
    history[len] = letter;
    history[len + 1] = '\0';
  } else {
    memmove(history, history + 1, 28);
    history[28] = letter;
    history[29] = '\0';
  }
}

ConnectionState getConnectionState() {
  return connectionState;
}

Mode getMode() {
  return mode;
}

const char* getCurrentSymbol() {
  return currentSymbol;
}

const char* getHistoryTX() {
  return historyTX;
}

const char* getHistoryRX() {
  return historyRX;
}
