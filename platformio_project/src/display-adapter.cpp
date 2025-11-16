// File: display-adapter.cpp v1.5
// Description: OLED adapter enforcing documented layout with history clipping, splash, timed big-letter/symbol, and didactic cursor.
// Last modification: add showSymbol; bottom-right positioning; strict clipping; 1.5s timeout behavior
// Modified: 2025-11-18
// Created: 2025-11-15

#include "display-adapter.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "bitmap.h"
#include "history.h"
#include "network-state.h"
#include "translator.h"

#ifndef SCREEN_WIDTH
  #define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
  #define SCREEN_HEIGHT 64
#endif
#ifndef SSD1306_RESET
  #define SSD1306_RESET -1
#endif

// ====== LOG FLAGS ======
#define LOG_DISPLAY_INIT    1
#define LOG_DISPLAY_UPDATE  1
#define LOG_DISPLAY_ERROR   0

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SSD1306_RESET);

static unsigned long lastUpdateMs = 0;
static const unsigned long UPDATE_INTERVAL_MS = 100;
static bool needFullRedraw = false;

// Splash
static bool splashActive = false;
static unsigned long splashUntilMs = 0;
static char splashLine1[DISPLAY_ADAPTER_LINE_BUF];
static char splashLine2[DISPLAY_ADAPTER_LINE_BUF];

// Optional redraw callback
static da_redraw_cb_t redrawCb = nullptr;

// History raw snapshot
static char txTopRaw[DISPLAY_ADAPTER_LINE_BUF];
static char txMidRaw[DISPLAY_ADAPTER_LINE_BUF];
static char txBotRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxTopRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxMidRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxBotRaw[DISPLAY_ADAPTER_LINE_BUF];

// Visible clipped buffers (respect 10/10/9)
static char txTopVis[DISPLAY_ADAPTER_LINE_BUF];
static char txMidVis[DISPLAY_ADAPTER_LINE_BUF];
static char txBotVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxTopVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxMidVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxBotVis[DISPLAY_ADAPTER_LINE_BUF];

// Right column: big content buffer + timeout
static char bigContent[DISPLAY_ADAPTER_LINE_BUF];
static unsigned long bigUntilMs = 0;
static const unsigned long BIG_TIMEOUT_MS = 1500;

static unsigned long lastHistoryVersion = 0;
static ConnectionState lastState = FREE;

// External (optional)
extern const char* getNetworkStrength();

// Helpers
static void clipToWidth(const char* src, char* dst, size_t maxVis) {
  if (!dst || maxVis == 0) return;
  if (!src || !*src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len <= maxVis) {
    strncpy(dst, src, maxVis);
    dst[maxVis] = '\0';
    return;
  }
  // Right-align recent content: keep last maxVis chars
  const char* start = src + (len - maxVis);
  strncpy(dst, start, maxVis);
  dst[maxVis] = '\0';
}

static void prepareVisibleBuffers() {
  clipToWidth(txTopRaw, txTopVis, 10);
  clipToWidth(txMidRaw, txMidVis, 10);
  clipToWidth(txBotRaw, txBotVis, 9);

  clipToWidth(rxTopRaw, rxTopVis, 10);
  clipToWidth(rxMidRaw, rxMidVis, 10);
  clipToWidth(rxBotRaw, rxBotVis, 9);
}

// Bottom status: only TX/RX tags; FREE hidden
static void drawStatusBar(ConnectionState st) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, SCREEN_HEIGHT - 10);
  if (st == TX) display.print("[TX]");
  else if (st == RX) display.print("[RX]");
}

// Didactic cursor (blinking) — shows only when big content expired
static void drawDidacticCursorWhenIdle() {
  if (!translator_isDidatic()) return;
  bool bigActive = (bigContent[0] != '\0') && (millis() <= bigUntilMs);
  if (bigActive) return;
  bool on = ((millis() / 500) % 2) == 0;
  if (!on) return;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // right column, near top-right
  display.setCursor(120, 10);
  display.print("_");
}

// Right column positioning (bottom-right with margins)
// GFX default font is 6x8 per char scaled by textSize.
// We compute width/height to align 3 px from right and ~3 chars from bottom.
// "3 characters from bottom" ~ 3 * (8 * textSize) vertical margin.
static void drawRightColumnContent() {
  bool active = (bigContent[0] != '\0') && (millis() <= bigUntilMs);
  if (!active) {
    // ensure cleared
    bigContent[0] = '\0';
    return;
  }

  size_t len = strlen(bigContent);
  uint8_t textSize;
  if (len <= 3) textSize = 6;
  else if (len <= 8) textSize = 3;
  else textSize = 1;

  const int charW = 6 * textSize;
  const int charH = 8 * textSize;
  const int marginRight = 3;
  const int marginBottomChars = 3; // desired char heights from bottom
  const int marginBottom = marginBottomChars * charH;

  int contentW = (int)(len * charW);
  int contentH = charH;

  int x = SCREEN_WIDTH - marginRight - contentW;
  if (x < 68) x = 68; // clamp to right column start

  int y = SCREEN_HEIGHT - marginBottom - contentH;
  if (y < 0) y = 0;

  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(bigContent);
}

// Full redraw
static void doFullRedraw(ConnectionState st) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Separators
  display.drawFastVLine(64, 0, 64, SSD1306_WHITE);
  display.drawFastHLine(0, 32, 64, SSD1306_WHITE);

  // TX (top-left)
  display.setTextSize(1);
  display.setCursor(2, 2);   display.print(txTopVis);
  display.setCursor(2, 12);  display.print(txMidVis);
  display.setCursor(2, 22);  display.print(txBotVis);

  // RX (bottom-left)
  display.setCursor(2, 34);  display.print(rxTopVis);
  display.setCursor(2, 44);  display.print(rxMidVis);
  display.setCursor(2, 54);  display.print(rxBotVis);

  // State label (TX/RX only)
  display.setTextSize(1);
  if (st == TX) {
    display.setCursor(68, 2);
    display.print("TX");
  } else if (st == RX) {
    display.setCursor(68, 2);
    display.print("RX");
  }

  // Wi-Fi indicator (optional)
  if ((void*)getNetworkStrength != nullptr) {
    const char* s = getNetworkStrength();
    if (s && *s) {
      display.setCursor(104, 2);
      display.print(s);
    }
  }

  // Big content (letter or symbol) with timeout and bottom-right placement
  drawRightColumnContent();

  // Didactic cursor when idle (no big content on screen)
  drawDidacticCursorWhenIdle();

  // Bottom status bar
  drawStatusBar(st);

  display.display();
}

// Public API
void displayAdapter_init() {
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
#if LOG_DISPLAY_ERROR
    Serial.println("display-adapter: init failed; Serial fallback enabled");
#endif
  } else {
    display.clearDisplay();
    display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
  }

  lastHistoryVersion = 0;
  lastState = FREE;
  needFullRedraw = true;

  splashActive = false;
  splashUntilMs = 0;
  splashLine1[0] = splashLine2[0] = '\0';

  txTopRaw[0] = txMidRaw[0] = txBotRaw[0] = '\0';
  rxTopRaw[0] = rxMidRaw[0] = rxBotRaw[0] = '\0';

  txTopVis[0] = txMidVis[0] = txBotVis[0] = '\0';
  rxTopVis[0] = rxMidVis[0] = rxBotVis[0] = '\0';

  bigContent[0] = '\0';
  bigUntilMs = 0;

#if LOG_DISPLAY_INIT
  Serial.printf("%lu - display-adapter - [INIT] initialized\n", millis());
#endif
}

void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms) {
  if (line1) strncpy(splashLine1, line1, sizeof(splashLine1)-1); else splashLine1[0] = '\0';
  if (line2) strncpy(splashLine2, line2, sizeof(splashLine2)-1); else splashLine2[0] = '\0';
  splashLine1[sizeof(splashLine1)-1] = '\0';
  splashLine2[sizeof(splashLine2)-1] = '\0';
  splashUntilMs = millis() + duration_ms;
  splashActive = true;
  needFullRedraw = true;
}

void displayAdapter_forceRedraw() {
  needFullRedraw = true;
  lastHistoryVersion = 0; // force re-read
}

void displayAdapter_setRedrawCallback(da_redraw_cb_t cb) {
  redrawCb = cb;
}

void displayAdapter_update() {
  unsigned long now = millis();
  if (now - lastUpdateMs < UPDATE_INTERVAL_MS) return;
  lastUpdateMs = now;

  // Splash
  if (splashActive) {
    if (now < splashUntilMs) {
      if (display.width() <= 0) {
        Serial.printf("%lu - display-adapter - [UPDATE] SPLASH: %s / %s\n", millis(), splashLine1, splashLine2);
      }
      return;
    } else {
      splashActive = false;
      needFullRedraw = true;
    }
  }

  // History snapshot + prepare visible buffers
  unsigned long v = history_getSnapshot(txTopRaw, txMidRaw, txBotRaw, rxTopRaw, rxMidRaw, rxBotRaw, sizeof(txTopRaw));
  ConnectionState st = ns_getState();
  prepareVisibleBuffers();

  // Expire big content if timeout passed
  if (bigContent[0] && now > bigUntilMs) {
    bigContent[0] = '\0';
    needFullRedraw = true;
  }

  if (v != lastHistoryVersion || st != lastState || needFullRedraw) {
    lastHistoryVersion = v;
    lastState = st;
    needFullRedraw = false;

    if (redrawCb) redrawCb();

    if (display.width() > 0) {
      doFullRedraw(st);
    } else {
      // Serial fallback
      Serial.printf("DISPLAY REDRAW state=%s v=%lu\n", (st==TX)?"TX":(st==RX)?"RX":"FREE", v);
      Serial.printf("TX> %s\n   %s\n   %s\n", txTopVis, txMidVis, txBotVis);
      Serial.printf("RX> %s\n   %s\n   %s\n", rxTopVis, rxMidVis, rxBotVis);
      if (bigContent[0]) Serial.printf("BIG> %s\n", bigContent);
    }
  }
}

// Show a small ASCII string/letter prominently (used on finalize)
void displayAdapter_showLetter(const char* ascii) {
  if (!ascii || !*ascii) {
    bigContent[0] = '\0';
    bigUntilMs = 0;
  } else {
    strncpy(bigContent, ascii, sizeof(bigContent) - 1);
    bigContent[sizeof(bigContent)-1] = '\0';
    bigUntilMs = millis() + BIG_TIMEOUT_MS;
  }
  needFullRedraw = true;
}

// NEW: Show a single Morse symbol prominently ('.' or '-')
void displayAdapter_showSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  bigContent[0] = sym;
  bigContent[1] = '\0';
  bigUntilMs = millis() + BIG_TIMEOUT_MS;
  needFullRedraw = true;
}