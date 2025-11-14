#ifndef CW_TRANSCEIVER_H
#define CW_TRANSCEIVER_H

#include <Arduino.h>

enum InputSource { LOCAL_INPUT, REMOTE };
enum ConnectionState { FREE, TX, RX };
enum Mode { DIDACTIC, MORSE };

#define LOCAL_PIN D5
#define REMOTE_PIN D6
#define BUZZER_PIN D8
#define DEBOUNCE_TIME 25
#define SHORT_PRESS 150
#define LONG_PRESS 400
#define LETTER_GAP 800
#define INACTIVITY_TIMEOUT 5000

void initCWTransceiver();
void updateCWTransceiver();
void captureInput(InputSource source, unsigned long duration);
void handleButtonPress(InputSource source);
void handleButtonRelease(InputSource source);
void handleInactivity();
void handleLetterGap();
char translateMorse();
void updateHistory(char letter);
ConnectionState getConnectionState();
Mode getMode();
const char* getCurrentSymbol();
const char* getHistoryTX();
const char* getHistoryRX();

#endif
