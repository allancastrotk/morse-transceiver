#include <Arduino.h>
#include "cw-transceiver.h"
#include "display.h"
#include "blinker.h"
#include "network.h"

// Configura inicialização do sistema
void setup() {
  Serial.begin(115200); // Inicia comunicação serial (115200 baud)
  while (!Serial) { } // Aguarda serial pronta
  for (int i = 0; i < 100 && Serial.available(); i++) Serial.read(); // Descarta dados residuais
  initNetwork();      // Inicializa Wi-Fi async (scans durante splash)
  initDisplay();      // Inicializa display OLED (delay 3s para splash)
  initCWTransceiver(); // Configura botão e buzzer
  initBlinker();      // Configura LED para Morse
}

// Executa loop principal
void loop() {
  static unsigned long lastButton = 0, lastDisplay = 0, lastBlinker = 0, lastNetwork = 0; // Temporização de atualizações
  unsigned long now = millis(); // Tempo atual
  if (now - lastButton >= 5) { updateCWTransceiver(); lastButton = now; } // Atualiza Morse a cada 5ms para fluidez
  if (now - lastDisplay >= 500) { updateDisplay(); lastDisplay = now; } // Atualiza display a cada 500ms
  if (now - lastBlinker >= 100) { updateBlinker(); lastBlinker = now; } // Atualiza LED a cada 100ms
  if (now - lastNetwork >= 100) { updateNetwork(); lastNetwork = now; } // Atualiza rede a cada 100ms (non-blocking)
  yield(); // Permite multitarefa do ESP8266
}
