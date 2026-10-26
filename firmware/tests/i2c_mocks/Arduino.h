#pragma once
// Simulated open-drain I2C lines for I2CBus bus-clear tests.
#include <cstdint>
#include <cstddef>
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 0x05
#define OUTPUT_OPEN_DRAIN 0x13
namespace sim {
extern int sdaPin, sclPin;
extern bool sdaDriven, sclDriven;   // S3 pulling the line low
extern int slaveHoldClocks;         // rising SCL edges until the stuck sensor releases SDA (-1 = never)
extern bool sclStuck;
extern int clocks;
inline bool sda(){ return !sdaDriven && slaveHoldClocks==0; }
inline bool scl(){ return !sclDriven && !sclStuck; }
}
inline void pinMode(uint8_t pin,uint8_t){ if(pin==sim::sdaPin) sim::sdaDriven=false; else sim::sclDriven=false; }
inline void digitalWrite(uint8_t pin,uint8_t v){
  if(pin==sim::sdaPin){ sim::sdaDriven=(v==LOW); return; }
  const bool wasHigh=sim::scl(); sim::sclDriven=(v==LOW);
  if(!wasHigh && sim::scl()){ ++sim::clocks; if(sim::slaveHoldClocks>0) --sim::slaveHoldClocks; }
}
inline int digitalRead(uint8_t pin){ return pin==sim::sdaPin ? sim::sda() : sim::scl(); }
inline void delayMicroseconds(uint32_t){}
