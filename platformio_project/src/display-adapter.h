// File:        display-adapter.h v2.0
// Project:     Morse Transceiver
// Description: Header for OLED display adapter. Enforces split layout (left TX/RX history, right big content),
//              supports splash with bitmap, mode overlay, timed big content, didactic cursor, and status bar.
// Last modification: aligned with display-adapter.cpp v2.0: added mode overlay API, redraw callback,
//                    history clipping constants, and removed duplicate enums (uses network-state.h).
// Modified:    2025-11-18
// Created:     2025-11-15
//
// License:     MIT License

#pragma once

#include <stdint.h>
#include "network-state.h"  // uses ConnectionState (FREE/TX/RX)

// Visible line buffer size used across adapter
#ifndef DISPLAY_ADAPTER_LINE_BUF
  #define DISPLAY_ADAPTER_LINE_BUF 32
#endif

// Optional redraw callback type:
// Called right before a full redraw occurs (e.g., to let external modules update state).
typedef void (*da_redraw_cb_t)(void);

// ====== API ======

// Initialize display and show boot bitmap (from bitmap.h). Safe fallback to Serial logging if OLED init fails.
void displayAdapter_init();

// Show splash message (two lines) for a given duration in ms. While active, splash overrides the layout.
void displayAdapter_showSplash(const char* line1, const char* line2, unsigned long duration_ms);

// Show a centered 2-line overlay message (temporarily overrides layout).
void displayAdapter_showModeMessage(const char* line1, const char* line2);

// Update big content on the right column (ASCII letter/string). Empty/null clears it immediately.
void displayAdapter_showLetter(const char* ascii);

// Update big content with a single symbol ('.' or '-'). Other chars are ignored.
void displayAdapter_showSymbol(char sym);

// Force a full redraw of the layout on the next update cycle.
void displayAdapter_forceRedraw();

// Set an optional callback invoked just before a full redraw.
void displayAdapter_setRedrawCallback(da_redraw_cb_t cb);

// Update routine (call frequently). Handles splash/overlay/big content lifecycles, history snapshot, and redraw.
void displayAdapter_update();

// Update the connection state used by the status bar and right column header (TX/RX).
void displayAdapter_setConnectionState(ConnectionState st);