#pragma once
#include "RobotProtocol.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace robotlink {
enum class Transport:uint8_t { NONE=0, UART=1, WIFI=2 };
inline const char *name(Transport t){return t==Transport::UART?"uart":t==Transport::WIFI?"wifi":"none";}
constexpr uint32_t HEARTBEAT_MS=250, LOSS_MS=2500, STABLE_MS=5000, COMMAND_MS=350;
constexpr uint16_t WIFI_PORT=8765;
constexpr char FIELD_SSID[]="Gladiator-Gateway", FIELD_PASSWORD[]=GLADIATOR_FIELD_PASSWORD, FIELD_IP[]="192.168.8.1";

// Explicit codecs: no compiler packing, native struct layout or ABI on the wire.
struct Writer {
  uint8_t data[1024]={}; size_t size=0;
  void u8(uint8_t v){if(size<sizeof(data))data[size++]=v;}
  void u32(uint32_t v){for(unsigned i=0;i<4;++i)u8(uint8_t(v>>(8*i)));}
  void f32(float v){uint32_t bits;static_assert(sizeof(v)==4,"IEEE float required");memcpy(&bits,&v,4);u32(bits);}
};
struct Reader {
  const uint8_t *data;size_t size,pos=0;bool valid=true;
  Reader(const Frame &f):data(reinterpret_cast<const uint8_t *>(f.payload)),size(f.length){}
  uint8_t u8(){if(pos>=size){valid=false;return 0;}return data[pos++];}
  uint32_t u32(){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(u8())<<(8*i);return v;}
  float f32(){uint32_t bits=u32();float v;memcpy(&v,&bits,4);return v;}
  bool done()const{return valid&&pos==size;}
};
struct Peer {
  uint8_t role=0; // 1 = authoritative S3, 2 = C6 interface
  Transport active=Transport::NONE;
  uint32_t probe=0,echoBoot=0,echoProbe=0,epoch=0,wifiLeaseMs=0;
  Writer encode()const{Writer w;w.u8(role);w.u8(uint8_t(active));w.u32(probe);w.u32(echoBoot);w.u32(echoProbe);w.u32(epoch);w.u32(wifiLeaseMs);return w;}
  bool decode(const Frame &f){Reader r(f);role=r.u8();active=Transport(r.u8());probe=r.u32();echoBoot=r.u32();echoProbe=r.u32();epoch=r.u32();wifiLeaseMs=r.u32();return r.done()&&role>=1&&role<=2&&uint8_t(active)<=2;}
};
struct Health {
  bool seen=false,confirmed=false,sequenceKnown=false;
  uint32_t lastRx=0,lastGood=0,stableSince=0,boot=0,sequence=0,peerProbe=0,probe=0,changes=0;
  bool accept(const Frame &f){
    if(sequenceKnown&&boot==f.bootId){uint32_t d=f.sequence-sequence;if(!d||d>0x7fffffff)return false;}
    if(boot!=f.bootId){confirmed=false;seen=false;++changes;}
    boot=f.bootId;sequence=f.sequence;sequenceKnown=true;return true;
  }
  void beat(const Peer &p,uint32_t localBoot,uint32_t now){
    lastRx=now;seen=true;peerProbe=p.probe;
    // Require an echo of a recent local probe: one-way UART is a failed link.
    if(p.echoBoot!=localBoot||uint32_t(probe-p.echoProbe)>2)return;
    if(!confirmed||uint32_t(now-lastGood)>HEARTBEAT_MS*3)stableSince=now;
    confirmed=true;lastGood=now;
  }
  bool alive(uint32_t now)const{return confirmed&&uint32_t(now-lastGood)<LOSS_MS;}
  bool stable(uint32_t now)const{return alive(now)&&uint32_t(now-lastGood)<=HEARTBEAT_MS*3&&uint32_t(now-stableSince)>=STABLE_MS;}
};
struct Selector {
  Transport active=Transport::NONE;uint32_t switches=0,changedMs=0;
  bool update(const Health &uart,const Health &wifi,uint32_t now){
    Transport next=active;
    if(active==Transport::UART&&!uart.alive(now))next=wifi.alive(now)?Transport::WIFI:Transport::NONE;
    if(active==Transport::WIFI&&!wifi.alive(now))next=Transport::NONE;
    if(uart.stable(now))next=Transport::UART;
    else if(next==Transport::NONE&&wifi.alive(now))next=Transport::WIFI;
    if(next==active)return false;
    active=next;++switches;changedMs=now;return true;
  }
};
struct Control {
  uint32_t epoch=0,client=0,sequence=0,observedMs=0;
  uint8_t operation=0,deadman=0,overdrive=0,power=25;int8_t left=0,right=0;
  Writer encode()const{Writer w;w.u32(epoch);w.u32(client);w.u32(sequence);w.u32(observedMs);w.u8(operation);w.u8(deadman);w.u8(overdrive);w.u8(power);w.u8(uint8_t(left));w.u8(uint8_t(right));return w;}
  bool decode(const Frame &f){Reader r(f);epoch=r.u32();client=r.u32();sequence=r.u32();observedMs=r.u32();operation=r.u8();deadman=r.u8();overdrive=r.u8();power=r.u8();left=int8_t(r.u8());right=int8_t(r.u8());return r.done();}
};
enum class Result:uint8_t { ACCEPTED=0, EXPIRED=1, OWNER=2, INVALID=3, REPLAY=4, STALE=5, OFFLINE=6 };
// Only instantiated by the S3 control loop. The C6 never decides safety state.
struct Session {
  uint32_t epoch=1,client=0,sequence=0,lastMs=0;bool armed=false;
  void invalidate(){++epoch;if(!epoch)++epoch;client=0;armed=false;}
  bool expire(uint32_t now){if(armed&&uint32_t(now-lastMs)>COMMAND_MS){invalidate();return true;}return false;}
  Result validate(const Control &c,uint32_t now,bool otherMoving){
    expire(now);
    if(c.epoch!=epoch)return Result::EXPIRED;
    if(otherMoving||(armed&&client!=c.client))return Result::OWNER;
    if(!c.client||c.operation>2||c.deadman>1||c.overdrive>1||c.power>100||c.left<-100||c.left>100||c.right<-100||c.right>100){invalidate();return Result::INVALID;}
    if(uint32_t(now-c.observedMs)>300)return Result::STALE;
    if(armed&&int32_t(c.sequence-sequence)<=0)return Result::REPLAY;
    if(c.operation==1){if(armed||c.deadman||c.left||c.right)return Result::INVALID;armed=true;client=c.client;}
    else if(c.operation==2&&!armed)return Result::EXPIRED;
    sequence=c.sequence;lastMs=now;
    if(c.operation==0)invalidate();
    return Result::ACCEPTED;
  }
};
}
