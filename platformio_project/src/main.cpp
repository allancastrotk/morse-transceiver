// File:        main.cpp v1.1
// Project:     Morse Transceiver
// Description: Main integration entry for Morse Transceiver. Wires modules: morse-key, telegrapher,
//              morse-telecom, network-state, history, display-adapter, buzzer-driver, translator,
//              network-connect, blinker. Non-blocking cooperative loop.
// Last modification: mode-aware history/display wiring; showSymbol in MORSE; ignore unknown letter;
//                    clean symbol buffer handling to avoid first-letter erratic behavior
// Modified:    2025-11-18
// Created:     2025-11-15
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
// Module enable flags (1 = enabled, 0 = disabled)
// -----------------------------------------------------------------------------
#define ENABLE_TRANSLATOR     1
#define ENABLE_DISPLAY        1
#define ENABLE_BUTTON         1
#define ENABLE_BUZZER         0
#define ENABLE_BLINKER        0

// Infrastructure / helper modules
#define ENABLE_HISTORY        1

// Morse-telecom paths
#define ENABLE_MORSE_TELECOM  0
#define ENABLE_ENCODER        0
#define ENABLE_DECODER        0

// Network features
#define ENABLE_NETWORK_CONN   0
#define ENABLE_NETWORK_TX     0
#define ENABLE_NETWORK_RX     0
#define ENABLE_NETWORK_STATE  0

// -----------------------------------------------------------------------------
// Symbol accumulation for translator (per-letter buffer, spaced symbols)
// -----------------------------------------------------------------------------
static char symBuf[64];
static size_t symPos = 0;

// Push one symbol and a space (". " or "- ")
static void pushSymToBuf(char sym) {
  if (!(sym == '.' || sym == '-')) return;
  if (symPos + 2 < sizeof(symBuf)) {
    symBuf[symPos++] = sym;
    symBuf[symPos++] = ' ';
    symBuf[symPos]   = '\0';
  }
}

// Clear buffer to clean start (avoid first-letter garbage)
static void clearSymBuf(void) {
  symPos = 0;
  symBuf[0] = '\0';
}

// -----------------------------------------------------------------------------
// Adapter / wiring callbacks
// -----------------------------------------------------------------------------
static void onTelegrapherLocalSymbol(char sym, unsigned long dur_ms) {
  // Always accumulate locally for finalize
  pushSymToBuf(sym);

  // Mode-aware history + display
#if ENABLE_TRANSLATOR
  if (!translator_isDidatic()) {
    // MORSE mode: history records only symbols; display shows symbol 1.5s
#if ENABLE_HISTORY
    history_pushTXSymbol(sym);
#endif
#if ENABLE_DISPLAY
    displayAdapter_showSymbol(sym);
    displayAdapter_forceRedraw();
#endif
  } else {
    // DIDATIC mode: não registra símbolos no history; aguarda finalize para letra
#if ENABLE_DISPLAY
    // opcional: pode mostrar o símbolo momentâneo também, mas documentação prioriza letra no finalize
#endif
  }
#else
  // Sem translator: apenas registra símbolo no history
#if ENABLE_HISTORY
  history_pushTXSymbol(sym);
#endif
#endif

#if ENABLE_NETWORK_STATE
  ns_requestLocalSymbol(sym, dur_ms);
#endif
#if ENABLE_NETWORK_TX && ENABLE_MORSE_TELECOM
  morse_telecom_sendSymbol(sym, dur_ms);
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
  morse_telecom_sendDown();
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
  morse_telecom_sendUp();
#endif
#if ENABLE_BUZZER
  buzzer_driver_playClick();
#endif
}

// Finalize callback: traduz a letra e exibe conforme modo
static void onTelegrapherFinalize() {
  if (symPos == 0) return;
  // Remover o espaço final
  if (symPos > 0) symBuf[symPos - 1] = '\0';

#if ENABLE_TRANSLATOR
  char ascii[16] = {0};
  size_t written = translator_morseWordToAscii(symBuf, ascii, sizeof(ascii));

  if (written > 0) {
#if LOG_MAIN_ACTION
    Serial.printf("%lu - main - [ACTION] letter -> \"%s\" (morse \"%s\")\n", millis(), ascii, symBuf);
#endif
    if (translator_isDidatic()) {
      // DIDATIC: history só com letras, display mostra letra por 1,5s
#if ENABLE_HISTORY
      history_pushTXLetter(ascii[0]);
#endif
#if ENABLE_DISPLAY
      displayAdapter_showLetter(ascii);
      displayAdapter_forceRedraw();
#endif
    } else {
      // MORSE: não registrar letra no history (apenas símbolos). Opcional: pode mostrar letra também.
#if ENABLE_DISPLAY
      // Se quiser ver a letra traduzida mesmo em MORSE, descomente:
      // displayAdapter_showLetter(ascii);
      // displayAdapter_forceRedraw();
#endif
    }
  } else {
#if LOG_MAIN_ACTION
    Serial.printf("%lu - main - [ACTION] invalid sequence (morse \"%s\")\n", millis(), symBuf);
#endif
    // Não empurrar '?' para o history; opcional: efeito visual
    // displayAdapter_forceRedraw(); // se houver efeito de erro
  }
#else
#if LOG_MAIN_ACTION
  Serial.printf("%lu - main - [ACTION] morse \"%s\"\n", millis(), symBuf);
#endif
#endif

  clearSymBuf();
}

// Long-press callback: alterna modo
static void onTelegrapherLongPress() {
#if ENABLE_TRANSLATOR
  if (translator_isDidatic()) translator_setModeMorse();
  else translator_setModeDidatic();
#if LOG_MAIN_ACTION
  Serial.printf("%lu - main - [ACTION] translator mode toggled -> %s\n",
                millis(), translator_isDidatic() ? "DIDATIC" : "MORSE");
#endif
#endif
#if ENABLE_DISPLAY
  displayAdapter_forceRedraw();
#endif
}

// Remote telegrapher -> local effects
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
  translator_init(); // default DIDATIC
#endif
#if (ENABLE_ENCODER || ENABLE_DECODER) && ENABLE_MORSE_TELECOM
  morse_telecom_init();
#endif
#if ENABLE_NETWORK_CONN
  initNetworkConnect();
#endif
#if ENABLE_NETWORK_STATE
  initNetworkState();
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

#if (ENABLE_NETWORK_RX || ENABLE_NETWORK_CONN) && ENABLE_MORSE_TELECOM
  morse_telecom_onRemoteDown(mt_remoteDown_cb);
  morse_telecom_onRemoteUp(mt_remoteUp_cb);
  morse_telecom_onRemoteSymbol(mt_remoteSymbol_cb);
#endif
#if ENABLE_NETWORK_STATE
  ns_onStateChange(onNsStateChange);
#endif
#if ENABLE_DISPLAY
  displayAdapter_showSplash("Morse", "Booting...", 3000);
  displayAdapter_forceRedraw();
#endif

#if ENABLE_BLINKER
  initBlinker(255, "SEMPRE ALERTA");
#endif

  clearSymBuf();
}

// -----------------------------------------------------------------------------
// Cooperative main loop (non-blocking)
// -----------------------------------------------------------------------------
void loop() {
  // Network...
#if (ENABLE_NETWORK_CONN || ENABLE_NETWORK_TX || ENABLE_NETWORK_RX || ENABLE_NETWORK_STATE)
  updateNetworkConnect();
  morse_telecom_update();
  updateNetworkState();
#endif

  // Process input queue first (drain ISR queue)
#if ENABLE_BUTTON
  morse_key_process();
#endif

  // Then classify and dispatch
  telegrapher_update();

#if ENABLE_DISPLAY
  displayAdapter_update();
#endif

#if ENABLE_BUZZER
  buzzer_driver_update();
#endif

#if ENABLE_BLINKER
  updateBlinker();
#endif

  yield();
}