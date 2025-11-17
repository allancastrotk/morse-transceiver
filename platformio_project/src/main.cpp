// File:        main.cpp v1.5
// Project:     Morse Transceiver
// Description: Main integration entry for Morse Transceiver. Wires modules: morse-key, telegrapher,
//              morse-telecom, network-state, history, display-adapter, buzzer-driver, translator,
//              network-connect, blinker. Non-blocking cooperative loop.
// Last modification: TX immediate display/history, conditional network send, blinker set to LED_BUILTIN.
// Modified:    2025-11-16
// Created:     2025-11-15%
//
// License:     MIT License

#include <Arduino.h>

#include "morse-key.h"
#include "telegrapher.h"
#include "morse-telecom.h"
#include "network-state.h"
#include "history.h"
#include "display-adapter.h"
#include "buzzer-driver.h"
#include "translator.h"
#include "network-connect.h"
#include "blinker.h"

// ====== LOG FLAGS ======
#define LOG_MAIN_INFO    1
#define LOG_MAIN_ACTION  1
#define LOG_MAIN_NERD    0

// -----------------------------------------------------------------------------
// Configuration (hardware)
// -----------------------------------------------------------------------------
#define KEY_PIN    D5
#define BUZZER_PIN 12

// -----------------------------------------------------------------------------
// Module enable flags
// -----------------------------------------------------------------------------
#define ENABLE_TRANSLATOR     1
#define ENABLE_DISPLAY        1
#define ENABLE_BUTTON         1
#define ENABLE_BUZZER         0
#define ENABLE_BLINKER        1
#define ENABLE_HISTORY        1
#define ENABLE_MORSE_TELECOM  1
#define ENABLE_ENCODER        0
#define ENABLE_DECODER        0
#define ENABLE_NETWORK_CONN   1
#define ENABLE_NETWORK_TX     1
#define ENABLE_NETWORK_RX     1
#define ENABLE_NETWORK_STATE  1

// -----------------------------------------------------------------------------
// Symbol accumulation for translator
// -----------------------------------------------------------------------------
static char symBuf[64];
static size_t symPos = 0;

static void pushSymToBuf(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  if (symPos + 2 < sizeof(symBuf)) {
    symBuf[symPos++] = sym;
    symBuf[symPos++] = ' ';
    symBuf[symPos]   = '\0';
  }
}

static void clearSymBuf(void) {
  symPos = 0;
  symBuf[0] = '\0';
}

// -----------------------------------------------------------------------------
// Adapter / wiring callbacks
// -----------------------------------------------------------------------------
static void onTelegrapherLocalSymbol(char sym, unsigned long dur_ms) {
  pushSymToBuf(sym);

#if ENABLE_TRANSLATOR
  if (!translator_isDidatic()) {
#if ENABLE_HISTORY
    history_pushTXSymbol(sym);   // ✅ TX imediato
#endif
#if ENABLE_DISPLAY
    displayAdapter_showSymbol(sym);
    displayAdapter_forceRedraw();
#endif
  }
#endif

#if ENABLE_NETWORK_STATE
  ns_requestLocalSymbol(sym, dur_ms);
#endif
#if ENABLE_NETWORK_TX && ENABLE_MORSE_TELECOM
  if (nc_isConnected()) {
    morse_telecom_sendSymbol(sym, dur_ms);
  }
#endif
#if ENABLE_BUZZER
  buzzer_driver_playClick();
#endif
}

static void onTelegrapherLocalDown() {
#if ENABLE_NETWORK_STATE
  ns_requestLocalDown();
#endif
#if ENABLE_NETWORK_TX && ENABLE_MORSE_TELECOM
  if (nc_isConnected()) {
    morse_telecom_sendDown();
  }
#endif
#if ENABLE_BUZZER
  buzzer_driver_playClick();
#endif
#if ENABLE_DISPLAY
  displayAdapter_forceRedraw();
#endif
}

static void onTelegrapherLocalUp() {
#if ENABLE_NETWORK_STATE
  ns_requestLocalUp();
#endif
#if ENABLE_NETWORK_TX && ENABLE_MORSE_TELECOM
  if (nc_isConnected()) {
    morse_telecom_sendUp();
  }
#endif
#if ENABLE_BUZZER
  buzzer_driver_playClick();
#endif
}

// Finalize callback
static void onTelegrapherFinalize() {
  if (symPos == 0) return;
  if (symPos > 0) symBuf[symPos - 1] = '\0';

#if ENABLE_TRANSLATOR
  char ascii[16] = {0};
  size_t written = translator_morseWordToAscii(symBuf, ascii, sizeof(ascii));

  if (written > 0) {
#if LOG_MAIN_ACTION
    Serial.printf("%lu - main - [ACTION] letter -> \"%s\" (morse \"%s\")\n", millis(), ascii, symBuf);
#endif
#if ENABLE_HISTORY
    history_pushTXLetter(ascii[0]);
#endif
#if ENABLE_DISPLAY
    displayAdapter_showLetter(ascii);
    displayAdapter_forceRedraw();
#endif
  }
#endif

  clearSymBuf();
}

// Long-press callback: alterna modo + overlay
static void onTelegrapherLongPress() {
#if ENABLE_TRANSLATOR
  bool toMorse = translator_isDidatic();
  if (toMorse) translator_setModeMorse();
  else translator_setModeDidatic();

#if LOG_MAIN_ACTION
  Serial.printf("%lu - main - [ACTION] translator mode toggled -> %s\n",
                millis(), translator_isDidatic() ? "DIDATIC" : "MORSE");
#endif

#if ENABLE_DISPLAY
  if (translator_isDidatic()) {
    displayAdapter_showModeMessage("DIDATIC", "MODE");
  } else {
    displayAdapter_showModeMessage("MORSE", "MODE");
  }
  displayAdapter_forceRedraw();
#endif
#endif
}

// Remote telegrapher -> RX only
static void onTelegrapherRemoteSymbol(char sym, unsigned long dur_ms) {
#if ENABLE_HISTORY
  history_pushRXSymbol(sym);
#endif
#if ENABLE_NETWORK_STATE
  ns_notifyRemoteSymbol(sym, dur_ms);
#endif
#if ENABLE_BUZZER
  buzzer_driver_playAck();
#endif
#if ENABLE_DISPLAY
  displayAdapter_forceRedraw();
#endif
}

static void onTelegrapherRemoteDown() {
#if ENABLE_NETWORK_STATE
  ns_notifyRemoteDown();
#endif
#if ENABLE_BUZZER
  buzzer_driver_onStateChange(RX);
#endif
#if ENABLE_DISPLAY
  displayAdapter_forceRedraw();
#endif
}

static void onTelegrapherRemoteUp() {
#if ENABLE_NETWORK_STATE
  ns_notifyRemoteUp();
#endif
#if ENABLE_BUZZER
  buzzer_driver_onStateChange(FREE);
#endif
#if ENABLE_DISPLAY
  displayAdapter_forceRedraw();
#endif
}

// Debug callback
static void mk_dbg_cb(bool down, unsigned long t_us) {
  (void)down; (void)t_us;
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);

  Serial.println();
  Serial.println("boot: Morse Transceiver");
  Serial.println("boot: Serial started 115200");
  Serial.print("boot: KEY_PIN = "); Serial.println((int)KEY_PIN);

#if ENABLE_HISTORY
  history_init();
#endif
#if ENABLE_TRANSLATOR
  translator_init();
#endif

  telegrapher_init();

#if ENABLE_DISPLAY
  displayAdapter_init();
#endif
#if ENABLE_BUZZER
  buzzer_driver_init(BUZZER_PIN);
#endif
#if ENABLE_BUTTON
  morse_key_init(KEY_PIN, true);
  morse_key_setDebugCallback(mk_dbg_cb);
#endif

  telegrapher_onLocalSymbol(onTelegrapherLocalSymbol);
  telegrapher_onLocalDown(onTelegrapherLocalDown);
  telegrapher_onLocalUp(onTelegrapherLocalUp);
  telegrapher_onFinalize(onTelegrapherFinalize);
  telegrapher_onLongPress(onTelegrapherLongPress);
  telegrapher_onRemoteSymbol(onTelegrapherRemoteSymbol);
  telegrapher_onRemoteDown(onTelegrapherRemoteDown);
  telegrapher_onRemoteUp(onTelegrapherRemoteUp);

#if ENABLE_DISPLAY
  displayAdapter_showSplash("Morse", "Booting...", 3000);
  displayAdapter_forceRedraw();
#endif

  clearSymBuf();

#if ENABLE_BLINKER
  // ✅ Blinker agora usa LED principal (GPIO2)
  initBlinker(LED_BUILTIN, "SEMPRE ALERTA");
#endif
}

// -----------------------------------------------------------------------------
// Cooperative main loop
// -----------------------------------------------------------------------------
void loop() {
#if ENABLE_BUTTON
  morse_key_process();
#endif

  telegrapher_update();
  updateNetworkState();
  displayAdapter_update();

#if ENABLE_DISPLAY
  displayAdapter_update();
#endif

#if ENABLE_BLINKER
  updateBlinker();
#endif

  yield();
}