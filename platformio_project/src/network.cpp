/* network.cpp — Morse Transceiver v6.1
   Gerencia Wi-Fi, heartbeat e mensagens DOWN/UP
*/

#include "network.h"
#include "cw-transceiver.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <stdarg.h>

// LOG FLAGS (reduzidos por padrão)
#define LOG_INIT     1
#define LOG_UPDATE   1
#define LOG_SCAN     1
#define LOG_CONNECT  1
#define LOG_AP       1
#define LOG_RX       1
#define LOG_TX       1
#define LOG_STRENGTH 1

static ESP8266WiFiMulti wifiMulti;
static WiFiServer server(5000);
static WiFiClient client;

static const char* SSID = "morse-transceiver";
static const char* PASS = "";

static const IPAddress AP_IP(192, 168, 4, 1);

static unsigned long lastHeartbeatSent = 0;
static unsigned long lastHeartbeatReceived = 0;
static unsigned long lastStatusCheck = 0;
static unsigned long lastScan = 0;
static unsigned long connectStart = 0;
static unsigned long lastRetry = 0;
static unsigned long retryDelay = 10000;

static int scanAttempts = 0;
static bool scanInProgress = false;
static int lastScanResult = -2;

NetworkState netState = SCANNING;
static bool actingAsClient = false;

static const unsigned long SCAN_INTERVAL_MS = 800;
static const unsigned long SCAN_TIMEOUT_MS = 7000;
static const int MAX_SCAN_ATTEMPTS = 3;
static const unsigned long CONNECT_RETRY_MS = 4000;
static const unsigned long CONNECT_WIFI_TIMEOUT_MS = 5000;

static const unsigned long HEARTBEAT_INTERVAL_ACTIVE_MS = 1000;
static const unsigned long HEARTBEAT_INTERVAL_IDLE_MS   = 10000;
static const unsigned long HEARTBEAT_TIMEOUT_ACTIVE_MS  = 8000;
static const unsigned long HEARTBEAT_TIMEOUT_IDLE_MS    = 30000;

static char peerIPbuf[16] = "";
static char lastNetEventBuf[32] = "";
static unsigned long lastNetEventAt = 0;

static void log_if(bool flag, const char *fmt, ...) {
  if (!flag) return;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}

static void safe_strncpy(char* dst, const char* src, size_t n) {
  if (!dst) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, n-1);
  dst[n-1] = '\0';
}

void initNetwork() {
  unsigned long now = millis();
  randomSeed(analogRead(0));
  delay(random(0, 1500));

  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  WiFi.scanNetworks(true, true);
  scanInProgress = true;
  scanAttempts = 1;
  lastScan = now;

  server.begin();

  netState = SCANNING;

  log_if(LOG_INIT, "%lu - Network init state=%d", now, netState);
}

static void updateLastEvent(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(lastNetEventBuf, sizeof(lastNetEventBuf), fmt, ap);
  va_end(ap);
  lastNetEventAt = millis();
}

static void sendLineToClient(const char* line) {
  if (client && client.connected()) {
    client.print(line);
    client.print("\n");
    client.flush();
    log_if(LOG_TX, "%lu - Sent: %s", millis(), line);
    updateLastEvent("TX:%s", line);
  } else {
    log_if(LOG_TX, "%lu - sendLineToClient: no client", millis());
  }
}

void network_sendDown() {
  if (client && client.connected()) {
    sendLineToClient("DOWN");
  } else {
    log_if(LOG_TX, "%lu - network_sendDown: no client", millis());
  }
}

void network_sendUp() {
  if (client && client.connected()) {
    sendLineToClient("UP");
  } else {
    log_if(LOG_TX, "%lu - network_sendUp: no client", millis());
  }
}

void sendDuration(unsigned long duration) {
  if (isConnected() && client.connected()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "duration:%lu", duration);
    sendLineToClient(buf);
  } else {
    log_if(LOG_TX, "%lu - sendDuration: no client", millis());
  }
}

void updateNetwork() {
  unsigned long now = millis();

  if (now - lastStatusCheck > 5000) {
    log_if(LOG_UPDATE, "%lu - WiFi.status: %d netState: %d client.connected: %d",
           now, WiFi.status(), netState, client.connected());
    lastStatusCheck = now;
  }

  switch (netState) {
    case SCANNING: {
      if (now - lastScan < SCAN_INTERVAL_MS) break;
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        if (now - lastScan > SCAN_TIMEOUT_MS) {
          WiFi.scanDelete();
          scanInProgress = false;
          scanAttempts++;
        }
      } else if (n >= 0 && scanAttempts <= MAX_SCAN_ATTEMPTS) {
        bool found = false;
        int targetCh = 1;
        for (int i = 0; i < n; ++i) {
          if (strcmp(WiFi.SSID(i).c_str(), SSID) == 0) {
            found = true;
            targetCh = WiFi.channel(i);
            break;
          }
        }
        WiFi.scanDelete();

        if (found) {
          WiFi.begin(SSID, PASS, targetCh);
          netState = CONNECTING;
          connectStart = now;
          lastRetry = now;
        } else {
          scanAttempts++;
          if (scanAttempts > MAX_SCAN_ATTEMPTS) {
            WiFi.mode(WIFI_AP_STA);
            int apChannel = 1;
            WiFi.softAP(SSID, PASS, apChannel);
            server.begin();
            netState = AP_MODE;
            lastRetry = now;
            updateLastEvent("AP_MODE");
          } else {
            WiFi.scanNetworks(true, true);
            scanInProgress = true;
            lastScan = now;
          }
        }
      } else if (scanAttempts > MAX_SCAN_ATTEMPTS) {
        WiFi.mode(WIFI_AP_STA);
        int apChannel = 1;
        WiFi.softAP(SSID, PASS, apChannel);
        server.begin();
        netState = AP_MODE;
        lastRetry = now;
        updateLastEvent("AP_MODE");
      } else {
        WiFi.scanNetworks(true, true);
        scanInProgress = true;
        lastScan = now;
      }
    } break;

    case CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) {
        if (WiFi.softAPgetStationNum() > 0) {
          netState = CONNECTED;
          lastHeartbeatReceived = now;
          updateLastEvent("LOCAL_STA_PRESENT");
          break;
        }

        if (WiFi.localIP() == AP_IP) {
          netState = AP_MODE;
          updateLastEvent("SKIP_SELF_CONNECT");
          break;
        }

        if (now - connectStart > CONNECT_RETRY_MS) {
          if (client.connect(AP_IP, 5000)) {
            netState = CONNECTED;
            actingAsClient = true;
            lastHeartbeatSent = now;
            lastHeartbeatReceived = now;
            safe_strncpy(peerIPbuf, AP_IP.toString().c_str(), sizeof(peerIPbuf));
            updateLastEvent("CONNECT");
          } else {
            connectStart = now;
            lastRetry = now;
          }
        }
      } else if (now - connectStart > CONNECT_WIFI_TIMEOUT_MS) {
        netState = DISCONNECTED;
        lastRetry = now;
        updateLastEvent("CONNECT_TIMEOUT");
      }
    } break;

    case CONNECTED: {
      if (!client.connected()) {
        netState = DISCONNECTED;
        lastRetry = now;
        actingAsClient = false;
        updateLastEvent("TCP_LOST");
        break;
      }

      unsigned long hbInterval = (getConnectionState() == FREE) ? HEARTBEAT_INTERVAL_IDLE_MS : HEARTBEAT_INTERVAL_ACTIVE_MS;
      unsigned long hbTimeout  = (getConnectionState() == FREE) ? HEARTBEAT_TIMEOUT_IDLE_MS : HEARTBEAT_TIMEOUT_ACTIVE_MS;

      if (actingAsClient && (now - lastHeartbeatSent > hbInterval)) {
        sendLineToClient("alive");
        lastHeartbeatSent = now;
      }

      if (now - lastHeartbeatReceived > hbTimeout) {
        client.stop();
        actingAsClient = false;
        netState = DISCONNECTED;
        lastRetry = now;
        updateLastEvent("HB_TIMEOUT");
        break;
      }

      while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();

        if (line == "alive") {
          lastHeartbeatReceived = now;
          if (!actingAsClient) {
            sendLineToClient("alive_ack");
          }
          updateLastEvent("RX:alive");
        } else if (line == "alive_ack") {
          if (actingAsClient) lastHeartbeatReceived = now;
          updateLastEvent("RX:alive_ack");
        } else if (line == "DOWN") {
          injectRemoteDown();
          updateLastEvent("RX:DOWN");
        } else if (line == "UP") {
          injectRemoteUp();
          updateLastEvent("RX:UP");
        } else if (line == "request_tx") {
          if (getConnectionState() == FREE) sendLineToClient("ok");
          else sendLineToClient("busy");
          updateLastEvent("RX:req_tx");
        } else if (line.startsWith("mac:")) {
          updateLastEvent("RX:mac");
        } else if (line.startsWith("duration:")) {
          updateLastEvent("RX:duration");
        } else {
          updateLastEvent("RX:unknown");
        }
      }
    } break;

    case AP_MODE: {
      yield();
      WiFiClient newClient = server.accept();
      if (newClient && !client.connected()) {
        client = newClient;
        String myMac = WiFi.macAddress();
        client.print("mac:" + myMac + "\n");
        actingAsClient = false;
        lastHeartbeatReceived = now;
        lastHeartbeatSent = now;
        safe_strncpy(peerIPbuf, client.remoteIP().toString().c_str(), sizeof(peerIPbuf));
        updateLastEvent("ACCEPT");
      } else if (newClient && client.connected()) {
        newClient.stop();
      }

      if (client.connected()) {
        unsigned long hbTimeoutAP = (getConnectionState() == FREE) ? HEARTBEAT_TIMEOUT_IDLE_MS : HEARTBEAT_TIMEOUT_ACTIVE_MS;
        if (now - lastHeartbeatReceived > hbTimeoutAP) {
          client.stop();
          netState = DISCONNECTED;
          lastRetry = now;
          updateLastEvent("AP_HB_TIMEOUT");
        }

        while (client.available()) {
          String line = client.readStringUntil('\n');
          line.trim();
          if (line == "alive") {
            lastHeartbeatReceived = now;
            sendLineToClient("alive_ack");
            updateLastEvent("AP_RX:alive");
          } else if (line == "DOWN") {
            injectRemoteDown();
            updateLastEvent("AP_RX:DOWN");
          } else if (line == "UP") {
            injectRemoteUp();
            updateLastEvent("AP_RX:UP");
          } else if (line == "request_tx") {
            if (getConnectionState() == FREE) sendLineToClient("ok");
            else sendLineToClient("busy");
            updateLastEvent("AP_RX:req_tx");
          }
        }
      }

      if (now - lastRetry > retryDelay) {
        if (WiFi.softAPgetStationNum() == 0) {
          int n2 = WiFi.scanNetworks(false, true);
          bool foundOther = false;
          int otherCh = 1;
          if (n2 > 0) {
            for (int i = 0; i < n2; ++i) {
              if (strcmp(WiFi.SSID(i).c_str(), SSID) == 0) {
                foundOther = true;
                otherCh = WiFi.channel(i);
                break;
              }
            }
          }
          WiFi.scanDelete();
          if (foundOther) {
            WiFi.begin(SSID, PASS, otherCh);
            netState = CONNECTING;
            connectStart = now;
            lastRetry = now;
            updateLastEvent("AP_DETECTED_JOIN");
          } else {
            lastRetry = now;
            retryDelay = min<unsigned long>(retryDelay + 5000, 60000);
            updateLastEvent("AP_BACKOFF");
          }
        } else {
          lastRetry = now;
          retryDelay = min<unsigned long>(retryDelay + 5000, 60000);
          updateLastEvent("AP_STAY");
        }
      }
    } break;

    case DISCONNECTED: {
      if (now - lastRetry > retryDelay) {
        WiFi.begin(SSID, PASS);
        netState = CONNECTING;
        connectStart = now;
        lastRetry = now;
        retryDelay = min<unsigned long>(retryDelay + 5000, 60000);
        updateLastEvent("RETRY_JOIN");
      }
    } break;

    default:
      netState = SCANNING;
      updateLastEvent("RESET_SCAN");
      break;
  } // switch
} // updateNetwork

// helpers
bool occupyNetwork() {
  return isConnected();
}

bool isConnected() {
  return (netState == CONNECTED || (netState == AP_MODE && client.connected()));
}

const char* getNetworkRole() {
  if (netState == AP_MODE) return "AP";
  if (netState == CONNECTED && actingAsClient) return "CLIENT";
  if (netState == CONNECTED && !actingAsClient) return "PEER";
  return "NONE";
}

const char* getPeerIP() {
  return peerIPbuf;
}

const char* getLastNetworkEvent() {
  return lastNetEventBuf;
}

unsigned long getLastNetworkEventAt() {
  return lastNetEventAt;
}

const char* getNetworkStrength() {
  static char strength[8];
  if ((netState == CONNECTED || netState == CONNECTING) && WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int percent = constrain(map(rssi, -100, -50, 0, 100), 0, 100);
    snprintf(strength, sizeof(strength), "%3d%%", percent);
    return strength;
  } else if (netState == AP_MODE && WiFi.softAPgetStationNum() > 0) {
    strcpy(strength, "100%");
    return strength;
  } else {
    strcpy(strength, "OFF");
    return strength;
  }
}