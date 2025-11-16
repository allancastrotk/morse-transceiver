// File: network-connect.h
// Description: Network connector for Morse project — WiFi + TCP lifecycle, non-blocking
// Exposes a small callback API for higher-level modules to subscribe to connect/disconnect events
// Created: 2025-11-15

#ifndef NETWORK_CONNECT_H
#define NETWORK_CONNECT_H

#include <Arduino.h>

typedef enum { NC_SCANNING, NC_CONNECTING, NC_CONNECTED, NC_AP_MODE, NC_DISCONNECTED } NC_State;

typedef void (*nc_cb_t)(void);
typedef void (*nc_cb_ip_t)(const char* ip);

void initNetworkConnect();
void updateNetworkConnect();

bool nc_isConnected();
bool nc_isActingClient();
const char* nc_getPeerIP();
const char* nc_getRole();

void nc_enqueueOutgoing(const char* line); // queue a line to send when possible

// Callbacks (only one subscriber for each; modules can chain if needed)
void nc_onConnected(nc_cb_t cb);       // called when link considered ready (after handshake)
void nc_onDisconnected(nc_cb_t cb);    // called when link lost
void nc_onAcceptedClient(nc_cb_ip_t cb); // called when server accepted a client (IP provided)

#endif