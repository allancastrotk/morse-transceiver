// File: display-adapter.h
// Description: Adapter between history, network-state and the physical display
// - Keeps the original display layout and update behavior (non-blocking, redraw only on change)
// - Shows splash at startup (non-blocking), three TX lines and three RX lines, and a status area with state (TX/RX/FREE) and mode
// - Uses history_getSnapshot() and ns_getState() to obtain content
// Created: 2025-11-15
// Corrected: 2025-11-15 (consistent types, include guards, minimal dependencies)

#ifndef DISPLAY_ADAPTER_H
#define DISPLAY_ADAPTER_H

#include <Arduino.h>
#include "history.h"
#include "network-state.h"

// Initialize the display adapter. Provide pin or config if required.
// If using Adafruit_SSD1306, initialize the library before calling displayAdapter_init if needed.
void displayAdapter_init();

// Non-blocking periodic update; call frequently from loop()
void displayAdapter_update();

// Force a full redraw on next update
void displayAdapter_forceRedraw();

// Splash management
void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms);

// Optional: register callback when user requests redraw (not used by default)
typedef void (*da_redraw_cb_t)(void);
void displayAdapter_setRedrawCallback(da_redraw_cb_t cb);

// Simple helpers for text dimensions the adapter expects (adjust for your display)
#define DISPLAY_ADAPTER_LINE_BUF 32
#define DISPLAY_ADAPTER_ROWS 3

#endif // DISPLAY_ADAPTER_H