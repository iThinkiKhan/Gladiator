#pragma once
#include "GladiatorLink.h"
// Compact 64-zone extension of RANGE. Legacy RANGE frames remain valid.
namespace rangegrid {
// One packet per LiDAR sample (the sensor runs at ~15 Hz); about 2 KB/s on the link.
constexpr uint32_t PERIOD_MS=67, FRESH_MS=2500;
constexpr size_t ZONES=64, WIRE_BYTES=ZONES*2;
struct Sample {
  uint16_t mm[ZONES]={};
  uint32_t receivedMs=0,sourceAgeMs=0,sampleCount=0;
  bool seen=false,usable=false;
  bool fresh(uint32_t now)const {
    const uint32_t elapsed=uint32_t(now-receivedMs);
    return seen&&usable&&sourceAgeMs<FRESH_MS&&elapsed<FRESH_MS&&sourceAgeMs+elapsed<FRESH_MS;
  }
};
inline void write(robotlink::Writer &w,const uint16_t (&mm)[ZONES]) {
  for(auto v:mm){w.u8(uint8_t(v));w.u8(uint8_t(v>>8));}
}
inline bool read(robotlink::Reader &r,Sample &out) {
  if(!r.valid||r.pos>r.size||r.size-r.pos!=WIRE_BYTES)return false;
  for(auto &v:out.mm){v=r.u8();v|=uint16_t(r.u8())<<8;}
  return r.done();
}
}
