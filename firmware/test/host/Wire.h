#pragma once
#include "Arduino.h"
#include <array>
#include <deque>
#include <map>
#include <vector>
class TwoWire {
public:
  std::map<uint8_t,std::array<uint8_t,256>> registers;
  std::map<uint8_t,std::map<uint8_t,uint16_t>> registers16;
  std::map<uint8_t,std::deque<uint8_t>> streams;
  std::vector<std::vector<uint8_t>> writes;
  std::vector<uint8_t> transmit;
  std::deque<uint8_t> receive;
  uint8_t address=0,reg=0;
  bool shortRead=false;
  bool begin(uint8_t,uint8_t,uint32_t) { return true; }
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t a) { address=a; transmit.clear(); }
  size_t write(uint8_t v) { transmit.push_back(v); return 1; }
  size_t write(const uint8_t *p,size_t n) { transmit.insert(transmit.end(),p,p+n); return n; }
  uint8_t endTransmission(bool=true) {
    if(!registers.count(address)&&!streams.count(address)&&!registers16.count(address)) return 2;
    if(!transmit.empty()) {
      reg=transmit[0];
      if(transmit.size()>1) writes.push_back(transmit);
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t a,uint8_t n) {
    receive.clear();
    if(!registers.count(a)&&!streams.count(a)&&!registers16.count(a)) return 0;
    uint8_t count=shortRead?n-1:n;
    for(uint8_t i=0;i<count;++i) {
      if(registers16.count(a)) {
        const uint16_t value=registers16[a][reg];
        receive.push_back(i==0?value>>8:value&255);
      } else if(streams.count(a)) {
        auto &s=streams[a]; receive.push_back(s.empty()?0xFF:s.front());
        if(!s.empty()) s.pop_front();
      } else receive.push_back(registers[a][uint8_t(reg+i)]);
    }
    return count;
  }
  int read() { auto v=receive.front(); receive.pop_front(); return v; }
};
extern TwoWire Wire;
