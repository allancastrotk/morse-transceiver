// File: display-adapter.cpp
// Implementation of display-adapter.h
// - Non-blocking adapter that preserves original display structure
// - Reads history snapshot and network-state to update the UI
// - Minimizes redraws: redraws only when history version or state changes, or when splash is active
// - Uses Adafruit_SSD1306 by default; adapt the draw calls if you use another library
// Modified: 2025-11-15

#include "display-adapter.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Config: adapt to your screen
#ifndef SCREEN_WIDTH
  #define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
  #define SCREEN_HEIGHT 64
#endif
#ifndef SSD1306_RESET
  #define SSD1306_RESET -1
#endif

// Create display instance (modify if using a different driver)
static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SSD1306_RESET);

// Internal state
static unsigned long lastHistoryVersion = 0;
static ConnectionState lastState = FREE;
static bool needFullRedraw = false;
static unsigned long lastUpdateMs = 0;
static unsigned long updateIntervalMs = 100; // throttle actual screen updates

// Splash state
static bool splashActive = false;
static unsigned long splashUntilMs = 0;
static char splashLine1[DISPLAY_ADAPTER_LINE_BUF];
static char splashLine2[DISPLAY_ADAPTER_LINE_BUF];

// Optional redraw callback
static da_redraw_cb_t redrawCb = nullptr;

// Local buffers for snapshot
static char txTop[DISPLAY_ADAPTER_LINE_BUF];
static char txMid[DISPLAY_ADAPTER_LINE_BUF];
static char txBot[DISPLAY_ADAPTER_LINE_BUF];
static char rxTop[DISPLAY_ADAPTER_LINE_BUF];
static char rxMid[DISPLAY_ADAPTER_LINE_BUF];
static char rxBot[DISPLAY_ADAPTER_LINE_BUF];

// Helper: draw status bar (mode + network state)
static void drawStatusBar(ConnectionState st) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int x = 0, y = SCREEN_HEIGHT - 10;
  display.setCursor(x, y);
  const char* stateStr = (st == TX) ? "TX" : (st == RX) ? "RX" : "FREE";
  char buf[32];
  snprintf(buf, sizeof(buf), "[%s] ", stateStr);
  display.print(buf);
  // Show activity indicator (last activity age) if desired
  unsigned long lastAct = ns_lastActivityMs();
  unsigned long age = (millis() > lastAct) ? (millis() - lastAct) : 0;
  if (age < 2000) display.print("*");
  else display.print(" ");
}

// Internal: perform a full screen redraw with current buffers and state
static void doFullRedraw(ConnectionState st) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Top area: TX lines (compact)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TX:");
  display.setCursor(0, 10);
  display.print(txTop);
  display.setCursor(0, 18);
  display.print(txMid);
  display.setCursor(0, 26);
  display.print(txBot);

  // Middle area: large main line (could be used for last letter or big letter)
  // Keep original structure: show latest TX top as a big marker if desired
  // For compatibility, we keep the middle as RX top as main focus
  display.setTextSize(1);
  display.setCursor(64, 0);
  display.print("RX:");
  display.setCursor(64, 10);
  display.print(rxTop);
  display.setCursor(64, 18);
  display.print(rxMid);
  display.setCursor(64, 26);
  display.print(rxBot);

  // Status bar at bottom
  drawStatusBar(st);

  display.display();
}

// Public API
void displayAdapter_init() {
  // Initialize physical display; if this fails, fall back to Serial logging
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display init failed; falling back to Serial output");
    // still keep adapter operational; doSerialDraws
  } else {
    display.clearDisplay();
    display.display();
  }
  lastHistoryVersion = 0;
  lastState = FREE;
  needFullRedraw = true;
  splashActive = false;
  splashUntilMs = 0;
  splashLine1[0] = '\0';
  splashLine2[0] = '\0';
  txTop[0] = txMid[0] = txBot[0] = '\0';
  rxTop[0] = rxMid[0] = rxBot[0] = '\0';
  Serial.printf("%lu - display-adapter initialized\n", millis());
}

// Show splash non-blocking
void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms) {
  if (line1) strncpy(splashLine1, line1, sizeof(splashLine1)-1); else splashLine1[0] = '\0';
  if (line2) strncpy(splashLine2, line2, sizeof(splashLine2)-1); else splashLine2[0] = '\0';
  splashLine1[sizeof(splashLine1)-1] = '\0';
  splashLine2[sizeof(splashLine2)-1] = '\0';
  splashUntilMs = millis() + duration_ms;
  splashActive = true;
  needFullRedraw = true;
}

// Force a redraw in next update
void displayAdapter_forceRedraw() {
  needFullRedraw = true;
  lastHistoryVersion = 0; // force snapshot re-read
}

// Set optional redraw callback
void displayAdapter_setRedrawCallback(da_redraw_cb_t cb) {
  redrawCb = cb;
}

// Main non-blocking update; call from loop
void displayAdapter_update() {
  unsigned long now = millis();
  if (now - lastUpdateMs < updateIntervalMs) return;
  lastUpdateMs = now;

  // If splash active, draw splash and return (splash overlays normal display)
  if (splashActive) {
    if (now >= splashUntilMs) {
      splashActive = false;
      needFullRedraw = true;
    } else {
      // Draw splash
      if (display.width() > 0) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 10);
        display.print(splashLine1);
        display.setTextSize(1);
        display.setCursor(0, 42);
        display.print(splashLine2);
        display.display();
      } else {
        // Fallback to Serial
        Serial.printf("%lu - SPLASH: %s / %s\n", millis(), splashLine1, splashLine2);
      }
      return;
    }
  }

  // Normal operation: get snapshot from history
  unsigned long v = history_getSnapshot(txTop, txMid, txBot, rxTop, rxMid, rxBot, sizeof(txTop));
  ConnectionState st = ns_getState();

  // Determine if redraw required
  if (v != lastHistoryVersion || st != lastState || needFullRedraw) {
    lastHistoryVersion = v;
    lastState = st;
    needFullRedraw = false;
    // call optional callback
    if (redrawCb) redrawCb();
    // perform redraw
    if (display.width() > 0) {
      doFullRedraw(st);
    } else {
      // Serial fallback: print snapshot
      Serial.printf("DISPLAY REDRAW state=%s v=%lu\n", (st==TX)?"TX":(st==RX)?"RX":"FREE", v);
      Serial.printf("TX> %s\n   %s\n   %s\n", txTop, txMid, txBot);
      Serial.printf("RX> %s\n   %s\n   %s\n", rxTop, rxMid, rxBot);
    }
  }
}