#include "../shared/MastPolicy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace mast;
Calibration fixture(){Calibration c;c.pan={1200,1500,1800,-100,120,false};
  c.tilt={1300,1500,1700,-50,50,true};c.frequencyHz=50;c.speedDeciDegPerSec=50;
  c.modelsVerified=c.clearanceVerified=true;return c;}
Command request(uint32_t session,uint32_t seq,Operation op,int16_t p=0,int16_t t=0){return {session,seq,op,p,t};}
int main(){
  Controller m;assert(!m.enabled&&!m.referenced&&!m.calibrated&&m.session==0);
  assert(!m.reference(7,0,true));assert(!m.configure(Calibration{}));
  auto c=fixture();assert(c.valid());assert(m.configure(c));assert(!m.enabled);
  auto bad=c;bad.pan.maxDeciDeg=451;assert(!bad.valid());bad=c;bad.modelsVerified=false;assert(!bad.valid());
  bad=c;bad.pan.neutralUs=bad.pan.lowUs;assert(!bad.valid());bad=c;bad.frequencyHz=0;assert(!bad.valid());
  assert(!m.reference(7,0,false));assert(!m.reference(0,0,true));assert(m.reference(7,0,true));
  assert(m.awaitingOperator&&m.targetPan==0&&m.targetTilt==0);
  assert(!m.configure(c));assert(!m.reference(8,0,true));
  auto move=request(7,1,Operation::MOVE,120,50);assert(m.command(move,0,true));
  assert(!m.command(move,0,true));assert(!m.command(request(8,2,Operation::MOVE),0,true));
  assert(!m.command(request(7,2,Operation::MOVE,121),0,true));
  assert(!m.command(request(7,2,Operation::MOVE,0,51),0,true));
  assert(!m.command(request(7,2,Operation(255)),0,true));
  m.tick(20,true,true,true,false);assert(m.pan==1&&m.tilt==1);
  unsigned checked=0;
  for(int p=-451;p<=451;++p)for(int t=-201;t<=201;++t){
    Controller x;assert(x.configure(c));assert(x.reference(7,0,true));
    bool expected=c.pan.contains(p)&&c.tilt.contains(t);
    assert(x.command(request(7,1,Operation::MOVE,p,t),0,true)==expected);++checked;
  }
  assert(m.command(request(7,2,Operation::SCAN),20,true));
  for(uint32_t now=40,seq=3;now<=20000;now+=20,++seq){
    assert(m.command(request(7,seq,Operation::KEEPALIVE),now,true));
    float previous=m.pan;m.tick(now,true,true,true,false);
    assert(m.pan>=c.pan.minDeciDeg&&m.pan<=c.pan.maxDeciDeg&&fabsf(m.pan-previous)<=1.001F);
    auto pulse=c.pan.pulse(m.pan);assert(pulse>=c.pan.lowUs&&pulse<=c.pan.highUs);
  }
  assert(m.scanning);m.tick(20020,true,true,false,false);assert(m.scanning&&m.enabled); // Missing range is unknown, not a false obstacle.
  assert(m.command(request(7,2000,Operation::SCAN),20020,true));
  m.tick(20040,true,true,true,true);assert(!m.scanning);
  m.tick(20800,true,true,true,false);assert(!m.enabled&&!m.referenced&&!m.session);
  assert(!m.command(request(7,2001,Operation::KEEPALIVE),20800,true));
  for(auto fault:{Reason::SAFETY,Reason::LINK,Reason::TIMING}){
    Controller x;assert(x.configure(c));assert(x.reference(9,100,true));
    x.tick(fault==Reason::TIMING?201:120,fault!=Reason::SAFETY,fault!=Reason::LINK,true,false);
    assert(!x.enabled&&!x.referenced&&x.reason==fault);
    assert(!x.command(request(9,1,Operation::MOVE),220,true));
  }
  Controller wrap;assert(wrap.configure(c));assert(wrap.reference(9,0xfffffff0,true));
  wrap.tick(4,true,true,true,false);assert(wrap.enabled);
  assert(wrap.command(request(9,1,Operation::KEEPALIVE),4,true));
  wrap.tick(24,true,true,true,false);assert(wrap.enabled);
  assert(wrap.command(request(9,2,Operation::STOP),24,true));assert(!wrap.referenced);
  Controller waiting;assert(waiting.configure(c));assert(waiting.reference(10,0,true));
  for(uint32_t n=20;n<=10000;n+=20)waiting.tick(n,true,true,true,false);
  assert(!waiting.enabled&&waiting.reason==Reason::LEASE);
  assert(c.tilt.pulse(-50)==1700&&c.tilt.pulse(0)==1500&&c.tilt.pulse(50)==1300);
  Axis endpoint{1200,1500,1500,-100,0,false};assert(endpoint.valid(450));
  assert(endpoint.pulse(-100)==1200&&endpoint.pulse(0)==1500&&!endpoint.contains(1));
  auto w=move.encode();robotlink::Frame frame{robotlink::Type::MAST_CONTROL,0,0,reinterpret_cast<const char *>(w.data),w.size};
  Command decoded;assert(decoded.decode(frame));assert(decoded.pan==120&&decoded.tilt==50&&decoded.session==7);
  frame.length--;assert(!decoded.decode(frame));
  auto geared=c;geared.pan={1000,1500,2000,-450,450,false,-900,900,1.5F};
  geared.tilt={1000,1500,2000,-200,200,false,-900,900,1.0F};assert(geared.valid());
  assert(geared.pan.pulse(-450)==1333&&geared.pan.pulse(450)==1667); // Head +/-45 = shaft +/-30, not +/-45.
  geared.panPin=13;geared.tiltPin=10;assert(geared.valid());geared.tiltPin=13;assert(!geared.valid());
  Controller jog;assert(jog.configure(c));assert(jog.reference(1,0,true));
  assert(!jog.command(request(1,1,Operation::RATE,301),0,true));assert(jog.command(request(1,1,Operation::RATE,300),0,true));
  assert(!jog.command(request(1,2,Operation::JOG,2),0,true));assert(jog.command(request(1,2,Operation::JOG,-1,1),0,true));
  jog.tick(20,true,true,false,false);assert(jog.pan==-6&&jog.tilt==6);
  assert(jog.command(request(1,3,Operation::HOLD),20,true));jog.tick(40,true,true,false,false);assert(jog.pan==-6&&jog.tilt==6);
  // The override can permit raw servo testing, but always inhibits driving.
  for(bool bench:{false,true})for(bool low:{false,true})for(bool shed:{false,true})for(bool manual:{false,true}){
    assert(drivePowerInhibited(bench,low,shed,manual)==(manual||(!bench&&(low||shed))));
  }
  assert(!manualTestReady(true,true,true,false));
  assert(manualTestReady(true,true,true,true));
  assert(!manualTestReady(false,true,true,true));
  assert(!manualTestReady(true,true,false,true));
  PulseTest overrideTest;assert(!overrideTest.batteryOverride);
  assert(!overrideTest.start(10,1500,500,2500,50,0,false,true));assert(!overrideTest.batteryOverride);
  assert(overrideTest.start(10,1500,500,2500,50,0,manualTestReady(true,true,true,true),true));
  assert(overrideTest.batteryOverride&&drivePowerInhibited(true,false,false,overrideTest.enabled&&overrideTest.batteryOverride));
  assert(!overrideTest.jogPin(13,1600,20,true));assert(overrideTest.batteryOverride);
  overrideTest.stop();assert(!overrideTest.enabled&&!overrideTest.batteryOverride);
  for(auto fault:{Reason::SAFETY,Reason::TIMING,Reason::LEASE}){
    assert(overrideTest.start(10,1500,500,2500,50,0,true,true));
    if(fault==Reason::LEASE){for(uint32_t n=20;n<=10000;n+=20)overrideTest.tick(n,true);}
    else overrideTest.tick(fault==Reason::TIMING?101:20,fault!=Reason::SAFETY);
    assert(!overrideTest.enabled&&!overrideTest.batteryOverride&&overrideTest.reason==fault);
  }
  assert(overrideTest.start(10,1500,500,2500,50,0,true));assert(!overrideTest.batteryOverride);overrideTest.stop();
  PulseTest routed;
  for(int selected:{10,13}){
    assert(routed.start(selected,1500,500,2500,50,100,true));
    assert(!routed.jogPin(selected==10?13:10,1800,120,true));
    assert(routed.targetUs==1500&&routed.lastCommand==100);
    assert(!routed.jogPin(0,1800,120,true));
    assert(routed.jogPin(selected,1800,120,true));
    assert(routed.targetUs==1800&&routed.lastCommand==120);routed.stop();
  }
  PulseTest full;assert(full.start(13,1500,500,2575,50,0,true));
  assert(full.jog(500,0,true));full.tick(20,true);assert(full.pulseUs==1498);
  assert(full.jog(2575,20,true));full.tick(40,true);assert(full.pulseUs==1500);
  assert(!full.jog(499,40,true));assert(!full.jog(2576,40,true));
  full.stop();assert(!full.start(13,1500,499,2575,50,0,true));
  assert(!full.start(13,1500,500,2576,50,0,true));
  assert(!full.start(13,499,500,2575,50,0,true));
  assert(!full.start(13,2576,500,2575,50,0,true));
  Axis rear=c.pan;rear.lowUs=500;rear.neutralUs=1200;rear.highUs=2575;rear.reversed=true;assert(rear.valid(450));
  assert(rear.pulse(rear.minDeciDeg)==2575&&rear.pulse(0)==1200&&rear.pulse(rear.maxDeciDeg)==500);
  rear.highUs=2576;assert(!rear.valid(450));
  PulseTest test;assert(!test.start(6,1500,1400,1600,50,0,true));
  assert(!test.start(10,1500,1400,1600,50,0,false));assert(test.start(13,1500,1400,1600,50,0,true));
  assert(!test.jog(1601,0,true));assert(test.jog(1600,0,true));test.tick(20,true);assert(test.pulseUs==1502);
  test.tick(121,true);assert(!test.enabled);assert(!test.jog(1500,121,true));
  assert(test.start(10,1500,1400,1600,50,0xfffffff0,true));test.tick(4,true);assert(test.enabled);test.tick(24,false);assert(!test.enabled);
  assert(test.start(10,1500,1400,1600,50,0,true));for(uint32_t n=20;n<=10000;n+=20)test.tick(n,true);assert(!test.enabled);
  puts("Mast policy passed: boot lock, measured gearing/bounds, manual rate/jog, bounded pulse commissioning, scan, invalid sensors, stop, replay, faults and expiry.");
  printf("Checked %u angle pairs.\n",checked);
}
