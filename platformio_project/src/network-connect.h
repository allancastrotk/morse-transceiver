// File: network-connect.h v1.2
// Description: Public API for non-blocking WiFi + TCP connector (ESP8266).
// Last modification: unified log flags, added nc_isConnected() accessor.
// Modified: 2025-11-16
// Created: 2025-11-15

#ifndef NETWORK_CONNECT_H
#define NETWORK_CONNECT_H

#include <Arduino.h>

// Connection states
enum NC_State {
  NC_SCANNING,
  NC_CONNECTING,
  NC_CONNECTED,
  NC_AP_MODE,
  NC_DISCONNECTED
};

// Callback types
typedef void (*nc_cb_t)(void);
typedef void (*nc_cb_ip_t)(const char* ip);

// ====== Public API ======
void initNetworkConnect();
void updateNetworkConnect();

// Outgoing queue
void nc_enqueueOutgoing(const char* line);

// Connection status
bool nc_isConnected();
bool nc_isActingClient();
const char* nc_getPeerIP();
const char* nc_getRole();

// Signal strength (bars or "OFF")
const char* getNetworkStrength();

// Callbacks
void nc_onConnected(nc_cb_t cb);
void nc_onDisconnected(nc_cb_t cb);
void nc_onAcceptedClient(nc_cb_ip_t cb);

#endif // NETWORK_CONNECT_H