// File: network-state.h v1.0
// Description: Link state manager (TX/RX/FREE) with local/remote events and callbacks
// Last modification: added timing constants and query helpers
// Modified: 2025-11-15 03:32
// Created: 2025-11-15

#ifndef NETWORK_STATE_H
#define NETWORK_STATE_H

#include <Arduino.h>

// Shared connection state used across modules (moved here to remove dependency on cw-transceiver.h)
typedef enum {
  FREE = 0,
  TX   = 1,
  RX   = 2
} ConnectionState;

// Optional timing constants that some modules expect (override before include if needed)
#ifndef DOT_MAX
  #define DOT_MAX        160
#endif
#ifndef DASH_MAX
  #define DASH_MAX       480
#endif
#ifndef MODE_HOLD_MS
  #define MODE_HOLD_MS   1500
#endif
#ifndef LONG_PRESS_MS
  #define LONG_PRESS_MS  2000
#endif
#ifndef LETTER_GAP_MS
  #define LETTER_GAP_MS  360
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ns_state_cb_t)(ConnectionState newState);
typedef void (*ns_local_send_cb_t)(void);                // request to send DOWN (start) or UP (stop)
typedef void (*ns_local_symbol_cb_t)(char sym, unsigned long dur_ms); // when local generates a symbol
typedef void (*ns_remote_symbol_cb_t)(char sym, unsigned long dur_ms); // remote symbol arrived

void initNetworkState();
void updateNetworkState();

// Called by CW layer when user presses/releases
void ns_requestLocalDown();         // local key pressed (start TX)
void ns_requestLocalUp();           // local key released (stop TX)
void ns_requestLocalSymbol(char sym, unsigned long dur_ms); // local symbol produced (dot/dash)

// Called by telecom/connect module when remote events arrive
void ns_notifyRemoteDown();         // remote pressed (RX starts)
void ns_notifyRemoteUp();           // remote released (RX ends)
void ns_notifyRemoteSymbol(char sym, unsigned long dur_ms); // remote symbol (decoded by telecom)

// Callbacks registration
void ns_onStateChange(ns_state_cb_t cb);
void ns_onLocalSendDown(ns_local_send_cb_t cb);
void ns_onLocalSendUp(ns_local_send_cb_t cb);
void ns_onLocalSymbol(ns_local_symbol_cb_t cb);
void ns_onRemoteSymbol(ns_remote_symbol_cb_t cb);

// Query
ConnectionState ns_getState();
unsigned long ns_lastActivityMs();

#ifdef __cplusplus
}
#endif

#endif // NETWORK_STATE_H