#include "network.h"
#include "cw-transceiver.h"  // Para captureInput(REMOTE, duration)

static ESP8266WiFiMulti wifiMulti;  // Não usado; mantido para compatibilidade
static WiFiServer server(5000);
static WiFiClient client;
static const char* SSID = "morse-transceiver";
static const char* PASS = "";  // Sem senha para simplicidade
static const IPAddress AP_IP(192, 168, 4, 1);
static const unsigned long SCAN_TIMEOUT = 5000;  // Timeout per scan attempt
static const unsigned long SCAN_INTERVAL = 500;  // Intervalo para verificar scan
static const unsigned long CONNECT_TIMEOUT = 5000;  // 5s for connect
static const unsigned long RETRY_INTERVAL_BASE = 10000;  // Retry STA base 10s
static const unsigned long STATUS_CHECK_INTERVAL = 5000;  // Check WiFi.status() a cada 5s
static const unsigned long HEARTBEAT_INTERVAL = 1000;  // Send "alive" every 1s when connected
static const unsigned long HEARTBEAT_TIMEOUT = 3000;  // Timeout if no heartbeat
static unsigned long lastHeartbeatSent = 0;
static unsigned long lastHeartbeatReceived = 0;
static unsigned long lastStatusCheck = 0;
static unsigned long lastScan = 0;
static unsigned long connectStart = 0;
static unsigned long lastRetry = 0;
static unsigned long retryDelay = RETRY_INTERVAL_BASE;  // Backoff inicia 10s
static int scanAttempts = 0;
static bool scanInProgress = false;
static bool scanPollingPrinted = false;  // To avoid repetition
static int lastScanResult = -2;  // Último resultado de scanComplete para evitar prints repetidos
static int asyncFailCount = 0;  // Contador de falhas de scan assíncrono
NetworkState netState = SCANNING;  // Definido como extern no header

void initNetwork() {
  unsigned long now = millis();
  randomSeed(analogRead(0));  // Seed for random
  delay(random(0, 2000));  // Random delay to desincronizar starts
  Serial.print(now);
  Serial.print(" - Random delay de ");
  Serial.print(random(0, 2000));
  Serial.println(" ms para desincronizar");
  WiFi.mode(WIFI_STA);  // Inicia como STA for scan/conexão during splash
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);  // For stability
  WiFi.scanNetworks(true, true);  // Async scan, mostrar ocultas
  scanInProgress = true;
  scanPollingPrinted = false;
  lastScan = now;
  scanAttempts = 1;
  asyncFailCount = 0;
  Serial.print(now);
  Serial.println(" - Iniciando busca async por SSID: morse-transceiver (STA primeiro during splash)");
}

// Atualização não bloqueante
void updateNetwork() {
  unsigned long now = millis();
  WiFiClient newClient;  // Evita cruzamento em switch
  // Check WiFi.status() periodicamente
  if (now - lastStatusCheck > STATUS_CHECK_INTERVAL) {
    Serial.print(now);
    Serial.print(" - WiFi status: ");
    uint8_t status = WiFi.status();
    Serial.print(status);
    Serial.print(" (");
    switch (status) {
      case WL_IDLE_STATUS: Serial.print("IDLE"); break;
      case WL_NO_SSID_AVAIL: Serial.print("NO SSID"); break;
      case WL_SCAN_COMPLETED: Serial.print("SCAN COMPLETED"); break;
      case WL_CONNECTED: Serial.print("CONNECTED"); break;
      case WL_CONNECT_FAILED: Serial.print("CONNECT FAILED"); break;
      case WL_CONNECTION_LOST: Serial.print("CONNECTION LOST"); break;
      case WL_DISCONNECTED: Serial.print("DISCONNECTED"); break;
      default: Serial.print("UNKNOWN"); break;
    }
    Serial.println(")");
    lastStatusCheck = now;
  }

  switch (netState) {
    case SCANNING: {
      if (now - lastScan < SCAN_INTERVAL) break;  // Verifica a cada 500ms
      int n = WiFi.scanComplete();
      if (n != lastScanResult) {
        Serial.print(now);
        Serial.print(" - scanComplete returned: ");
        Serial.println(n);
        lastScanResult = n;
      }
      if (n == WIFI_SCAN_RUNNING) {
        if (!scanPollingPrinted) {
          Serial.print(now);
          Serial.println(" - Scan em andamento...");
          scanPollingPrinted = true;
        }
        if (now - lastScan > SCAN_TIMEOUT) {
          Serial.print(now);
          Serial.println(" - Scan timeout; assumindo falha, incrementando tentativa");
          WiFi.scanDelete();  // Limpa scan travado
          scanInProgress = false;
          scanPollingPrinted = false;
          lastScanResult = -2;
          scanAttempts++;
          asyncFailCount++;
        }
      } else if (n == WIFI_SCAN_FAILED) {
        Serial.print(now);
        Serial.println(" - Scan falhou; tentando novamente");
        asyncFailCount++;
        WiFi.scanDelete();
        scanInProgress = false;
        scanPollingPrinted = false;
        lastScanResult = -2;
        scanAttempts++;
      } else if (n >= 0 && scanAttempts <= 3) {
        scanInProgress = false;
        scanPollingPrinted = false;
        lastScanResult = -2;
        Serial.print(now);
        Serial.print(" - Redes encontradas na tentativa ");
        Serial.print(scanAttempts);
        Serial.println(":");
        bool found = false;
        int targetChannel = 1;  // Default channel
        String targetBSSID = "";  // Para eleição
        int bestRSSI = -200;  // Para escolher melhor
        int numSameSSID = 0;
        for (int i = 0; i < n; ++i) {
          Serial.print(now);
          Serial.print(" - SSID: ");
          Serial.print(WiFi.SSID(i));
          Serial.print(", RSSI: ");
          Serial.print(WiFi.RSSI(i));
          Serial.print(" dBm, Canal: ");
          Serial.print(WiFi.channel(i));
          Serial.print(", Encryption: ");
          Serial.print(WiFi.encryptionType(i));
          Serial.print(", BSSID: ");
          Serial.println(WiFi.BSSIDstr(i));
          if (strcmp(WiFi.SSID(i).c_str(), SSID) == 0) {
            numSameSSID++;
            if (WiFi.RSSI(i) > bestRSSI) {
              bestRSSI = WiFi.RSSI(i);
              targetChannel = WiFi.channel(i);
              targetBSSID = WiFi.BSSIDstr(i);
            }
          }
        }
        WiFi.scanDelete();  // Limpa resultados após processar
        if (numSameSSID > 1) {
          Serial.print(now);
          Serial.println(" - Múltiplos APs com mesmo SSID; iniciando negociação");
          WiFi.begin(SSID, PASS, targetChannel);  // Conecta ao melhor canal
          netState = CONNECTING;
          connectStart = now;
        } else if (numSameSSID == 1) {
          found = true;
          Serial.print(now);
          Serial.println(" - SSID alvo encontrado, iniciando conexão como STA");
          WiFi.begin(SSID, PASS, targetChannel);
          netState = CONNECTING;
          connectStart = now;
          WiFi.printDiag(Serial);  // Diagnóstico: modo, status, etc.
        } else {
          Serial.print(now);
          Serial.print(" - Tentativa ");
          Serial.print(scanAttempts);
          Serial.println(" sem SSID alvo; proximo scan");
          WiFi.scanNetworks(true);
          scanInProgress = true;
          scanPollingPrinted = false;
          lastScan = now;
          scanAttempts++;
        }
      }
      if (scanAttempts > 3 && !scanInProgress) {
        Serial.print(now);
        Serial.println(" - Nenhum SSID alvo encontrado após tentativas ou falhas; iniciando AP e mantendo STA retry (dual mode)");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(SSID, PASS, 1);  // Inicia AP no canal 1
        Serial.print(now);
        Serial.print(" - AP iniciado com SSID: ");
        Serial.print(SSID);
        Serial.print(", IP: ");
        Serial.print(WiFi.softAPIP());
        Serial.print(", MAC: ");
        Serial.print(WiFi.softAPmacAddress());
        Serial.print(", Clientes conectados: ");
        Serial.println(WiFi.softAPgetStationNum());
        WiFi.printDiag(Serial);  // Diagnóstico AP+STA
        server.begin();
        netState = AP_MODE;
        lastRetry = now;
        asyncFailCount = 0;
        // Tentar scan síncrono como fallback
        Serial.print(now);
        Serial.println(" - Tentando scan síncrono como fallback");
        int n = WiFi.scanNetworks(false, true);  // Scan síncrono
        Serial.print(now);
        Serial.print(" - Scan síncrono retornou: ");
        Serial.println(n);
        if (n > 0) {
          for (int i = 0; i < n; ++i) {
            Serial.print(now);
            Serial.print(" - SSID: ");
            Serial.print(WiFi.SSID(i));
            Serial.print(", RSSI: ");
            Serial.print(WiFi.RSSI(i));
            Serial.print(" dBm, Canal: ");
            Serial.print(WiFi.channel(i));
            Serial.print(", Encryption: ");
            Serial.print(WiFi.encryptionType(i));
            Serial.print(", BSSID: ");
            Serial.println(WiFi.BSSIDstr(i));
            if (strcmp(WiFi.SSID(i).c_str(), SSID) == 0) {
              Serial.print(now);
              Serial.println(" - SSID alvo encontrado em scan síncrono, iniciando conexão");
              WiFi.begin(SSID, PASS, WiFi.channel(i));
              netState = CONNECTING;
              connectStart = now;
              break;
            }
          }
          WiFi.scanDelete();
        }
      } else if (!scanInProgress) {
        // Iniciar próximo scan
        WiFi.scanNetworks(true, true);
        Serial.print(now);
        Serial.println(" - Iniciando novo scan assíncrono");
        scanInProgress = true;
        scanPollingPrinted = false;
        lastScan = now;
        lastScanResult = -2;
      }
      break;
    }
    case CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print(now);
        Serial.println(" - Conectado como STA; conectando TCP ao servidor");
        WiFi.printDiag(Serial);  // Diagnóstico STA
        if (client.connect(AP_IP, 5000)) {
          netState = CONNECTED;
          Serial.print(now);
          Serial.println(" - TCP cliente conectado");
          // Iniciar negotiation if needed (ex.: send MAC)
        } else {
          Serial.print(now);
          Serial.println(" - Falha TCP cliente (verifique se AP está ativo no peer); retry em 5s");
          connectStart = now;
          WiFi.printDiag(Serial);  // Diagnóstico falha
        }
      } else if (now - connectStart > CONNECT_TIMEOUT) {
        Serial.print(now);
        Serial.println(" - Timeout conexão STA; indo para DISCONNECTED");
        netState = DISCONNECTED;
        lastRetry = now;
        WiFi.printDiag(Serial);  // Diagnóstico falha
      }
      break;
    case CONNECTED:
      if (!client.connected()) {
        netState = DISCONNECTED;
        Serial.print(now);
        Serial.println(" - TCP desconectado; indo para DISCONNECTED");
        lastRetry = now;
      } else {
        // Heartbeat
        if (now - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
          client.print("alive\n");
          client.flush();
          lastHeartbeatSent = now;
          Serial.print(now);
          Serial.println(" - Enviado heartbeat 'alive'");
        }
        if (now - lastHeartbeatReceived > HEARTBEAT_TIMEOUT) {
          Serial.print(now);
          Serial.println(" - Heartbeat timeout; indo para DISCONNECTED");
          client.stop();
          netState = DISCONNECTED;
          lastRetry = now;
        }
        // Receber pacotes
        while (client.available()) {
          String line = client.readStringUntil('\n');
          line.trim();
          if (line == "alive") {
            lastHeartbeatReceived = now;
            Serial.print(now);
            Serial.println(" - Recebido heartbeat 'alive'");
          } else if (line.startsWith("duration:")) {
            unsigned long dur = line.substring(9).toInt();
            if (dur >= 25) {
              Serial.print(now);
              Serial.print(" - Recebido duration remoto: ");
              Serial.println(dur);
              captureInput(REMOTE, dur);
            }
          } else if (line == "request_tx") {
            if (getConnectionState() == FREE) {
              client.print("ok\n");
              client.flush();
              Serial.print(now);
              Serial.println(" - Enviado 'ok' para request_tx");
            } else {
              client.print("busy\n");
              client.flush();
              Serial.print(now);
              Serial.println(" - Enviado 'busy' para request_tx");
            }
          } else if (line.startsWith("mac:")) {
            String remoteMac = line.substring(4);
            String myMac = WiFi.macAddress();
            if (myMac > remoteMac && WiFi.getMode() == WIFI_AP_STA) {
              Serial.print(now);
              Serial.println(" - Meu MAC é maior; revertendo para STA");
              WiFi.softAPdisconnect(true);
              WiFi.mode(WIFI_STA);
              WiFi.begin(SSID, PASS);
              netState = CONNECTING;
              connectStart = now;
            } else {
              Serial.print(now);
              Serial.println(" - Meu MAC é menor; permanecendo como AP");
            }
          }
        }
      }
      break;
    case AP_MODE:
      newClient = server.available();
      if (newClient && !client.connected()) {
        client = newClient;
        Serial.print(now);
        Serial.println(" - Cliente TCP conectado ao AP");
        Serial.print(now);
        Serial.print(" - Clientes conectados: ");
        Serial.println(WiFi.softAPgetStationNum());
        // Enviar MAC para negociação
        String myMac = WiFi.macAddress();
        client.print("mac:" + myMac + "\n");
        client.flush();
        Serial.print(now);
        Serial.print(" - Enviado MAC para negociação: ");
        Serial.println(myMac);
      }
      if (client.connected()) {
        // Heartbeat
        if (now - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
          client.print("alive\n");
          client.flush();
          lastHeartbeatSent = now;
          Serial.print(now);
          Serial.println(" - Enviado heartbeat 'alive'");
        }
        if (now - lastHeartbeatReceived > HEARTBEAT_TIMEOUT) {
          Serial.print(now);
          Serial.println(" - Heartbeat timeout; indo para DISCONNECTED");
          client.stop();
          netState = DISCONNECTED;
          lastRetry = now;
        }
        // Receber pacotes
        while (client.available()) {
          String line = client.readStringUntil('\n');
          line.trim();
          if (line == "alive") {
            lastHeartbeatReceived = now;
            Serial.print(now);
            Serial.println(" - Recebido heartbeat 'alive'");
          } else if (line.startsWith("duration:")) {
            unsigned long dur = line.substring(9).toInt();
            if (dur >= 25) {
              Serial.print(now);
              Serial.print(" - Recebido duration remoto (AP): ");
              Serial.println(dur);
              captureInput(REMOTE, dur);
            }
          } else if (line == "request_tx") {
            if (getConnectionState() == FREE) {
              client.print("ok\n");
              client.flush();
              Serial.print(now);
              Serial.println(" - Enviado 'ok' para request_tx");
            } else {
              client.print("busy\n");
              client.flush();
              Serial.print(now);
              Serial.println(" - Enviado 'busy' para request_tx");
            }
          } else if (line.startsWith("mac:")) {
            String remoteMac = line.substring(4);
            String myMac = WiFi.macAddress();
            if (myMac > remoteMac) {
              Serial.print(now);
              Serial.println(" - Meu MAC é maior; revertendo para STA");
              WiFi.softAPdisconnect(true);
              WiFi.mode(WIFI_STA);
              WiFi.begin(SSID, PASS);
              netState = CONNECTING;
              connectStart = now;
            } else {
              Serial.print(now);
              Serial.println(" - Meu MAC é menor; permanecendo como AP");
            }
          }
        }
      }
      // Tentar reconexão como STA em dual mode
      if (now - lastRetry > retryDelay) {
        Serial.print(now);
        Serial.println(" - Tentando reconexão STA em AP_MODE");
        WiFi.begin(SSID, PASS);
        netState = CONNECTING;
        connectStart = now;
        lastRetry = now;
        retryDelay += 5000;
      }
      break;
    case DISCONNECTED:
      if (now - lastRetry > retryDelay) {
        Serial.print(now);
        Serial.println(" - Retry conexão STA em DISCONNECTED");
        netState = CONNECTING;
        connectStart = now;
        lastRetry = now;
        retryDelay += 5000;
        WiFi.begin(SSID, PASS);
      }
      break;
  }
}

bool occupyNetwork() {
  return isConnected();
}

bool isConnected() {
  return (netState == CONNECTED || (netState == AP_MODE && client.connected()));
}

void sendDuration(unsigned long duration) {
  unsigned long now = millis();
  if (isConnected() && client.connected()) {
    client.print("duration:");
    client.print(duration);
    client.print("\n");
    client.flush();
    Serial.print(now);
    Serial.print(" - Enviado duration local: ");
    Serial.println(duration);
  }
}

const char* getNetworkStrength() {
  static char strength[5];  // "100%", " OFF"
  if (netState == CONNECTED && WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    int percent = constrain(map(rssi, -100, -50, 0, 100), 0, 100);
    sprintf(strength, "%3d%%", percent);
    return strength;
  } else {
    strcpy(strength, " OFF");
    return strength;
  }
}
