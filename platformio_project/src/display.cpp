/* display.cpp — PowerTune Morse Transceiver v6.1
   Usa historyVersion para evitar flood de redraw/log
*/

#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "cw-transceiver.h"
#include "bitmap.h"
#include "network.h"  // getNetworkStrength()

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
#define DISPLAY_INIT_DURATION 3000
#define CURSOR_BLINK 500
#define DISPLAY_DURATION 3000
#define DISPLAY_UPDATE_INTERVAL 100
#define NETWORK_UPDATE_INTERVAL 5000  // check strength every 5s

// ====== LOG FLAGS: 0 = off, 1 = on ======
#define LOG_DISPLAY 1

#if LOG_DISPLAY
  #define LOG_DEBUG(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG_DEBUG(...) ((void)0)
#endif

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// caches and state
static bool displayEnabled = true;
static bool splashActive = true;
static unsigned long splashStart = 0;

static unsigned long lastBlink = 0;
static unsigned long lastDisplay = 0;
static char lastHistoryTX[30] = "";
static char lastHistoryRX[30] = "";
static char lastSymbol[16] = "";
static char lastTranslatedDisplay[4] = "";
static ConnectionState lastState = FREE;
static bool lastModeSwitching = false;
static unsigned long lastUpdateTime = 0;
static unsigned long lastNetworkUpdate = 0;
static char lastStrength[8] = " OFF";

static unsigned long lastHistoryVersion = 0;

// safe copy helpers
static void safeCopy(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  snprintf(dst, dstSize, "%s", src);
}

void initDisplay() {
  unsigned long now = millis();
  Serial.printf("%lu - Initializing I2C (SDA=D2, SCL=D1)\n", now);
  Wire.begin(D2, D1);

  Serial.printf("%lu - Attempting SSD1306 init at 0x%X\n", millis(), OLED_ADDRESS);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.printf("%lu - SSD1306 init failed, continuing without display\n", millis());
    displayEnabled = false;
    splashActive = false;
    return;
  }

  Serial.printf("%lu - SSD1306 initialized\n", millis());
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.display();

  splashStart = millis();
  splashActive = true;

  safeCopy(lastHistoryTX, sizeof(lastHistoryTX), "");
  safeCopy(lastHistoryRX, sizeof(lastHistoryRX), "");
  safeCopy(lastSymbol, sizeof(lastSymbol), "");
  safeCopy(lastTranslatedDisplay, sizeof(lastTranslatedDisplay), "");
  safeCopy(lastStrength, sizeof(lastStrength), getNetworkStrength());
  lastUpdateTime = 0;
  lastNetworkUpdate = 0;
  lastBlink = millis();
  lastDisplay = millis();
  lastHistoryVersion = getHistoryVersion();
  LOG_DEBUG("%lu - display init complete (splash active)\n", millis());
}

void updateDisplay() {
  if (!displayEnabled) return;

  unsigned long now = millis();
  if (splashActive) {
    if (now - splashStart >= DISPLAY_INIT_DURATION) {
      splashActive = false;
      display.clearDisplay();
      display.display();
      LOG_DEBUG("%lu - Splash finished, clearing display\n", now);
    } else {
      return;
    }
  }

  if (now - lastUpdateTime < DISPLAY_UPDATE_INTERVAL) return;
  lastUpdateTime = now;

  const char* currentHistTX = getHistoryTX();
  const char* currentHistRX = getHistoryRX();
  const char* currentSymbol = getCurrentSymbol();
  const char* lastTranslated = getLastTranslated();
  ConnectionState currentState = getConnectionState();
  bool modeSwitching = isModeSwitching();

  if (!currentHistTX) currentHistTX = "";
  if (!currentHistRX) currentHistRX = "";
  if (!currentSymbol) currentSymbol = "";
  if (!lastTranslated) lastTranslated = "";

  bool strengthChanged = false;
  if (now - lastNetworkUpdate >= NETWORK_UPDATE_INTERVAL) {
    const char* currentStrength = getNetworkStrength();
    if (!currentStrength) currentStrength = " OFF";
    if (strcmp(currentStrength, lastStrength) != 0) {
      safeCopy(lastStrength, sizeof(lastStrength), currentStrength);
      strengthChanged = true;
      LOG_DEBUG("%lu - Wi-Fi strength updated: %s\n", now, lastStrength);
    }
    lastNetworkUpdate = now;
  }

  // use historyVersion to detect history changes cheaply and atomically
  bool contentChanged = false;
  unsigned long currentHistoryVersion = getHistoryVersion();
  if (currentHistoryVersion != lastHistoryVersion) {
    contentChanged = true;
    lastHistoryVersion = currentHistoryVersion;
  }

  if (!contentChanged) {
    contentChanged = (currentState != lastState) || (modeSwitching != lastModeSwitching) ||
                     (strcmp(lastTranslated, lastTranslatedDisplay) != 0) || strengthChanged;
  }

  static bool firstUpdate = true;
  bool doLog = contentChanged || firstUpdate;
  firstUpdate = false;

  if (doLog && strlen(lastTranslated) > 0 && strcmp(lastTranslated, lastTranslatedDisplay) != 0) {
    safeCopy(lastTranslatedDisplay, sizeof(lastTranslatedDisplay), lastTranslated);
    lastDisplay = now;
    LOG_DEBUG("%lu - New translated letter for display: %s\n", now, lastTranslatedDisplay);
  }

  if (!contentChanged && !modeSwitching && !(getMode() == DIDACTIC && (now - lastBlink >= CURSOR_BLINK))) {
    return;
  }

  display.clearDisplay();
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
    if (doLog) LOG_DEBUG("%lu - Showing mode on display\n", now);
  } else {
    display.setTextSize(1);
    display.drawFastVLine(64, 0, 64, WHITE);
    display.drawFastHLine(0, 32, 64, WHITE);

    if (currentState == TX) {
      display.setCursor(68, 2);
      display.print("TX");
      if (doLog) LOG_DEBUG("%lu - Display state: TX\n", now);
    } else if (currentState == RX) {
      display.setCursor(68, 55);
      display.print("RX");
      if (doLog) LOG_DEBUG("%lu - Display state: RX\n", now);
    }

    display.setCursor(104, 2);
    display.print(lastStrength);

    // TX history (left top) - split safely into 3 lines
    char lineTX1[11] = {0}, lineTX2[11] = {0}, lineTX3[11] = {0};
    safeCopy(lineTX1, sizeof(lineTX1), currentHistTX);
    if (strlen(currentHistTX) > 10) safeCopy(lineTX2, sizeof(lineTX2), currentHistTX + 10);
    if (strlen(currentHistTX) > 20) safeCopy(lineTX3, sizeof(lineTX3), currentHistTX + 20);
    display.setCursor(2, 2); display.print(lineTX1);
    display.setCursor(2, 12); display.print(lineTX2);
    display.setCursor(2, 22); display.print(lineTX3);
    if (doLog && strlen(currentHistTX) > 0) LOG_DEBUG("%lu - Showing TX history: %s\n", now, currentHistTX);

    // RX history (left bottom)
    char lineRX1[11] = {0}, lineRX2[11] = {0}, lineRX3[11] = {0};
    safeCopy(lineRX1, sizeof(lineRX1), currentHistRX);
    if (strlen(currentHistRX) > 10) safeCopy(lineRX2, sizeof(lineRX2), currentHistRX + 10);
    if (strlen(currentHistRX) > 20) safeCopy(lineRX3, sizeof(lineRX3), currentHistRX + 20);
    display.setCursor(2, 34); display.print(lineRX1);
    display.setCursor(2, 44); display.print(lineRX2);
    display.setCursor(2, 54); display.print(lineRX3);
    if (doLog && strlen(currentHistRX) > 0) LOG_DEBUG("%lu - Showing RX history: %s\n", now, currentHistRX);

    // Right large area: symbol / letter
    display.setTextSize(6);
    display.setCursor(90, 20);
    if (getMode() == DIDACTIC) {
      if (strlen(lastTranslatedDisplay) > 0 && (now - lastDisplay < DISPLAY_DURATION)) {
        display.print(lastTranslatedDisplay);
        if (doLog) LOG_DEBUG("%lu - Showing letter: %s\n", now, lastTranslatedDisplay);
      } else if (now - lastBlink >= CURSOR_BLINK) {
        lastBlink = now;
        static bool showCursor = false;
        showCursor = !showCursor;
        if (showCursor) display.print("_");
      }
    } else { // MORSE mode
      if (strlen(currentSymbol) > 0) {
        safeCopy(lastSymbol, sizeof(lastSymbol), currentSymbol);
        display.print(lastSymbol);
        if (doLog) LOG_DEBUG("%lu - Showing current symbol: %s\n", now, lastSymbol);
        lastDisplay = now;
      } else {
        if ((strlen(currentHistTX) > 0 && currentState == TX) ||
            (strlen(currentHistRX) > 0 && currentState == RX)) {
          const char* hist = (currentState == TX) ? currentHistTX : currentHistRX;
          size_t hlen = strlen(hist);
          if (hlen > 0) {
            char lastChar = hist[hlen - 1];
            if (now - lastDisplay < DISPLAY_DURATION) {
              char tmp[2] = { lastChar, '\0' };
              display.print(tmp);
              if (doLog) LOG_DEBUG("%lu - Showing recent char: %c\n", now, lastChar);
            }
          }
        }
      }
    }
    display.setTextSize(1);
  }

  display.display();

  // update caches
  safeCopy(lastHistoryTX, sizeof(lastHistoryTX), currentHistTX);
  safeCopy(lastHistoryRX, sizeof(lastHistoryRX), currentHistRX);
  safeCopy(lastSymbol, sizeof(lastSymbol), currentSymbol);
  lastState = currentState;
  lastModeSwitching = modeSwitching;
}