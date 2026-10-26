#pragma once
#include <cstdint>
#include <cstddef>
class TwoWire {public:uint8_t address=0;bool begin(int=0,int=0,uint32_t=0){return true;}bool end(){return true;}void setTimeOut(uint16_t){}void beginTransmission(uint8_t a){address=a;}int endTransmission(bool=true){return address==0x4b?0:2;}int requestFrom(uint8_t,uint8_t n){return n;}int read(){return 0;}size_t write(uint8_t){return 1;}size_t write(const uint8_t*,size_t n){return n;}};
extern TwoWire Wire;
