// File: network-state.h v1.2
// Description: Header for network-state.cpp, defines ConnectionState enum and callback types.
// Last modification: added CONTENTION state for conflict resolution
// Modified: 2025-11-18
// Created: 2025-11-15

#ifndef NETWORK_STATE_H
#define NETWORK_STATE_H

// Enum de estados de conexão
typedef enum {
    FREE = 0,       // idle/offline
    TX   = 1,       // transmitindo local
    RX   = 2,       // recebendo remoto
    CONTENTION = 3  // conflito detectado (local e remoto pressionados juntos)
} ConnectionState;

// Callback types
typedef void (*ns_state_cb_t)(ConnectionState s);
typedef void (*ns_local_send_cb_t)(void);
typedef void (*ns_local_symbol_cb_t)(char sym, unsigned long dur_ms);
typedef void (*ns_remote_symbol_cb_t)(char sym, unsigned long dur_ms);

// Public API
void initNetworkState();
ConnectionState ns_getState();
unsigned long ns_lastActivityMs();

void ns_onStateChange(ns_state_cb_t cb);
void ns_onLocalSendDown(ns_local_send_cb_t cb);
void ns_onLocalSendUp(ns_local_send_cb_t cb);
void ns_onLocalSymbol(ns_local_symbol_cb_t cb);
void ns_onRemoteSymbol(ns_remote_symbol_cb_t cb);

void ns_requestLocalDown();
void ns_requestLocalUp();
void ns_requestLocalSymbol(char sym, unsigned long dur_ms);

void ns_notifyRemoteDown();
void ns_notifyRemoteUp();
void ns_notifyRemoteSymbol(char sym, unsigned long dur_ms);

void updateNetworkState();

#endif // NETWORK_STATE_H