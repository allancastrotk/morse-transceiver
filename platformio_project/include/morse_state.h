#ifndef MORSE_STATE_H
#define MORSE_STATE_H

// Protótipos usados por display.cpp
const char* getLastTranslated();
bool isModeSwitching();

// Funções auxiliares (opcionais)
void setLastTranslated(const char* s);
void setModeSwitching(bool v);

#endif // MORSE_STATE_H
