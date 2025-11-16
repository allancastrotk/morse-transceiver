// File: display-adapter.h v1.5
// Description: Adapter between history/network-state and SSD1306 OLED display.
//              Provides splash, redraw, history clipping, big-letter rendering, and didactic cursor.
// Last modification: removed log flags from header; added showSymbol declaration
// Modified: 2025-11-18 03:20
// Created: 2025-11-15

#ifndef DISPLAY_ADAPTER_H
#define DISPLAY_ADAPTER_H

#include <Arduino.h>
#include "network-state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Buffer size for line snapshots (safe > max visible length)
#define DISPLAY_ADAPTER_LINE_BUF 32

// Redraw callback type
typedef void (*da_redraw_cb_t)(void);

// Initialization and update
void displayAdapter_init(void);
void displayAdapter_update(void);
void displayAdapter_forceRedraw(void);

// Splash screen (bitmap + optional text lines)
void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms);

// Optional redraw callback (called before each full redraw)
void displayAdapter_setRedrawCallback(da_redraw_cb_t cb);

// Show a small ASCII string/letter prominently in right column (auto timeout ~1.5s)
void displayAdapter_showLetter(const char* ascii);

// Show a single Morse symbol ('.' or '-') prominently in right column (auto timeout ~1.5s)
void displayAdapter_showSymbol(char sym);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_ADAPTER_H