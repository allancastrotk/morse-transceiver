#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

typedef enum { SCANNING, CONNECTING, CONNECTED, AP_MODE, DISCONNECTED } NetworkState;

extern NetworkState netState;

void initNetwork();
void updateNetwork();
bool occupyNetwork();
bool isConnected();
void sendDuration(unsigned long duration);
const char* getNetworkStrength();

// DOWN/UP model
void network_sendDown();
void network_sendUp();

// status helpers for display
const char* getNetworkRole();
const char* getPeerIP();
const char* getLastNetworkEvent();
unsigned long getLastNetworkEventAt();

#endif