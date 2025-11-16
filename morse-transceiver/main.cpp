// File: main.cpp
// Description: Main integration for Morse transceiver project (PlatformIO friendly, no .ino)
// - Wires morse-key, telegrapher, morse-telecom, network-state, history, display-adapter, buzzer-driver and translator
// - Non-blocking loop calling each module's update
// Created: 2025-11-15
// Corrigido: nomes de inicialização/atualização de network e pequenas inconsistências

#include <Arduino.h>
#include "morse-key.h"
#include "telegrapher.h"
#include "morse-telecom.h"
#include "network-state.h"
#include "history.h"
#include "display-adapter.h"
#include "buzzer-driver.h"
#include "translator.h"
#include "network-connect.h" // para init/updateNetworkConnect

// Configuration
#define KEY_PIN        2
#define BUZZER_PIN     12

// Forward local callbacks (thin adapters)
static void onTelegrapherLocalSymbol(char sym, unsigned long dur_ms) {
  // push to history and request network send via network-state / morse-telecom
  history_pushTXSymbol(sym);
  // Request network-state to handle transmission; network-state should call morse_telecom_sendSymbol or enqueue
  ns_requestLocalSymbol(sym, dur_ms);
  // Also send via telecom immediately (simple policy)
  morse_telecom_sendSymbol(sym, dur_ms);
  // audible click feedback
  buzzer_driver_playClick();
}

static void onTelegrapherLocalDown() {
  history_pushTXSymbol('>'); // marker for down (optional)
  ns_requestLocalDown();
  morse_telecom_sendDown();
  buzzer_driver_playClick();
}

static void onTelegrapherLocalUp() {
  ns_requestLocalUp();
  morse_telecom_sendUp();
  buzzer_driver_playClick();
}

static void onTelegrapherModeToggle() {
  // Let display redraw to show mode
  displayAdapter_forceRedraw();
}

static void onTelegrapherRemoteSymbol(char sym, unsigned long dur_ms) {
  history_pushRXSymbol(sym);
  ns_notifyRemoteSymbol(sym, dur_ms);
  // small ack sound for remote symbol
  buzzer_driver_playAck();
}

static void onTelegrapherRemoteDown() {
  ns_notifyRemoteDown();
  buzzer_driver_onStateChange(RX);
}

static void onTelegrapherRemoteUp() {
  ns_notifyRemoteUp();
  buzzer_driver_onStateChange(FREE);
}

// Network-state callbacks to UI and buzzer
static void onNsStateChange(ConnectionState st) {
  // update display and play sound via buzzer-driver
  displayAdapter_forceRedraw();
  buzzer_driver_onStateChange(st);
}

// morse-telecom incoming wiring -> telegrapher
static void mt_remoteDown_cb() { telegrapher_handleRemoteDown(); }
static void mt_remoteUp_cb()   { telegrapher_handleRemoteUp(); }
static void mt_remoteSymbol_cb(char sym, unsigned long dur_ms) { telegrapher_handleRemoteSymbol(sym, dur_ms); }

// Optional debug callback from morse-key (ISR-safe tiny)
static void mk_dbg_cb(bool down, unsigned long t_us) {
  // Keep very small: just a serial dot for debug (avoid heavy work in ISR)
  // Serial.println("K"); // avoid in ISR for high freq
  (void)down; (void)t_us;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // Init modules (order: low-level first)
  history_init();
  translator_init();
  morse_telecom_init();

  // Network connect and network state initialization (nomes corretos)
  initNetworkConnect();   // inicializa o gerenciador de conexão TCP/WiFi
  initNetworkState();     // inicializa a máquina de estado TX/RX/FREE

  telegrapher_init();
  displayAdapter_init();
  buzzer_driver_init(BUZZER_PIN);

  // Key init after telegrapher so ISR events are safely queued
  morse_key_init(KEY_PIN, true);
  morse_key_setDebugCallback(mk_dbg_cb);

  // Register callbacks
  telegrapher_onLocalSymbol(onTelegrapherLocalSymbol);
  telegrapher_onLocalDown(onTelegrapherLocalDown);
  telegrapher_onLocalUp(onTelegrapherLocalUp);
  telegrapher_onModeToggle(onTelegrapherModeToggle);

  telegrapher_onRemoteSymbol(onTelegrapherRemoteSymbol);
  telegrapher_onRemoteDown(onTelegrapherRemoteDown);
  telegrapher_onRemoteUp(onTelegrapherRemoteUp);

  morse_telecom_onRemoteDown(mt_remoteDown_cb);
  morse_telecom_onRemoteUp(mt_remoteUp_cb);
  morse_telecom_onRemoteSymbol(mt_remoteSymbol_cb);

  ns_onStateChange(onNsStateChange);

  // Initial UI splash
  displayAdapter_showSplash("Morse", "Booting...", 1200);

  // Force first redraw after boot
  displayAdapter_forceRedraw();
}

void loop() {
  // Main cooperative loop
  // Order chosen to prioritize network processing and UI responsiveness
  // 1) network I/O (assumes functions exist)
  updateNetworkConnect();    // process incoming network data, will call morse_telecom_handleIncomingLine when lines arrive
  morse_telecom_update();    // flush any telecom local queue to network
  telegrapher_update();      // process key events & remote event forwarding
  updateNetworkState();      // state machine for TX/RX arbitration, timeouts
  displayAdapter_update();   // redraw when history or state changed
  buzzer_driver_update();    // drive buzzer non-blocking

  // small yield to allow WiFi/OS background tasks on some platforms
  delay(1);
}