#ifndef BLINKER_H
#define BLINKER_H

#include <Arduino.h>

void initBlinker(); // Configura LED e mensagem Morse inicial

void setBlinkerMessage(const char* newMessage); // Define mensagem Morse

void updateBlinker(); // Atualiza piscar do LED

#endif
