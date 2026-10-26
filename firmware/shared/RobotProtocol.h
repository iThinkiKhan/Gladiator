#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Shared, allocation-free wire format used by both processors and host tests.
// Gladiator Link: COBS(header + typed payload + CRC32) followed by 0.
// Identical frames on UART and authenticated Wi-Fi streams. Integers are LE.
namespace robotlink {
constexpr uint8_t VERSION=2;
constexpr size_t MAX_PAYLOAD=10000, HEADER_SIZE=12;
constexpr size_t MAX_RAW=HEADER_SIZE+MAX_PAYLOAD+4;
constexpr size_t MAX_FRAME=MAX_RAW+MAX_RAW/254+2;
enum class Type:uint8_t { HELLO=1, HEARTBEAT=2, TELEMETRY=3, PING=4, PONG=5, GET_STATE=6,
  CONTROLLER=16, POWER=17, ORIENTATION=18, RANGE=19, PRESENCE=20,
  DETAIL=21, SUBSCRIBE=22, CONTROL=32, CONTROL_RESULT=33, MAST_CONTROL=34, MAST_STATE=35, MAST_CONFIG=36,
  WIFI_LEASE=40, SYSTEM=41, LOG=42 };
inline uint32_t read32(const uint8_t *p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
inline void write32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
inline uint32_t crc32(const uint8_t *p,size_t n) {
  uint32_t c=0xFFFFFFFF;
  for(size_t i=0;i<n;++i) { c^=p[i]; for(unsigned j=0;j<8;++j)c=(c>>1)^(0xEDB88320U & (0U-(c&1))); }
  return ~c;
}
inline size_t cobsEncode(const uint8_t *src,size_t n,uint8_t *dst,size_t capacity) {
  if(capacity<n+n/254+2)return 0;
  size_t codeAt=0,out=1; uint8_t code=1;
  for(size_t i=0;i<n;++i) {
    if(src[i]==0) { dst[codeAt]=code; codeAt=out++; code=1; }
    else { dst[out++]=src[i]; if(++code==0xFF) {dst[codeAt]=code;codeAt=out++;code=1;} }
  }
  dst[codeAt]=code; return out;
}
inline size_t cobsDecode(const uint8_t *src,size_t n,uint8_t *dst,size_t capacity) {
  size_t in=0,out=0;
  while(in<n) {
    const uint8_t code=src[in++];
    if(!code||size_t(code-1)>n-in||size_t(code-1)>capacity-out)return 0;
    for(unsigned j=1;j<code;++j)dst[out++]=src[in++];
    if(code!=0xFF && in<n) {if(out==capacity)return 0;dst[out++]=0;}
  }
  return out;
}
inline size_t encode(Type type,uint32_t sequence,uint32_t bootId,const char *json,size_t length,
                     uint8_t *raw,uint8_t *out,size_t capacity) {
  if(length>MAX_PAYLOAD)return 0;
  raw[0]=VERSION;raw[1]=uint8_t(type);write32(raw+2,sequence);write32(raw+6,bootId);
  raw[10]=uint8_t(length);raw[11]=uint8_t(length>>8);
  memcpy(raw+HEADER_SIZE,json,length);
  write32(raw+HEADER_SIZE+length,crc32(raw,HEADER_SIZE+length));
  size_t count=cobsEncode(raw,HEADER_SIZE+length+4,out,capacity);
  if(!count||count>=capacity)return 0;
  out[count++]=0;return count;
}
struct Frame {Type type;uint32_t sequence,bootId;const char *payload;size_t length;};
class Decoder {
public:
  void reset() {used_=0;discard_=false;lastByteMs_=0;}
  uint32_t frames=0,errors=0,oversize=0,timeouts=0;
  bool feed(uint8_t byte,uint32_t now,Frame &frame) {
    if(used_ && uint32_t(now-lastByteMs_)>500) {used_=0;discard_=true;++timeouts;}
    lastByteMs_=now;
    if(byte) {
      if(discard_)return false;
      if(used_>=sizeof(encoded_)) {used_=0;discard_=true;++oversize;return false;}
      encoded_[used_++]=byte;return false;
    }
    if(discard_) {discard_=false;used_=0;return false;}
    if(!used_)return false;
    size_t n=cobsDecode(encoded_,used_,decoded_,MAX_RAW);used_=0;
    if(n<HEADER_SIZE+4 || decoded_[0]!=VERSION || read32(decoded_+n-4)!=crc32(decoded_,n-4)) {++errors;return false;}
    size_t length=size_t(decoded_[10])|size_t(decoded_[11])<<8;
    if(length>MAX_PAYLOAD||n!=HEADER_SIZE+length+4) {++errors;return false;}
    frame={Type(decoded_[1]),read32(decoded_+2),read32(decoded_+6),reinterpret_cast<char *>(decoded_+HEADER_SIZE),length};
    // CRC has already been checked; a terminator makes payload access convenient.
    decoded_[HEADER_SIZE+length]=0;
    ++frames;return true;
  }
private:
  uint8_t encoded_[MAX_FRAME],decoded_[MAX_RAW];
  size_t used_=0;
  uint32_t lastByteMs_=0;
  bool discard_=false;
};
}
