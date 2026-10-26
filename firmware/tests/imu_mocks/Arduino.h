#pragma once
#include <cstdint>
#include <cstddef>
#include <initializer_list>
uint32_t millis();
#define log_e(...) ((void)0)
// Pin helpers used by I2CBus's bus clear; the simulated bus is always idle.
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 0x05
#define OUTPUT_OPEN_DRAIN 0x13
inline void pinMode(uint8_t,uint8_t){}
inline void digitalWrite(uint8_t,uint8_t){}
inline int digitalRead(uint8_t){return HIGH;}
inline void delayMicroseconds(uint32_t){}
