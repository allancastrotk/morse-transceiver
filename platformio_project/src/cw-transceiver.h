#ifndef CW_TRANSCEIVER_H
#define CW_TRANSCEIVER_H

#include <Arduino.h>

enum InputSource { LOCAL_INPUT, REMOTE };
enum ConnectionState { FREE, TX, RX };
enum Mode { DIDACTIC, MORSE };

// pins / thresholds
#define LOCAL_PIN D5
#define REMOTE_PIN D6
#define BUZZER_PIN D8

#define DOT_MAX 150
#define DASH_MIN 150
#define DASH_MAX 400
#define LETTER_GAP_MS 400
#define MODE_HOLD_MS 3000
#define DEBOUNCE_MS 50

// activity timeout: após este tempo sem DOWN/UP, a rede volta a FREE
#define ACTIVITY_TIMEOUT_MS 5000UL

// lifecycle
void initCWTransceiver();
void updateCWTransceiver();

// network integration (DOWN/UP model)
void injectRemoteDown();
void injectRemoteUp();

// cw -> network (to notify peer)
void sendRemoteDown();
void sendRemoteUp();

// getters used by display
const char* getCurrentSymbol();
const char* getHistoryTX();
const char* getHistoryRX();
unsigned long getHistoryVersion();
const char* getLastTranslated();
bool isModeSwitching();
ConnectionState getConnectionState();
Mode getMode();

#endif