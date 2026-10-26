#include "../../shared/RobotProtocol.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace robotlink;
uint8_t raw[MAX_RAW],encoded[MAX_FRAME];
bool feed(Decoder &d,const uint8_t *p,size_t n,Frame &f,uint32_t now=10){bool good=false;for(size_t i=0;i<n;++i)good=d.feed(p[i],now,f)||good;return good;}
int main(){
  assert(crc32((const uint8_t*)"123456789",9)==0xcbf43926);
  Decoder d;Frame f;
  for(size_t length:{size_t(0),size_t(1),size_t(254),size_t(255),MAX_PAYLOAD}){
    std::string payload(length,'x');for(size_t i=0;i<length;i+=17)payload[i]=char(i);
    size_t n=encode(Type::TELEMETRY,0xffffffff,42,payload.data(),length,raw,encoded,sizeof(encoded));assert(n&&n<=MAX_FRAME);
    assert(feed(d,encoded,n,f));assert(f.type==Type::TELEMETRY&&f.sequence==0xffffffff&&f.bootId==42&&f.length==length);
    assert(memcmp(f.payload,payload.data(),length)==0);
  }
  assert(!encode(Type::PING,0,0,"",MAX_PAYLOAD+1,raw,encoded,sizeof(encoded)));
  assert(!encode(Type::PING,0,0,"{}",2,raw,encoded,1));
  size_t n=encode(Type::PING,1,2,"{}",2,raw,encoded,sizeof(encoded));
  std::vector<uint8_t> good(encoded,encoded+n);
  // Valid COBS with bad CRC must never reach the application.
  raw[HEADER_SIZE]^=1;size_t corrupt=cobsEncode(raw,HEADER_SIZE+2+4,encoded,sizeof(encoded));encoded[corrupt++]=0;
  assert(!feed(d,encoded,corrupt,f));assert(d.errors==1);assert(feed(d,good.data(),good.size(),f));
  // Correct CRC but inconsistent length/version is rejected.
  n=encode(Type::PING,1,2,"{}",2,raw,encoded,sizeof(encoded));raw[10]=3;write32(raw+14,crc32(raw,14));
  n=cobsEncode(raw,18,encoded,sizeof(encoded));encoded[n++]=0;assert(!feed(d,encoded,n,f));
  raw[10]=2;raw[0]=99;write32(raw+14,crc32(raw,14));n=cobsEncode(raw,18,encoded,sizeof(encoded));encoded[n++]=0;assert(!feed(d,encoded,n,f));
  // Overflow and stalled partial packets discard through the next delimiter.
  std::vector<uint8_t> noise(MAX_FRAME+50,1);assert(!feed(d,noise.data(),noise.size(),f));assert(d.oversize==1);
  assert(!d.feed(0,10,f));assert(feed(d,good.data(),good.size(),f));
  assert(!d.feed(3,100,f));assert(!d.feed(1,601,f));assert(d.timeouts==1);assert(!d.feed(0,602,f));
  assert(feed(d,good.data(),good.size(),f,603));
  // A fragmented frame can straddle the millisecond clock wrap.
  Decoder wrap;assert(!feed(wrap,good.data(),3,f,0xfffffff0));assert(feed(wrap,good.data()+3,good.size()-3,f,8));
  // Arbitrary noise interspersed with delimiters always permits recovery.
  uint32_t random=1234;for(int k=0;k<1000;++k){for(int j=0;j<k%100;++j){random=random*1664525+1013904223;d.feed(random>>24,700,f);}d.feed(0,700,f);assert(feed(d,good.data(),good.size(),f,700));}
  puts("Protocol tests passed: CRC, COBS, bounds, corruption, timeout, overflow, clock wrap, noise recovery.");
}
