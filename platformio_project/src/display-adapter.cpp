// File: display-adapter.cpp v2.0-fixed-2
// Description: OLED adapter enforcing documented layout with history clipping, splash,
//              timed big-letter/symbol, didactic cursor, and mode overlay.
// Last modification: fixed malformed right-column indicator block and removed/adjusted unused helpers
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
#define LOG_DISPLAY_INFO    1
#define LOG_DISPLAY_ACTION  1
#define LOG_DISPLAY_NERD    0

typedef enum { D_LOG_INFO, D_LOG_ACTION, D_LOG_NERD } d_log_cat_t;
static void display_log_cat(d_log_cat_t cat, const char* fmt, ...) {
  if ((cat == D_LOG_INFO && !LOG_DISPLAY_INFO) ||
      (cat == D_LOG_ACTION && !LOG_DISPLAY_ACTION) ||
      (cat == D_LOG_NERD && !LOG_DISPLAY_NERD)) return;
  char body[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  const char* prefix = (cat == D_LOG_INFO) ? "[INFO]" :
                       (cat == D_LOG_ACTION) ? "[ACTION]" : "[NERD]";
  Serial.printf("%lu - display-adapter - %s %s\n", millis(), prefix, body);
}

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SSD1306_RESET);

// ====== Loop pacing & redraw ======
static unsigned long lastUpdateMs = 0;
static const unsigned long UPDATE_INTERVAL_MS = 100;
static bool needFullRedraw = false;

// ====== Splash ======
static bool splashActive = false;
static unsigned long splashUntilMs = 0;
static char splashLine1[DISPLAY_ADAPTER_LINE_BUF];
static char splashLine2[DISPLAY_ADAPTER_LINE_BUF];

// ====== Mode overlay ======
static bool modeMsgActive = false;
static unsigned long modeMsgUntilMs = 0;
static char modeMsgLine1[DISPLAY_ADAPTER_LINE_BUF];
static char modeMsgLine2[DISPLAY_ADAPTER_LINE_BUF];
static const unsigned long MODEMSG_TIMEOUT_MS = 1500;

// ====== Optional redraw callback ======
static da_redraw_cb_t redrawCb = nullptr;

// ====== History snapshot buffers (raw from history) ======
static char txTopRaw[DISPLAY_ADAPTER_LINE_BUF];
static char txMidRaw[DISPLAY_ADAPTER_LINE_BUF];
static char txBotRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxTopRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxMidRaw[DISPLAY_ADAPTER_LINE_BUF];
static char rxBotRaw[DISPLAY_ADAPTER_LINE_BUF];

// ====== Visible clipped buffers (fit left pane cells) ======
static char txTopVis[DISPLAY_ADAPTER_LINE_BUF];
static char txMidVis[DISPLAY_ADAPTER_LINE_BUF];
static char txBotVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxTopVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxMidVis[DISPLAY_ADAPTER_LINE_BUF];
static char rxBotVis[DISPLAY_ADAPTER_LINE_BUF];

// ====== Right column: big content buffer + timeout ======
static char bigContent[DISPLAY_ADAPTER_LINE_BUF];
static unsigned long bigUntilMs = 0;
static const unsigned long BIG_TIMEOUT_MS = 1500;

// ====== Last known state ======
static unsigned long lastHistoryVersion = 0;
static ConnectionState lastState = FREE;

// ====== Blink tracking ======
static bool lastBlinkOn = false;

// ====== External (optional) ======
extern const char* getNetworkStrength();

// ====== Forward declarations ======
static void drawRightColumnContent(int& outX, int& outW, uint8_t& outTextSize);
static void drawIdleCursorIfNeeded(int contentX, int contentW, uint8_t textSize);
static void drawStatusBar(ConnectionState st);
static void drawStatusIndicatorRight(ConnectionState st);
static void doFullRedraw(ConnectionState st);

// ====== Helpers ======
static void clipToWidth(const char* src, char* dst, size_t maxVis) {
  if (!dst || maxVis == 0) return;
  if (!src || !*src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len <= maxVis) {
    strncpy(dst, src, maxVis);
    dst[maxVis] = '\0';
    return;
  }
  const char* start = src + (len - maxVis);
  strncpy(dst, start, maxVis);
  dst[maxVis] = '\0';
}

static void prepareVisibleBuffers() {
  // TX (10/10/9 chars per row in left pane)
  clipToWidth(txTopRaw, txTopVis, 10);
  clipToWidth(txMidRaw, txMidVis, 10);
  clipToWidth(txBotRaw, txBotVis, 9);

  // RX (10/10/9 chars per row in left pane)
  clipToWidth(rxTopRaw, rxTopVis, 10);
  clipToWidth(rxMidRaw, rxMidVis, 10);
  clipToWidth(rxBotRaw, rxBotVis, 9);
}

// ====== Right column content (big letter/symbol at bottom) ======
static void drawRightColumnContent(int& outX, int& outW, uint8_t& outTextSize) {
  bool active = (bigContent[0] != '\0') && (millis() <= bigUntilMs);
  if (!active) { outW = 0; outTextSize = 6; outX = 68; return; }

  size_t len = strlen(bigContent);
  uint8_t textSize = (len <= 3) ? 6 : (len <= 8) ? 3 : 1;

  const int charW = 6 * textSize;
  const int charH = 8 * textSize;
  const int marginRight = 3;
  const int marginBottom = 3;

  int contentW = (int)(len * charW);
  int contentH = charH;

  int x = SCREEN_WIDTH - marginRight - contentW;
  if (x < 68) x = 68;

  int y = SCREEN_HEIGHT - contentH - marginBottom;
  if (y < 0) y = 0;

  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(bigContent);

  outX = x;
  outW = contentW;
  outTextSize = textSize;
}

// ====== Idle cursor (editor-like): 1 Hz blink, 3px height, width = charW - 3 ======
static void drawIdleCursorIfNeeded(int contentX, int contentW, uint8_t textSize) {
  if (!translator_isDidatic()) return;

  // Only when no big content is active
  bool bigActive = (bigContent[0] != '\0') && (millis() <= bigUntilMs);
  if (bigActive) return;

  // Blink at 1 Hz (500ms on/off)
  bool blinkOn = ((millis() / 500) % 2) == 0;
  if (!blinkOn) return;

  const int marginBottom = 3;
  const int charW = (6 * textSize) - 3; // fine alignment: width equals char width minus 3

  // X position: caret (end of content) or centered idle position
  int x;
  if (contentW == 0) {
    // Idle center: draw at right column baseline, aligned to where a single char would start
    x = SCREEN_WIDTH - 3 - charW - 4;
    if (x < 68) x = 68;
  } else {
    // Caret after last char, align left by 3 px for aesthetic
    x = contentX + contentW - 3;
    if (x < 68) x = 68;
  }

  // Y position: 3 px above bottom edge
  int y = SCREEN_HEIGHT - marginBottom - 3;
  if (y < 0) y = 0;

  display.fillRect(x, y, charW, 3, SSD1306_WHITE);
}

// ====== Status indicators (right column) - compact and safe ======
static void drawStatusIndicatorRight(ConnectionState st) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const int rightColX = 68; // start of right column content
  const int labelW = 28;
  const int labelH = 8;

  // Clear small top area then bottom area to avoid ghosting
  display.fillRect(rightColX - 1, 0, labelW + 2, labelH + 2, SSD1306_BLACK);

  int rxY = SCREEN_HEIGHT - 10;
  // If big content active, avoid collision: compute big top and lift RX
  bool bigActive = (bigContent[0] != '\0') && (millis() <= bigUntilMs);
  if (bigActive) {
    uint8_t tsize = (strlen(bigContent) <= 3) ? 6 : (strlen(bigContent) <= 8) ? 3 : 1;
    int bigCharH = 8 * tsize;
    int bigTopY = SCREEN_HEIGHT - bigCharH - 3;
    if (bigTopY < rxY + labelH) rxY = bigTopY - labelH - 2;
    if (rxY < 0) rxY = SCREEN_HEIGHT - 10;
  }

  display.fillRect(rightColX - 1, rxY - 1, labelW + 2, labelH + 2, SSD1306_BLACK);

  if (st == TX) {
    display.setCursor(rightColX, 2);
    display.print("[TX]");
  } else if (st == RX) {
    display.setCursor(rightColX, rxY);
    display.print("[RX]");
  }

  // network strength top-right
  if ((void*)getNetworkStrength != nullptr) {
    const char* s = getNetworkStrength();
    if (s && *s) {
      display.fillRect(SCREEN_WIDTH - 28, 0, 28, labelH + 2, SSD1306_BLACK);
      display.setCursor(SCREEN_WIDTH - 28, 2);
      display.print(s);
    }
  }
}

// ====== Mode overlay (two lines, centered, size 3) ======
void displayAdapter_showModeMessage(const char* line1, const char* line2) {
  if (line1) strncpy(modeMsgLine1, line1, sizeof(modeMsgLine1)-1); else modeMsgLine1[0] = '\0';
  if (line2) strncpy(modeMsgLine2, line2, sizeof(modeMsgLine2)-1); else modeMsgLine2[0] = '\0';
  modeMsgLine1[sizeof(modeMsgLine1)-1] = '\0';
  modeMsgLine2[sizeof(modeMsgLine2)-1] = '\0';
  modeMsgUntilMs = millis() + MODEMSG_TIMEOUT_MS;
  modeMsgActive = true;
  needFullRedraw = true;
  display_log_cat(D_LOG_ACTION, "mode message requested: \"%s\" / \"%s\"", modeMsgLine1, modeMsgLine2);
}

static void drawModeOverlayNow() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);

  // First line centered
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(modeMsgLine1, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  int y = (SCREEN_HEIGHT / 2) - (int)h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  display.setCursor(x, y);
  display.print(modeMsgLine1);

  // Second line centered, a few pixels below center
  display.getTextBounds(modeMsgLine2, 0, 0, &x1, &y1, &w, &h);
  x = (SCREEN_WIDTH - (int)w) / 2;
  y = (SCREEN_HEIGHT / 2) + 5;
  if (x < 0) x = 0;
  if (y + (int)h > SCREEN_HEIGHT) y = SCREEN_HEIGHT - h;
  display.setCursor(x, y);
  display.print(modeMsgLine2);

  display.display();
}

// ====== Full redraw ======
static void doFullRedraw(ConnectionState st) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Separators (grid)
  display.drawFastVLine(64, 0, 64, SSD1306_WHITE);
  display.drawFastHLine(0, 32, 64, SSD1306_WHITE);

  // Left pane: TX (top half)
  display.setTextSize(1);
  display.setCursor(2, 2);   display.print(txTopVis);
  display.setCursor(2, 12);  display.print(txMidVis);
  display.setCursor(2, 22);  display.print(txBotVis);

  // Left pane: RX (bottom half)
  display.setCursor(2, 34);  display.print(rxTopVis);
  display.setCursor(2, 44);  display.print(rxMidVis);
  display.setCursor(2, 54);  display.print(rxBotVis);

  // Right column content (bottom) then idle cursor
  int contentX = 68, contentW = 0;
  uint8_t textSize = 6;
  drawRightColumnContent(contentX, contentW, textSize);
  drawIdleCursorIfNeeded(contentX, contentW, textSize);

  // Status indicators only on right column (no bottom-left indicator)
  drawStatusIndicatorRight(st);

  display.display();
}

// ====== History integration helpers ======
static inline unsigned long readHistoryVersion() {
  #if defined(HISTORY_HAS_VERSION)
    return history_get_version();
  #else
    return 0;
  #endif
}

static inline void snapshotHistoryRows() {
  #if defined(HISTORY_EXPORTS_ROWS)
    history_export_tx_rows(txTopRaw, txMidRaw, txBotRaw);
    history_export_rx_rows(rxTopRaw, rxMidRaw, rxBotRaw);
  #else
    #ifdef history_get_tx_top
      history_get_tx_top(txTopRaw, DISPLAY_ADAPTER_LINE_BUF);
      history_get_tx_mid(txMidRaw, DISPLAY_ADAPTER_LINE_BUF);
      history_get_tx_bot(txBotRaw, DISPLAY_ADAPTER_LINE_BUF);
      history_get_rx_top(rxTopRaw, DISPLAY_ADAPTER_LINE_BUF);
      history_get_rx_mid(rxMidRaw, DISPLAY_ADAPTER_LINE_BUF);
      history_get_rx_bot(rxBotRaw, DISPLAY_ADAPTER_LINE_BUF);
    #else
      txTopRaw[0] = txMidRaw[0] = txBotRaw[0] = '\0';
      rxTopRaw[0] = rxMidRaw[0] = rxBotRaw[0] = '\0';
    #endif
  #endif
}

// ====== Public API ======
void displayAdapter_init() {
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display_log_cat(D_LOG_INFO, "init failed; Serial fallback enabled");
  } else {
    display.clearDisplay();
    // desenha bitmap de splash imediatamente
    display.drawBitmap(0, 0, bitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
  }

  lastHistoryVersion = 0;
  lastState = FREE;
  needFullRedraw = true;

  // Splash init: mostrar automaticamente por 3s (use displayAdapter_showSplash para chamadas futuras)
  displayAdapter_showSplash("Morse", "Booting...", 3000);

  // Mode overlay init
  modeMsgActive = false;
  modeMsgUntilMs = 0;
  modeMsgLine1[0] = modeMsgLine2[0] = '\0';

  // Buffers init
  txTopRaw[0] = txMidRaw[0] = txBotRaw[0] = '\0';
  rxTopRaw[0] = rxMidRaw[0] = rxBotRaw[0] = '\0';

  txTopVis[0] = txMidVis[0] = txBotVis[0] = '\0';
  rxTopVis[0] = rxMidVis[0] = rxBotVis[0] = '\0';

  bigContent[0] = '\0';
  bigUntilMs = 0;

  lastBlinkOn = false;

  display_log_cat(D_LOG_INFO, "initialized");
}

void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms) {
  if (line1) strncpy(splashLine1, line1, sizeof(splashLine1)-1); else splashLine1[0] = '\0';
  if (line2) strncpy(splashLine2, line2, sizeof(splashLine2)-1); else splashLine2[0] = '\0';
  splashLine1[sizeof(splashLine1)-1] = '\0';
  splashLine2[sizeof(splashLine2)-1] = '\0';
  splashUntilMs = millis() + duration_ms;
  splashActive = true;
  needFullRedraw = true;
  display_log_cat(D_LOG_ACTION, "splash requested: \"%s\" / \"%s\"", splashLine1, splashLine2);
}

void displayAdapter_forceRedraw() {
  needFullRedraw = true;
  lastHistoryVersion = 0;
  display_log_cat(D_LOG_ACTION, "force redraw");
}

void displayAdapter_setRedrawCallback(da_redraw_cb_t cb) {
  redrawCb = cb;
}

void displayAdapter_update() {
  unsigned long now = millis();
  if (now - lastUpdateMs < UPDATE_INTERVAL_MS) return;
  lastUpdateMs = now;

  // Splash lifecycle
  if (splashActive) {
    if (now < splashUntilMs) {
      if (display.width() <= 0) {
        display_log_cat(D_LOG_INFO, "splash active: %s / %s", splashLine1, splashLine2);
      }
      return;
    } else {
      splashActive = false;
      needFullRedraw = true;
      display_log_cat(D_LOG_ACTION, "splash expired");
    }
  }

  // Mode overlay lifecycle
  if (modeMsgActive) {
    if (now < modeMsgUntilMs) {
      drawModeOverlayNow();
      return; // override everything while overlay is active
    } else {
      modeMsgActive = false;
      needFullRedraw = true;
      display_log_cat(D_LOG_ACTION, "mode message expired");
    }
  }

  // Take history snapshot (ensure it fills tx/rx buffers distinctly)
  unsigned long v = history_getSnapshot(txTopRaw, txMidRaw, txBotRaw,
                                        rxTopRaw, rxMidRaw, rxBotRaw,
                                        sizeof(txTopRaw));
  ConnectionState st = ns_getState();
  prepareVisibleBuffers();

  // Expire big content
  bool bigActiveNow = (bigContent[0] != '\0') && (now <= bigUntilMs);
  if (bigContent[0] && now > bigUntilMs) {
    bigContent[0] = '\0';
    needFullRedraw = true;
    display_log_cat(D_LOG_ACTION, "big content expired");
  }

  // Idle cursor blink tracking: force redraw when toggle occurs and idle
  bool shouldBlink = translator_isDidatic() && !bigActiveNow;
  bool blinkOn = ((now / 500) % 2) == 0;
  if (shouldBlink && blinkOn != lastBlinkOn) {
    needFullRedraw = true;
    lastBlinkOn = blinkOn;
  }
  if (!shouldBlink && lastBlinkOn != false) {
    lastBlinkOn = false;
  }

  // Redraw if anything changed or forced
  if (v != lastHistoryVersion || st != lastState || needFullRedraw) {
    lastHistoryVersion = v;
    lastState = st;
    needFullRedraw = false;

    if (redrawCb) redrawCb();

    if (display.width() > 0) {
      doFullRedraw(st);
    } else {
      display_log_cat(D_LOG_INFO, "serial redraw state=%s v=%lu",
                      (st==TX)?"TX":(st==RX)?"RX":"FREE", v);
      Serial.printf("TX> %s\n   %s\n   %s\n", txTopVis, txMidVis, txBotVis);
      Serial.printf("RX> %s\n   %s\n   %s\n", rxTopVis, rxMidVis, rxBotVis);
      if (bigContent[0]) Serial.printf("BIG> %s\n", bigContent);
    }
  } else {
    // lightweight refresh: draw right-side small elements so indicator & cursor remain responsive
    int contentX = 68, contentW = 0; uint8_t tsize = 6;
    drawRightColumnContent(contentX, contentW, tsize);
    drawIdleCursorIfNeeded(contentX, contentW, tsize);
    drawStatusIndicatorRight(st);
    display.display();
  }
}

void displayAdapter_showLetter(const char* ascii) {
  if (!ascii || !*ascii) {
    bigContent[0] = '\0';
    bigUntilMs = 0;
  } else {
    strncpy(bigContent, ascii, sizeof(bigContent) - 1);
    bigContent[sizeof(bigContent)-1] = '\0';
    bigUntilMs = millis() + BIG_TIMEOUT_MS;
    display_log_cat(D_LOG_ACTION, "show letter \"%s\"", bigContent);
  }
  needFullRedraw = true;
}

void displayAdapter_showSymbol(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  bigContent[0] = sym;
  bigContent[1] = '\0';
  bigUntilMs = millis() + BIG_TIMEOUT_MS;
  needFullRedraw = true;
  display_log_cat(D_LOG_ACTION, "show symbol %c", sym);
}