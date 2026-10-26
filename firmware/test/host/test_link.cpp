#include "../../shared/GladiatorLink.h"
#include "../../shared/DetailCodec.h"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace robotlink;
Frame frame(Type t,const Writer &w,uint32_t sequence=1,uint32_t boot=22){return {t,sequence,boot,reinterpret_cast<const char *>(w.data),w.size};}
void beat(Health &h,uint32_t now,bool echo=true){Peer p;p.role=2;p.probe=++h.probe;p.echoBoot=echo?11:0;p.echoProbe=h.probe;h.beat(p,11,now);}
Control command(Session &s,uint32_t now,uint8_t op,uint32_t seq){Control c;c.epoch=s.epoch;c.client=42;c.observedMs=now;c.operation=op;c.sequence=seq;return c;}
int main(){
  Writer w;w.u8(0xff);w.u32(0x12345678);w.f32(-12.5f);Reader r(frame(Type::POWER,w));assert(r.u8()==255&&r.u32()==0x12345678&&r.f32()==-12.5f&&r.done());assert(r.u8()==0&&!r.done());
  Peer peer;peer.role=1;peer.active=Transport::WIFI;peer.probe=19;peer.echoBoot=22;peer.epoch=91;peer.wifiLeaseMs=60000;
  auto p=peer.encode();Peer decoded;assert(decoded.decode(frame(Type::HEARTBEAT,p)));assert(decoded.epoch==91&&decoded.active==Transport::WIFI);p.u8(0);assert(!decoded.decode(frame(Type::HEARTBEAT,p)));
  Health uart,wifi;Selector selection;
  for(uint32_t now=0;now<=10000;now+=250){beat(uart,now,false);selection.update(uart,wifi,now);}assert(selection.active==Transport::NONE);assert(!uart.alive(10000));
  for(uint32_t now=10250;now<=15250;now+=250){beat(uart,now);selection.update(uart,wifi,now);}assert(selection.active==Transport::UART);
  // One missing probe must not change transports. A longer gap resets recovery.
  beat(uart,15750);assert(!selection.update(uart,wifi,15750));assert(uart.stable(15750));
  beat(wifi,18000);assert(!selection.update(uart,wifi,18000));assert(selection.update(uart,wifi,18250));assert(selection.active==Transport::WIFI);
  for(uint32_t now=18250;now<=22000;now+=250){beat(uart,now);beat(wifi,now);selection.update(uart,wifi,now);assert(selection.active==Transport::WIFI);}
  beat(uart,23000);beat(wifi,23000);selection.update(uart,wifi,23000);assert(!uart.stable(23000));
  for(uint32_t now=23250;now<=28000;now+=250){beat(uart,now);beat(wifi,now);selection.update(uart,wifi,now);}assert(selection.active==Transport::UART);
  assert(selection.update(uart,wifi,30500)&&selection.active==Transport::NONE);
  Health wrap;for(uint32_t elapsed=0;elapsed<=5250;elapsed+=250)beat(wrap,uint32_t(0xfffff000U+elapsed));assert(wrap.stable(uint32_t(0xfffff000U+5250)));
  Writer empty;Health replay;assert(replay.accept(frame(Type::PING,empty,0xffffffff,9)));assert(replay.accept(frame(Type::PING,empty,0,9)));assert(!replay.accept(frame(Type::PING,empty,0,9)));assert(!replay.accept(frame(Type::PING,empty,0xffffffff,9)));assert(replay.accept(frame(Type::PING,empty,0,10)));
  Session session;session.epoch=1234;
  auto move=command(session,10,2,1);move.deadman=1;move.left=move.right=25;assert(session.validate(move,10,false)==Result::EXPIRED&&!session.armed);
  auto arm=command(session,10,1,1);assert(session.validate(arm,10,true)==Result::OWNER);assert(session.validate(arm,10,false)==Result::ACCEPTED&&session.armed);
  move=command(session,100,2,2);move.deadman=1;move.left=move.right=25;assert(session.validate(move,100,false)==Result::ACCEPTED);assert(session.validate(move,101,false)==Result::REPLAY);
  auto other=move;other.client=7;other.sequence++;assert(session.validate(other,110,false)==Result::OWNER&&session.client==42);
  assert(!session.expire(450));assert(session.expire(451));assert(session.validate(move,451,false)==Result::EXPIRED&&!session.armed);
  // Even a delayed explicit arm from the expired session cannot rearm it.
  assert(session.validate(arm,451,false)==Result::EXPIRED);
  arm=command(session,500,1,3);assert(session.validate(arm,500,false)==Result::ACCEPTED);
  auto old=command(session,600,2,4);session.invalidate();assert(session.validate(old,600,false)==Result::EXPIRED);
  arm=command(session,700,1,5);arm.left=1;assert(session.validate(arm,700,false)==Result::INVALID&&!session.armed);
  arm.left=0;arm.observedMs=350;assert(session.validate(arm,700,false)==Result::STALE);
  arm.observedMs=701;assert(session.validate(arm,700,false)==Result::STALE);
  arm.observedMs=700;assert(session.validate(arm,700,false)==Result::ACCEPTED);
  auto bad=command(session,750,2,6);bad.left=-101;assert(session.validate(bad,750,false)==Result::INVALID&&!session.armed);
  arm=command(session,0xfffffff0,1,0xfffffffe);assert(session.validate(arm,0xfffffff0,false)==Result::ACCEPTED);
  move=command(session,20,2,0xffffffff);assert(session.validate(move,20,false)==Result::ACCEPTED);move.sequence=0;assert(session.validate(move,20,false)==Result::ACCEPTED);
  auto stop=command(session,30,0,1);auto epoch=session.epoch;assert(session.validate(stop,30,false)==Result::ACCEPTED&&!session.armed&&session.epoch!=epoch);
  // Typed control survives the exact framing used by BOTH adapters.
  auto payload=move.encode();uint8_t raw[MAX_RAW],wire[MAX_FRAME];size_t n=encode(Type::CONTROL,8,9,reinterpret_cast<const char *>(payload.data),payload.size,raw,wire,sizeof(wire));
  for(unsigned transport=0;transport<2;++transport){Decoder decoder;Frame received;bool got=false;for(size_t i=0;i<n;++i)got=decoder.feed(wire[i],20,received)||got;Control c;assert(got&&c.decode(received)&&c.client==move.client&&c.sequence==move.sequence);}
  DetailWriter tree;tree.u8(3);tree.number(12345.6789);DetailReader reader{tree.data,tree.size};assert(reader.u8()==3&&reader.number()==12345.6789&&reader.valid);
  tree.size=MAX_PAYLOAD;tree.u8(1);assert(!tree.valid&&tree.size==MAX_PAYLOAD);
  puts("Gladiator Link tests passed: typed codecs, bidirectional loss, hysteresis, reboot/replay, clock wrap, ownership, arming, expiry and transport-invariant control.");
}
