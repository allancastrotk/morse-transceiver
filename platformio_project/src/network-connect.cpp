// File: network-connect.cpp v1.2
// Description: Non-blocking WiFi + TCP connector with outgoing queue, heartbeat and RX -> telecom integration
// Last modification: unified log flags (INFO/ACTION/NERD), clarified nc_isConnected() usage
// Modified: 2025-11-16
// Created: 2025-11-15

#include "network-connect.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include "morse-telecom.h"

// ====== LOG FLAGS ======
#define LOG_NC_INFO   1   // init, status, strength
#define LOG_NC_ACTION 1   // state changes, connect/disconnect, enqueue/send
#define LOG_NC_NERD   0   // scan details, heartbeat, queue internals

// Configuration
static const char* NC_SSID = "morse-transceiver";
static const char* NC_PASS = "";
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint16_t TCP_PORT = 5000;
static const unsigned long SCAN_INTERVAL_MS = 800;
static const unsigned long SCAN_TIMEOUT_MS = 7000;
static const int MAX_SCAN_ATTEMPTS = 3;
static const unsigned long CONNECT_RETRY_MS = 4000;
static const unsigned long CONNECT_WIFI_TIMEOUT_MS = 5000;
static const unsigned long HEARTBEAT_INTERVAL_MS = 1500;
static const unsigned long HEARTBEAT_TIMEOUT_MS = 6000;

// Queue for outgoing lines
#define OUTQ_SIZE 32
struct OutQ { char line[64]; unsigned long ts; };
static OutQ outQueue[OUTQ_SIZE];
static int outHead = 0, outTail = 0, outCount = 0;

// WiFi / TCP internals
static ESP8266WiFiMulti wifiMulti;
static WiFiServer server(TCP_PORT);
static WiFiClient client;

static NC_State state = NC_SCANNING;
static bool actingAsClient = false;

static unsigned long lastScan = 0;
static int scanAttempts = 0;
static int lastScanResult = -2;

static unsigned long connectStart = 0;
static unsigned long lastStatusLog = 0;
static unsigned long lastHeartbeatSent = 0;
static unsigned long lastHeartbeatReceived = 0;

// incoming line buffer
static char rxLineBuf[128];
static int rxLinePos = 0;

// peer info
static char peerIPbuf[32] = "";

// callbacks
static nc_cb_t cb_connected = nullptr;
static nc_cb_t cb_disconnected = nullptr;
static nc_cb_ip_t cb_accepted = nullptr;

// helpers
static void logf(bool flag, const char* fmt, ...) {
#if defined(ARDUINO)
  if (!flag) return;
  va_list ap; va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  Serial.println();
#endif
}

// outgoing queue
static void outq_push(const char* line) {
  if (!line || !*line) return;
  unsigned long now = millis();
  if (outCount >= OUTQ_SIZE) {
    outHead = (outHead + 1) % OUTQ_SIZE;
    outCount--;
  }
  strncpy(outQueue[outTail].line, line, sizeof(outQueue[outTail].line) - 1);
  outQueue[outTail].line[sizeof(outQueue[outTail].line) - 1] = '\0';
  outQueue[outTail].ts = now;
  outTail = (outTail + 1) % OUTQ_SIZE;
  outCount++;
  logf(LOG_NC_ACTION, "%lu - [ACTION] Enqueued: %s (q=%d)", now, line, outCount);
}

static bool outq_send_one_if_connected() {
  if (outCount == 0) return true;
  if (!(client && client.connected())) return false;
  const char* ln = outQueue[outHead].line;
  client.print(ln); client.print("\n"); client.flush();
  logf(LOG_NC_ACTION, "%lu - [ACTION] Sent queued: %s", millis(), ln);
  outHead = (outHead + 1) % OUTQ_SIZE;
  outCount--;
  return true;
}

// ====== Public API ======
void initNetworkConnect() {
  Serial.printf("%lu - [INFO] initNetworkConnect\n", millis());
  randomSeed(analogRead(A0));
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  WiFi.scanNetworks(true, true);
  scanAttempts = 1;
  lastScan = millis();
  server.begin();
  state = NC_SCANNING;
  outHead = outTail = outCount = 0;
  actingAsClient = false;
  peerIPbuf[0] = '\0';
  connectStart = millis();
  lastHeartbeatSent = lastHeartbeatReceived = millis();
  lastStatusLog = millis();
  rxLinePos = 0;

  logf(LOG_NC_INFO, "%lu - [INFO] network-connect initialized (SCANNING)", millis());
}

void nc_enqueueOutgoing(const char* line) { outq_push(line); }

bool nc_isConnected() { return (state == NC_CONNECTED); }
bool nc_isActingClient() { return actingAsClient; }
const char* nc_getPeerIP() { return peerIPbuf; }

const char* nc_getRole() {
  if (state == NC_AP_MODE) return "AP";
  if (state == NC_CONNECTED && actingAsClient) return "CLIENT";
  if (state == NC_CONNECTED && !actingAsClient) return "PEER";
  return "NONE";
}

// Strength string
const char* getNetworkStrength() {
  static char s[5];
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int bars = (rssi >= -55) ? 4 :
               (rssi >= -65) ? 3 :
               (rssi >= -75) ? 2 :
               (rssi >= -85) ? 1 : 0;
    snprintf(s, sizeof(s), " %d", bars);
  } else {
    strncpy(s, " OFF", sizeof(s) - 1);
    s[sizeof(s) - 1] = '\0';
  }
  return s;
}

void nc_onConnected(nc_cb_t cb) { cb_connected = cb; }
void nc_onDisconnected(nc_cb_t cb) { cb_disconnected = cb; }
void nc_onAcceptedClient(nc_cb_ip_t cb) { cb_accepted = cb; }

// Internal: process client incoming data into lines
static void processClientIncoming() {
  while (client && client.connected() && client.available()) {
    int c = client.read();
    if (c <= 0) break;

    if (c == '\n' || rxLinePos >= (int)sizeof(rxLineBuf) - 1) {
      rxLineBuf[rxLinePos] = '\0';
      if (rxLinePos > 0 && rxLineBuf[rxLinePos - 1] == '\r') rxLineBuf[rxLinePos - 1] = '\0';

      logf(LOG_NC_ACTION, "%lu - [ACTION] RX raw: %s", millis(), rxLineBuf);

      if (strcmp(rxLineBuf, "alive") == 0) {
        lastHeartbeatReceived = millis();
        if (!actingAsClient) {
          client.print("alive_ack\n");
          client.flush();
          logf(LOG_NC_ACTION, "%lu - [ACTION] Sent: alive_ack", millis());
        }
      } else if (strcmp(rxLineBuf, "alive_ack") == 0) {
        lastHeartbeatReceived = millis();
      } else {
        morse_telecom_handleIncomingLine(rxLineBuf);
      }

      rxLinePos = 0;
    } else if (c != '\r') {
      rxLineBuf[rxLinePos++] = (char)c;
    }
  }
}

// Try to accept a new client in AP mode
static void tryAcceptClient() {
  WiFiClient newClient = server.accept();
  if (newClient && !client.connected()) {
    client = newClient;
    strncpy(peerIPbuf, client.remoteIP().toString().c_str(), sizeof(peerIPbuf) - 1);
    peerIPbuf[sizeof(peerIPbuf) - 1] = '\0';
    actingAsClient = false;
    lastHeartbeatReceived = lastHeartbeatSent = millis();
    state = NC_CONNECTED;
    logf(LOG_NC_ACTION, "%lu - [ACTION] Accepted TCP client %s", millis(), peerIPbuf);
    while (outCount > 0) {
      if (!outq_send_one_if_connected()) break;
    }
    if (cb_accepted) cb_accepted(peerIPbuf);
    if (cb_connected) cb_connected();
  } else if (newClient) {
    newClient.stop();
    logf(LOG_NC_ACTION, "%lu - [ACTION] Refused extra client", millis());
  }
}

// Public update: call frequently from main loop
void updateNetworkConnect() {
  unsigned long now = millis();

  // Periodic status log
  if (now - lastStatusLog >= 5000) {
    logf(LOG_NC_INFO, "%lu - [INFO] WiFi.status=%d state=%d client.connected=%d",
         now, WiFi.status(), (int)state, client.connected());
    lastStatusLog = now;
  }

  switch (state) {
    case NC_SCANNING: {
      if (now - lastScan < SCAN_INTERVAL_MS) break;
      int n = WiFi.scanComplete();
      if (n != lastScanResult) {
        lastScanResult = n;
        logf(LOG_NC_NERD, "%lu - [NERD] scanComplete=%d", now, n);
      }

      if (n == WIFI_SCAN_RUNNING) {
        if (now - lastScan > SCAN_TIMEOUT_MS) {
          WiFi.scanDelete();
          scanAttempts++;
          logf(LOG_NC_ACTION, "%lu - [ACTION] scan timeout attempts=%d", now, scanAttempts);
          WiFi.scanNetworks(true, true);
          lastScan = now;
        }
      } else if (n >= 0 && scanAttempts <= MAX_SCAN_ATTEMPTS) {
        bool found = false;
        int targetCh = 1;
        for (int i = 0; i < n; ++i) {
          if (strcmp(WiFi.SSID(i).c_str(), NC_SSID) == 0) {
            found = true;
            targetCh = WiFi.channel(i);
            logf(LOG_NC_ACTION, "%lu - [ACTION] Found SSID ch=%d rssi=%d",
                 now, targetCh, WiFi.RSSI(i));
            break;
          }
        }
        WiFi.scanDelete();
        if (found) {
          WiFi.begin(NC_SSID, NC_PASS, targetCh);
          state = NC_CONNECTING;
          connectStart = now;
          logf(LOG_NC_ACTION, "%lu - [ACTION] Joining SSID on ch=%d", now, targetCh);
        } else {
          scanAttempts++;
          if (scanAttempts > MAX_SCAN_ATTEMPTS) {
            WiFi.mode(WIFI_AP_STA);
            int apChannel = 1;
            WiFi.softAP(NC_SSID, NC_PASS, apChannel);
            server.begin();
            state = NC_AP_MODE;
            logf(LOG_NC_ACTION, "%lu - [ACTION] Entered AP_MODE ch=%d", now, apChannel);
          } else {
            WiFi.scanNetworks(true, true);
            lastScan = now;
            logf(LOG_NC_NERD, "%lu - [NERD] SSID not found, scanning again attempt=%d", now, scanAttempts);
          }
        }
      } else {
        WiFi.scanNetworks(true, true);
        lastScan = now;
      }
    } break;

    case NC_CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) {
        if (WiFi.localIP() == AP_IP) {
          state = NC_AP_MODE;
          logf(LOG_NC_ACTION, "%lu - [ACTION] local IP equals AP_IP, switching to AP_MODE", now);
          break;
        }
        if (now - connectStart >= CONNECT_RETRY_MS) {
          if (client.connect(AP_IP, TCP_PORT)) {
            actingAsClient = true;
            state = NC_CONNECTED;
            lastHeartbeatSent = lastHeartbeatReceived = now;
            strncpy(peerIPbuf, AP_IP.toString().c_str(), sizeof(peerIPbuf) - 1);
            peerIPbuf[sizeof(peerIPbuf) - 1] = '\0';
            logf(LOG_NC_ACTION, "%lu - [ACTION] TCP client connected to %s:%d",
                 now, AP_IP.toString().c_str(), TCP_PORT);
            while (outCount > 0) {
              if (!outq_send_one_if_connected()) break;
            }
            if (cb_connected) cb_connected();
          } else {
            connectStart = now;
            logf(LOG_NC_NERD, "%lu - [NERD] client.connect failed, retrying", now);
          }
        }
      } else if (now - connectStart > CONNECT_WIFI_TIMEOUT_MS) {
        state = NC_DISCONNECTED;
        logf(LOG_NC_ACTION, "%lu - [ACTION] WiFi STA did not complete connect; DISCONNECTED", now);
        if (cb_disconnected) cb_disconnected();
      }
    } break;

    case NC_CONNECTED: {
      if (!client || !client.connected()) {
        logf(LOG_NC_ACTION, "%lu - [ACTION] TCP lost", now);
        client.stop();
        if (actingAsClient) {
          actingAsClient = false;
          state = NC_CONNECTING;
          WiFi.reconnect();
          connectStart = now;
          logf(LOG_NC_ACTION, "%lu - [ACTION] actingAsClient lost; will reconnect", now);
          if (cb_disconnected) cb_disconnected();
        } else {
          state = NC_AP_MODE;
          logf(LOG_NC_ACTION, "%lu - [ACTION] AP client disconnected; back to AP_MODE", now);
          if (cb_disconnected) cb_disconnected();
        }
        break;
      }

      if (actingAsClient && (now - lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS)) {
        client.print("alive\n");
        client.flush();
        lastHeartbeatSent = now;
        logf(LOG_NC_NERD, "%lu - [NERD] Sent: alive", now);
      }

      if ((now - lastHeartbeatReceived) >= HEARTBEAT_TIMEOUT_MS) {
        client.stop();
        if (actingAsClient) {
          actingAsClient = false;
          state = NC_CONNECTING;
          WiFi.reconnect();
          connectStart = now;
          logf(LOG_NC_ACTION, "%lu - [ACTION] HB timeout; client will reconnect", now);
          if (cb_disconnected) cb_disconnected();
        } else {
          state = NC_AP_MODE;
          logf(LOG_NC_ACTION, "%lu - [ACTION] HB timeout in AP_MODE; switching to AP_MODE", now);
          if (cb_disconnected) cb_disconnected();
        }
        break;
      }

      while (outCount > 0) {
        if (!outq_send_one_if_connected()) break;
      }
      processClientIncoming();
    } break;

    case NC_AP_MODE: {
      tryAcceptClient();
      if (client && client.connected()) {
        while (outCount > 0) {
          if (!outq_send_one_if_connected()) break;
        }
        processClientIncoming();
      }
      if ((millis() - lastScan) > 20000) {
        int n2 = WiFi.scanNetworks(false, true);
        bool found = false;
        int otherCh = 1;
        if (n2 > 0) {
          for (int i = 0; i < n2; ++i) {
            if (strcmp(WiFi.SSID(i).c_str(), NC_SSID) == 0) {
              found = true;
              otherCh = WiFi.channel(i);
              break;
            }
          }
        }
        WiFi.scanDelete();
        lastScan = millis();
        if (found) {
          WiFi.begin(NC_SSID, NC_PASS, otherCh);
          state = NC_CONNECTING;
          connectStart = millis();
          logf(LOG_NC_ACTION, "%lu - [ACTION] Detected external SSID; switching to CONNECTING", millis());
        }
      }
    } break;

    case NC_DISCONNECTED: {
      if (millis() - connectStart > CONNECT_RETRY_MS) {
        WiFi.begin(NC_SSID, NC_PASS);
        state = NC_CONNECTING;
        connectStart = millis();
        logf(LOG_NC_ACTION, "%lu - [ACTION] DISCONNECTED -> CONNECTING (retry)", millis());
      }
    } break;
  } // switch
}