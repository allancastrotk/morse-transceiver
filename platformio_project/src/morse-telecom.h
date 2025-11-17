// File: morse-telecom.h v1.1
// Description: Morse telecom framing — send helpers and remote event callbacks
// Last modification: unified formatting, consistent header style
// Modified: 2025-11-16
// Created: 2025-11-15

#ifndef MORSE_TELECOM_H
#define MORSE_TELECOM_H

#include <Arduino.h>

// ====== Callback types ======
typedef void (*mt_remote_down_cb_t)(void);
typedef void (*mt_remote_up_cb_t)(void);
typedef void (*mt_remote_symbol_cb_t)(char sym, unsigned long dur_ms);

// ====== Lifecycle ======
void morse_telecom_init();
void morse_telecom_update();

// ====== Sending helpers ======
// These enqueue to network via nc_enqueueOutgoing
void morse_telecom_sendDown();
void morse_telecom_sendUp();
void morse_telecom_sendSymbol(char sym, unsigned long dur_ms);

// ====== Incoming line handler ======
// Call this from network-connect when a line is received
void morse_telecom_handleIncomingLine(const char* line);

// ====== Callbacks registration ======
void morse_telecom_onRemoteDown(mt_remote_down_cb_t cb);
void morse_telecom_onRemoteUp(mt_remote_up_cb_t cb);
void morse_telecom_onRemoteSymbol(mt_remote_symbol_cb_t cb);

#endif // MORSE_TELECOM_H