#include "MatrixLidarSensor.h"
#include <esp_timer.h>

namespace sensors {
void MatrixLidarSensor::fail(bool protocol) {
  data_.meta.error(millis());
  if(protocol) ++data_.protocolErrors;
  data_.derived.valid=false;
  state_=State::DISCOVER; stateMs_=millis(); tried_=true;
}
bool MatrixLidarSensor::command(uint8_t id) {
  const uint8_t mode[]={0x55,0,5,1,0,0,0,8};
  const uint8_t sample[]={0x55,0,1,2};
  data_.meta.lastAttemptMs=millis();
  if(!bus_.write(data_.meta.address,id==1?mode:sample,id==1?8:4)) { fail(); return false; }
  command_=id; state_=State::RESPONSE; stateMs_=millis(); lastPollMs_=stateMs_;
  return true;
}
bool MatrixLidarSensor::response() {
  uint8_t head=0;
  if(!bus_.read(data_.meta.address,&head,1)) { fail(); return false; }
  if(head==0xFF) return false; // Device has not prepared a response yet.
  if(head!=0x53 && head!=0x63) { fail(true); return false; }
  uint8_t cmd=0, lengthBytes[2]={};
  if(!bus_.read(data_.meta.address,&cmd,1) || !bus_.read(data_.meta.address,lengthBytes,2)) { fail(); return false; }
  const uint16_t length=uint16_t(lengthBytes[0]) | uint16_t(lengthBytes[1])<<8;
  // Bound the payload before reading; never let a malformed response overrun 64 zones.
  if(cmd!=command_ || length>128 || (head==0x53 && cmd==2 && length!=128)) { fail(true); return false; }
  uint8_t payload[128]={};
  for(uint16_t offset=0; offset<length; offset+=32) {
    const uint8_t count=min<uint16_t>(32,length-offset);
    if(!bus_.read(data_.meta.address,payload+offset,count)) { fail(); return false; }
  }
  status_=head;
  if(head!=0x53) { fail(true); return false; }
  if(cmd==2) {
    auto &d=data_.derived; d.valid=false; d.usableZones=0;
    for(uint8_t i=0; i<64; ++i) {
      const uint16_t mm=uint16_t(payload[2*i]) | uint16_t(payload[2*i+1])<<8;
      data_.raw.distanceMm[i]=mm;
      // A range check is a derived usability test, NOT a native target status.
      // 4000 is the top of the documented span and the value the sensor returns
      // for a zone with no return at all: bench measurement on 2026-09-09 saw it
      // land on exactly 4000 every time while real readings jittered by tens of
      // mm. Counting it would report empty air as a usable 4 m obstacle reading,
      // so the sentinel is excluded and the span is half-open.
      if(mm>=20 && mm<4000) {
        ++d.usableZones;
        if(!d.valid || mm<d.nearestMm) { d.valid=true; d.nearestMm=mm; d.nearestZone=i; }
      }
    }
    data_.raw.responseStatus=head; data_.raw.responseCommand=cmd;
    data_.raw.receivedUs=esp_timer_get_time();
    data_.meta.initialized=true; data_.meta.good(millis());
  }
  return true;
}
void MatrixLidarSensor::update() {
  const uint32_t now=millis();
  auto &m=data_.meta;
  if(m.initialized) m.age(now,500,2500);
  if(!m.hasSample || now-m.lastUpdateMs>500) data_.derived.valid=false;
  switch(state_) {
    case State::DISCOVER:
      if(tried_ && now-stateMs_<board::SENSOR_RETRY_MS) return;
      tried_=true; stateMs_=now;
      for(uint8_t a=0x30; a<=0x33; ++a) {
        if(!bus_.present(a)) continue;
        m.address=a; m.initialized=false; m.hasSample=false;
        m.health=SensorHealth::INITIALIZING;
        command(1); return;
      }
      m.error(now); m.online=false; m.health=SensorHealth::OFFLINE; return;
    case State::RESPONSE:
      if(now-stateMs_>8000) { fail(true); return; }
      if(now-lastPollMs_<5) return;
      lastPollMs_=now;
      if(response()) {
        state_=command_==1?State::SETTLE:State::SAMPLE;
        if(command_==1) stateMs_=millis();
      }
      return;
    case State::SETTLE:
      if(now-stateMs_<5000) return;
      command(2); return;
    case State::SAMPLE:
      if(now-stateMs_<67) return;
      command(2); return;
  }
}
}
