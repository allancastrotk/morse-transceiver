#include "morse_state.h"
#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "cw-transceiver.h"
#include "bitmap.h"
#include "network.h"  // Para getNetworkStrength()

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
#define DISPLAY_INIT_DURATION 3000
#define CURSOR_BLINK 500
#define DISPLAY_DURATION 1500
#define DISPLAY_UPDATE_INTERVAL 100
#define NETWORK_UPDATE_INTERVAL 5000  // Verifica sinal a cada 5s

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static unsigned long lastBlink = 0;
static unsigned long lastDisplay = 0;
static char lastHistoryTX[30] = "";
static char lastHistoryRX[30] = "";
static char lastSymbol[8] = "";
static char lastTranslatedDisplay[2] = "";
static ConnectionState lastState = FREE;
static bool lastModeSwitching = false;
static unsigned long lastUpdateTime = 0;
static unsigned long lastNetworkUpdate = 0;
static char lastStrength[5] = " OFF";  // Cache para otimizacao

void initDisplay() {
  unsigned long now = millis();
  Serial.print(now);
  Serial.println(" - Inicializando I2C (SDA=D2, SCL=D1)");
  Wire.begin(D2, D1);
  Serial.print(now);
  Serial.print(" - Tentando inicializar SSD1306 no endereco 0x");
  Serial.println(OLED_ADDRESS, HEX);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.print(now);
    Serial.println(" - Erro: Falha ao inicializar SSD1306");
    for (;;);
  }
  Serial.print(now);
  Serial.println(" - SSD1306 inicializado com sucesso");
  display.clearDisplay();
  Serial.print(now);
  Serial.println(" - Exibindo bitmap inicial");
  display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.display();
  Serial.print(now);
  Serial.println(" - Display inicializado com bitmap");
  delay(DISPLAY_INIT_DURATION);  // Bloqueante; scans network async nao afetam
  display.clearDisplay();
  display.display();
  Serial.print(now);
  Serial.println(" - Display limpo apos bitmap");
  updateDisplay();
  Serial.print(now);
  Serial.println(" - Estrutura da tela exibida apos inicializacao");
}

void updateDisplay() {
  unsigned long now = millis();
  if (now - lastUpdateTime < DISPLAY_UPDATE_INTERVAL) return;
  lastUpdateTime = now;

  const char* currentHistTX = getHistoryTX();
  const char* currentHistRX = getHistoryRX();
  const char* currentSymbol = getCurrentSymbol();
  const char* lastTranslated = getLastTranslated();
  ConnectionState currentState = getConnectionState();
  bool modeSwitching = isModeSwitching();
  static bool firstUpdate = true;
  bool contentChanged = strcmp(currentHistTX, lastHistoryTX) != 0 ||
                       strcmp(currentHistRX, lastHistoryRX) != 0 ||
                       strcmp(currentSymbol, lastSymbol) != 0 ||
                       currentState != lastState ||
                       modeSwitching != lastModeSwitching ||
                       strcmp(lastTranslated, lastTranslatedDisplay) != 0;
  bool logUpdate = contentChanged || firstUpdate;

  // Verifica sinal Wi-Fi a cada NETWORK_UPDATE_INTERVAL, mas imprime apenas se alterado
  bool strengthChanged = false;
  if (now - lastNetworkUpdate >= NETWORK_UPDATE_INTERVAL) {
    const char* currentStrength = getNetworkStrength();
    if (strcmp(currentStrength, lastStrength) != 0) {
      strcpy(lastStrength, currentStrength);
      strengthChanged = true;
      Serial.print(now);
      Serial.print(" - Sinal Wi-Fi atualizado: ");
      Serial.println(lastStrength);
    }
    lastNetworkUpdate = now;
  }

  if (!firstUpdate && !contentChanged && !modeSwitching && !strengthChanged &&
      !(getMode() == DIDACTIC && now - lastBlink >= CURSOR_BLINK)) return;
  firstUpdate = false;

  if (logUpdate && strcmp(lastTranslated, lastTranslatedDisplay) != 0 && strlen(lastTranslated) > 0) {
    strcpy(lastTranslatedDisplay, lastTranslated);
    lastDisplay = now;
    Serial.print(now);
    Serial.print(" - Nova letra traduzida para exibicao: ");
    Serial.println(lastTranslatedDisplay);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  if (modeSwitching) {
    display.setTextSize(2);
    display.setCursor(32, SCREEN_HEIGHT / 4 - 8);
    if (getMode() == DIDACTIC) {
      display.println("DIDACTIC");
      display.setCursor(32, SCREEN_HEIGHT * 3 / 4 - 8);
      display.println("MODE");
    } else {
      display.println("MORSE");
      display.setCursor(32, SCREEN_HEIGHT * 3 / 4 - 8);
      display.println("MODE");
    }
    if (logUpdate) {
      Serial.print(now);
      Serial.println(" - Exibindo modo no display");
    }
  } else {
    display.drawFastVLine(64, 0, 64, WHITE);
    display.drawFastHLine(0, 32, 64, WHITE);

    if (currentState == TX) {
      display.setCursor(68, 2);
      display.print("TX");
      if (logUpdate) {
        Serial.print(now);
        Serial.println(" - Exibindo estado: TX");
      }
    } else if (currentState == RX) {
      display.setCursor(68, 55);
      display.print("RX");
      if (logUpdate) {
        Serial.print(now);
        Serial.println(" - Exibindo estado: RX");
      }
    }

    // Sinal Wi-Fi alinhado a direita (4 chars)
    display.setCursor(104, 2);  // 128 - 4*6 = 104 para textSize(1)
    display.print(lastStrength);

    // Historico TX (esquerda superior)
    char lineTX1[11], lineTX2[11], lineTX3[10];
    memset(lineTX1, 0, sizeof(lineTX1)); memset(lineTX2, 0, sizeof(lineTX2)); memset(lineTX3, 0, sizeof(lineTX3));
    strncpy(lineTX1, currentHistTX, 10); strncpy(lineTX2, currentHistTX + 10, 10); strncpy(lineTX3, currentHistTX + 20, 9);
    display.setCursor(2, 2); display.print(lineTX1); display.setCursor(2, 12); display.print(lineTX2); display.setCursor(2, 22); display.print(lineTX3);
    if (logUpdate && strlen(currentHistTX) > 0) {
      Serial.print(now);
      Serial.print(" - Exibindo historico TX: ");
      Serial.println(currentHistTX);
    }

    // Historico RX (esquerda inferior)
    char lineRX1[11], lineRX2[11], lineRX3[10];
    memset(lineRX1, 0, sizeof(lineRX1)); memset(lineRX2, 0, sizeof(lineRX2)); memset(lineRX3, 0, sizeof(lineRX3));
    strncpy(lineRX1, currentHistRX, 10); strncpy(lineRX2, currentHistRX + 10, 10); strncpy(lineRX3, currentHistRX + 20, 9);
    display.setCursor(2, 34); display.print(lineRX1); display.setCursor(2, 44); display.print(lineRX2); display.setCursor(2, 54); display.print(lineRX3);
    if (logUpdate && strlen(currentHistRX) > 0) {
      Serial.print(now);
      Serial.print(" - Exibindo historico RX: ");
      Serial.println(currentHistRX);
    }

    // Letra/simbolo direito
    display.setTextSize(6);
    display.setCursor(90, 20);
    if (getMode() == DIDACTIC) {
      if (strlen(lastTranslatedDisplay) > 0 && now - lastDisplay < DISPLAY_DURATION) {
        display.print(lastTranslatedDisplay);
        if (logUpdate) {
          Serial.print(now);
          Serial.print(" - Exibindo letra: ");
          Serial.println(lastTranslatedDisplay);
        }
      } else if (now - lastBlink >= CURSOR_BLINK) {
        lastBlink = now;
        static bool showCursor = true;
        showCursor = !showCursor;
        if (showCursor) display.print("_");
        // Sem log para cursor piscante
      }
    } else if (getMode() == MORSE) {
      if (strlen(currentSymbol) > 0) {
        display.print(currentSymbol);
        if (logUpdate) {
          Serial.print(now);
          Serial.print(" - Exibindo simbolo atual: ");
          Serial.println(currentSymbol);
        }
        lastDisplay = now;
      } else if ((strlen(currentHistTX) > 0 && currentState == TX) ||
                 (strlen(currentHistRX) > 0 && currentState == RX)) {
        char lastChar = (currentState == TX) ? currentHistTX[strlen(currentHistTX) - 1] : currentHistRX[strlen(currentHistRX) - 1];
        if (lastChar != '\0' && now - lastDisplay < DISPLAY_DURATION) {
          display.print(lastChar);
          if (logUpdate) {
            Serial.print(now);
            Serial.print(" - Exibindo simbolo: ");
            Serial.println(lastChar);
          }
        }
      }
    }
    display.setTextSize(1);
  }

  display.display();
  // Sem log geral para "Display atualizado"; apenas em mudanças de conteudo ou sinal acima

  strcpy(lastHistoryTX, currentHistTX);
  strcpy(lastHistoryRX, currentHistRX);
  strcpy(lastSymbol, currentSymbol);
  lastState = currentState;
  lastModeSwitching = modeSwitching;
}

