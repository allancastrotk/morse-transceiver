#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>

enum NetworkState { SCANNING, CONNECTING, CONNECTED, AP_MODE, DISCONNECTED };

void initNetwork();
void updateNetwork();
bool occupyNetwork();
bool isConnected();
void sendDuration(unsigned long duration);
const char* getNetworkStrength();

extern NetworkState netState;

#endif
