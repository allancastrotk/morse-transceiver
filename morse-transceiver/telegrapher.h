// File: telegrapher.h
// Description: Telegrapher module — central operator logic for local key and remote events
// - Receives KeyDown/KeyUp from morse-key and remote events from morse-telecom
// - Classifies dot/dash/ignore, detects mode-hold, letter gaps, and emits high-level events
// Created: 2025-11-15

#ifndef TELEGRAPHER_H
#define TELEGRAPHER_H

#include <Arduino.h>

// High-level symbol event
typedef void (*tg_local_symbol_cb_t)(char sym, unsigned long dur_ms); // local symbol ready
typedef void (*tg_local_down_cb_t)(void); // local down started (to request network send DOWN)
typedef void (*tg_local_up_cb_t)(void);   // local up ended (to request network send UP)
typedef void (*tg_mode_toggle_cb_t)(void); // mode toggled (DIDACTIC <-> MORSE)

// Remote events forwarded by telegrapher (after morse-telecom parsing)
typedef void (*tg_remote_symbol_cb_t)(char sym, unsigned long dur_ms);
typedef void (*tg_remote_down_cb_t)(void);
typedef void (*tg_remote_up_cb_t)(void);

// Public lifecycle
void telegrapher_init();
void telegrapher_update(); // call frequently from main loop

// Callbacks registration (who wants to know high-level telegrapher events)
void telegrapher_onLocalSymbol(tg_local_symbol_cb_t cb);
void telegrapher_onLocalDown(tg_local_down_cb_t cb);
void telegrapher_onLocalUp(tg_local_up_cb_t cb);
void telegrapher_onModeToggle(tg_mode_toggle_cb_t cb);

void telegrapher_onRemoteSymbol(tg_remote_symbol_cb_t cb);
void telegrapher_onRemoteDown(tg_remote_down_cb_t cb);
void telegrapher_onRemoteUp(tg_remote_up_cb_t cb);

// Input APIs used by morse-key (hardware) and morse-telecom (remote)
typedef struct { bool down; unsigned long t_us; } TG_KeyEvent;
bool telegrapher_pushKeyEvent(const TG_KeyEvent* ev); // called from morse-key (ISR-safe wrapper should be used)
void telegrapher_handleRemoteDown();  // called by morse-telecom
void telegrapher_handleRemoteUp();    // called by morse-telecom
void telegrapher_handleRemoteSymbol(char sym, unsigned long dur_ms); // called by morse-telecom

// Query
bool telegrapher_isInHoldMode(); // true if currently in MODE_HOLD_MS hold (useful for UI)

#endif