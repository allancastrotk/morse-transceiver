// File: morse-telecom.h
// Description: morse-telecom module — protocol framing and high-level messaging
// Provides sending helpers and callbacks for remote events.
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