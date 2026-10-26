#define GLADIATOR_WIFI_HOST_TEST
#include "../../shared/WifiLink.h"
#include <cassert>
#include <cstdio>
using namespace robotlink;
WifiChannel client,server;
uint32_t now=0;unsigned received=0;
const uint8_t data[]={1,2,0,4,255};
void tick(){++now;client.poll(now,[](const uint8_t *,size_t){});server.poll(now,[](const uint8_t *p,size_t n){assert(n==sizeof(data)&&memcmp(p,data,n)==0);++received;});}
void connectPair(){incoming[0].clear();incoming[1].clear();client.attach(0,false,now);server.attach(1,true,now);for(int i=0;i<10;++i)tick();assert(client.ready&&server.ready);}
int main(){
  uint8_t key[20],digest[32];memset(key,0x0b,sizeof(key));
  const uint8_t expected[]={0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
  assert(hmac(key,20,reinterpret_cast<const uint8_t *>("Hi There"),8,nullptr,0,digest)&&memcmp(digest,expected,32)==0);
  connectingReady=false;client.attach(0,false,now);client.poll(++now,[](const uint8_t *,size_t){});assert(incoming[1].empty()&&client.fd>=0);connectingReady=true;
  connectPair();assert(client.send(data,sizeof(data),now));assert(client.send(data,sizeof(data),now));
  for(int i=0;i<30;++i)tick();assert(received==2&&server.errors==0);
  // Capture a valid authenticated record and then replay it in the same session.
  assert(client.send(data,sizeof(data),now));for(int i=0;i<10;++i)client.poll(++now,[](const uint8_t *,size_t){});
  auto record=incoming[1];for(int i=0;i<20;++i)tick();assert(received==3);
  incoming[1]=record;for(int i=0;i<20;++i)tick();assert(server.fd<0&&received==3);
  // Connection nonces prevent replay across reconnections, even at counter 1.
  connectPair();incoming[1]=record;for(int i=0;i<20;++i)tick();assert(server.fd<0&&received==3);
  connectPair();assert(client.send(data,sizeof(data),now));for(int i=0;i<10;++i)client.poll(++now,[](const uint8_t *,size_t){});
  assert(incoming[1].size()>10);incoming[1][8]^=1;for(int i=0;i<20;++i)tick();assert(server.fd<0&&received==3);
  // Queue pressure is bounded; successful frames keep contiguous MAC counters.
  connectPair();unsigned queued=0;while(client.send(data,sizeof(data),now))++queued;assert(queued>2&&queued<300);
  // Allow enough simulated network progress without exceeding the send deadline.
  for(int i=0;i<2000;++i){client.poll(now,[](const uint8_t *,size_t){});server.poll(now,[](const uint8_t *p,size_t n){assert(n==sizeof(data)&&memcmp(p,data,n)==0);++received;});}
  assert(received==3+queued&&server.ready);
  assert(client.send(data,sizeof(data),now));blocked=true;client.poll(now+501,[](const uint8_t *,size_t){});assert(client.fd<0);blocked=false;
  connectPair();client.close();client.attach(0,false,now);client.poll(now+2001,[](const uint8_t *,size_t){});assert(client.fd<0);
  puts("Wi-Fi adapter tests passed: HMAC vector, fragmented I/O, buffered frames, tampering, replay within/across connections, backpressure and deadlines.");
}
