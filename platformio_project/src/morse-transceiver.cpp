/*
  morse-project.ino — PowerTune Morse Transceiver
  Inicializa módulos e executa loop principal
*/

#include <Arduino.h>
#include "display.h"
#include "network.h"
#include "cw-transceiver.h"
#include "blinker.h"

void setup() {
  Serial.begin(115200);
  delay(50);

  initDisplay();
  initNetwork();
  initCWTransceiver();
  initBlinker();

  // exemplo: piscar uma mensagem curta no LED ao iniciar
  startBlinker("PT");
}

void loop() {
  // prioridades leves no loop: rede, transceptor, display, blinker
  updateNetwork();
  updateCWTransceiver();
  updateDisplay();
  updateBlinker();

  // yield para WiFi/RTOS cooperativo
  yield();
}