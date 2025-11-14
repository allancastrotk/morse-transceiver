#include "blinker.h"
#include <Arduino.h>

#define LED_PIN D4            // Pino do LED (GPIO2, ativo em HIGH)
#define DOT_TIME 300          // Duração de ponto (ms)
#define DASH_TIME 600         // Duração de traço (ms)
#define SYMBOL_GAP 300        // Intervalo entre símbolos (ms)
#define LETTER_GAP 600        // Intervalo entre letras (ms)
#define WORD_GAP 1800         // Intervalo entre palavras (ms)

static char message[] = "SEMPRE ALERTA";  // Mensagem padrão em Morse
static char morseMessage[100] = "";       // Buffer para mensagem Morse
static bool blinkerActive = false;        // Estado do LED
static unsigned long blinkerStartTime = 0; // Tempo inicial do evento
static unsigned long blinkerDuration = 0; // Duração do evento
static int morseIndex = 0;                // Índice do símbolo Morse

// Tabela Morse em PROGMEM
const struct {                            
  const char* morse;
  char letter;
} morseBlinkerTable[] PROGMEM = {
  { ".-", 'A' }, { "-...", 'B' }, { "-.-.", 'C' }, { "-..", 'D' }, { ".", 'E' },
  { "..-.", 'F' }, { "--.", 'G' }, { "....", 'H' }, { "..", 'I' }, { ".---", 'J' },
  { "-.-", 'K' }, { ".-..", 'L' }, { "--", 'M' }, { "-.", 'N' }, { "---", 'O' },
  { ".--.", 'P' }, { "--.-", 'Q' }, { ".-.", 'R' }, { "...", 'S' }, { "-", 'T' },
  { "..-", 'U' }, { "...-", 'V' }, { ".--", 'W' }, { "-..-", 'X' }, { "-.--", 'Y' },
  { "--..", 'Z' }, { ".----", '1' }, { "..---", '2' }, { "...--", '3' },
  { "....-", '4' }, { ".....", '5' }, { "-....", '6' }, { "--...", '7' },
  { "---..", '8' }, { "----.", '9' }, { "-----", '0' }
};

// Configura LED e mensagem inicial
void initBlinker() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  setBlinkerMessage(message);
}

// Converte caractere em Morse
void charToMorse(char c, char* morse) {
  unsigned long now = millis();
  morse[0] = '\0';
  if (c == ' ') {
    strcat(morse, " ");
    return;
  }
  for (int i = 0; i < 36; i++) {
    char letter = pgm_read_byte(&morseBlinkerTable[i].letter);
    if (letter == toupper(c)) {
      strcpy_P(morse, morseBlinkerTable[i].morse);
      Serial.print(now);
      Serial.print(" - Convertendo caractere '");
      Serial.print(c);
      Serial.print("' para Morse: ");
      Serial.println(morse);
      break;
    }
  }
}

// Define mensagem Morse
void setBlinkerMessage(const char* newMessage) {
  unsigned long now = millis();
  morseMessage[0] = '\0';
  for (int i = 0; newMessage[i] != '\0' && strlen(morseMessage) < sizeof(morseMessage) - 7; i++) {
    char morse[7] = "";
    charToMorse(newMessage[i], morse);
    strcat(morseMessage, morse);
    if (newMessage[i + 1] != '\0' && morse[0] != '\0') {
      strcat(morseMessage, "/");
    }
  }
  morseIndex = 0;
  Serial.print(now);
  Serial.print(" - Mensagem Morse definida: ");
  Serial.println(morseMessage);
}

// Atualiza piscar do LED
void updateBlinker() {
  unsigned long now = millis();
  if (!blinkerActive && morseIndex < strlen(morseMessage)) {
    char current = morseMessage[morseIndex];
    if (current == '.') {
      digitalWrite(LED_PIN, HIGH);
      blinkerDuration = DOT_TIME;
      blinkerActive = true;
      blinkerStartTime = now;
      morseIndex++;
    } else if (current == '-') {
      digitalWrite(LED_PIN, HIGH);
      blinkerDuration = DASH_TIME;
      blinkerActive = true;
      blinkerStartTime = now;
      morseIndex++;
    } else if (current == '/') {
      blinkerDuration = LETTER_GAP;
      blinkerActive = true;
      blinkerStartTime = now;
      morseIndex++;
    } else if (current == ' ') {
      blinkerDuration = WORD_GAP;
      blinkerActive = true;
      blinkerStartTime = now;
      morseIndex++;
    }
  }

  if (blinkerActive && (now - blinkerStartTime >= blinkerDuration)) {
    digitalWrite(LED_PIN, LOW);
    blinkerActive = false;
    blinkerDuration = SYMBOL_GAP;
    blinkerStartTime = now;
  }

  if (morseIndex >= strlen(morseMessage) && !blinkerActive) {
    morseIndex = 0;
  }
}
