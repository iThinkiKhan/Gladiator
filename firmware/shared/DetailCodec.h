#pragma once
#include "RobotProtocol.h"
namespace robotlink {
// Requested detail tree tags: null, false, true, float64, UTF-8, array, map.
template<size_t Capacity> struct TreeWriter {
  uint8_t data[Capacity];size_t size=0;bool valid=true;
  void bytes(const void *p,size_t n){if(size+n>sizeof(data)){valid=false;return;}memcpy(data+size,p,n);size+=n;}
  void u8(uint8_t v){bytes(&v,1);}
  void u16(size_t v){u8(uint8_t(v));u8(uint8_t(v>>8));}
  void string(const char *s){size_t n=strlen(s);u16(n);bytes(s,n);}
  void number(double d){uint64_t bits;memcpy(&bits,&d,8);for(unsigned i=0;i<8;++i)u8(uint8_t(bits>>(i*8)));}
};
using DetailWriter=TreeWriter<MAX_PAYLOAD>;
struct DetailReader {
  const uint8_t *data;size_t size,pos=0;bool valid=true;
  uint8_t u8(){if(pos>=size){valid=false;return 0;}return data[pos++];}
  size_t u16(){size_t n=u8();return n|(size_t(u8())<<8);}
  double number(){uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=uint64_t(u8())<<(8*i);double d;memcpy(&d,&bits,8);return d;}
};
}
