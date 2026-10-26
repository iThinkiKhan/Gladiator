#pragma once
#include <cstdint>
#include <cstddef>
// Counts driver use: the driver must never be handed a stuck bus.
class TwoWire { public:
  int transactions=0, begins=0, ends=0;
  bool begin(int,int,uint32_t){++begins;return true;} bool end(){++ends;return true;} void setTimeOut(uint16_t){}
  void beginTransmission(uint8_t){++transactions;} int endTransmission(bool=true){return 0;}
  size_t requestFrom(uint8_t,uint8_t n){++transactions;return n;} int read(){return 0;}
  size_t write(uint8_t){return 1;} size_t write(const uint8_t*,size_t n){return n;} };
extern TwoWire Wire;
