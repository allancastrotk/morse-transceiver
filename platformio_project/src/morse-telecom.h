// File: morse-telecom.h v1.0
// Description: Morse telecom framing — send helpers and remote event callbacks
// Last modification: local queue and lightweight parser for incoming lines
// Modified: 2025-11-15 03:32
// Created: 2025-11-15

#ifndef MORSE_TELECOM_H
#define MORSE_TELECOM_H

#include <Arduino.h>

// callbacks for remote events
typedef void (*mt_remote_down_cb_t)(void);
typedef void (*mt_remote_up_cb_t)(void);
typedef void (*mt_remote_symbol_cb_t)(char sym, unsigned long dur_ms);

// lifecycle
void morse_telecom_init();
void morse_telecom_update();

// sending (these enqueue to network via nc_enqueueOutgoing)
void morse_telecom_sendDown();
void morse_telecom_sendUp();
void morse_telecom_sendSymbol(char sym, unsigned long dur_ms);

// incoming line handler (call this from network-connect when a line is received)
void morse_telecom_handleIncomingLine(const char* line);

// callbacks registration
void morse_telecom_onRemoteDown(mt_remote_down_cb_t cb);
void morse_telecom_onRemoteUp(mt_remote_up_cb_t cb);
void morse_telecom_onRemoteSymbol(mt_remote_symbol_cb_t cb);

#endif