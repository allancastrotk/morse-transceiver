// File: telegrapher.h v1.4
// Description: Telegrapher module API (non-blocking, ISR-safe key event capture).
//              Handles classification of key press durations into dot/dash,
//              letter finalization, and long-press detection.
// Last modification: aligned with project standard; added finalize and long-press callbacks.
// Modified: 2025-11-18
// Created: 2025-11-15

#ifndef TELEGRAPHER_H
#define TELEGRAPHER_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event type pushed from ISR by morse-key
typedef struct {
  bool down;            // true = key down, false = key up
  unsigned long t_us;   // timestamp in microseconds from ISR
} TG_KeyEvent;

// Callback typedefs
typedef void (*tg_simple_cb_t)(void);
typedef void (*tg_local_symbol_cb_t)(char sym, unsigned long dur_ms);
typedef void (*tg_remote_symbol_cb_t)(char sym, unsigned long dur_ms);
typedef void (*tg_remote_simple_cb_t)(void);

// Core lifecycle
void telegrapher_init(void);
void telegrapher_update(void);
void telegrapher_pushKeyEvent(const TG_KeyEvent* ev);

// Local callbacks (register)
void telegrapher_onLocalSymbol(tg_local_symbol_cb_t cb);
void telegrapher_onLocalDown(tg_simple_cb_t cb);
void telegrapher_onLocalUp(tg_simple_cb_t cb);

// Finalize (letter gap reached) and long-press callbacks
void telegrapher_onFinalize(tg_simple_cb_t cb);
void telegrapher_onLongPress(tg_simple_cb_t cb);

// Backwards-compat shim (maps old API to long-press)
void telegrapher_onModeToggle(tg_simple_cb_t cb);

// Remote callbacks (register)
void telegrapher_onRemoteSymbol(tg_remote_symbol_cb_t cb);
void telegrapher_onRemoteDown(tg_remote_simple_cb_t cb);
void telegrapher_onRemoteUp(tg_remote_simple_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif // TELEGRAPHER_H
