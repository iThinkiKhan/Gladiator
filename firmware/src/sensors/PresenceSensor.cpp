#include "PresenceSensor.h"
#include <esp_timer.h>
#include <string.h>

namespace sensors {
void PresenceSensor::update() {
  const uint32_t now=millis();
  auto &m=data_.meta;
  m.age(now,800,2500);
  if(!m.online) {
    if(tried_ && now-lastDiscoveryMs_<board::SENSOR_RETRY_MS) return;
    tried_=true; lastDiscoveryMs_=now;
    for(uint8_t a=0x2A; a<=0x2B; ++a) {
      if(!bus_.present(a)) continue;
      uint8_t status=0;
      if(!bus_.readRegisters(a,0,&status,1)) continue;
      m.address=a; m.online=m.initialized=true; m.hasSample=false;
      m.lastAttemptMs=now; m.health=SensorHealth::INITIALIZING;
      // Start acquisition if stopped; retain its existing mode and sensitivity.
      if(!(status&1)) {
        const uint8_t start[]={1,0x55};
        if(!bus_.write(a,start,2)) { m.error(now); m.online=false; return; }
      }
      lastPollMs_=now; return;
    }
    m.error(now); m.online=false; m.health=SensorHealth::OFFLINE; return;
  }
  if(now-lastPollMs_<200) return;
  lastPollMs_=m.lastAttemptMs=now;
  uint8_t status=0,after=0,version=0,result[7]={},config[11]={};
  if(!bus_.readRegisters(m.address,0,&status,1) ||
     !bus_.readRegisters(m.address,3,&version,1) ||
     !bus_.readRegisters(m.address,0x10,result,7) ||
     !bus_.readRegisters(m.address,0x20,config,11) ||
     !bus_.readRegisters(m.address,0,&after,1)) { m.error(now); return; }
  if((status&0x83)!=(after&0x83)) { m.health=SensorHealth::DEGRADED; return; }
  data_.raw.status=status; data_.raw.firmwareVersion=version;
  memcpy(data_.raw.result,result,7); memcpy(data_.raw.configuration,config,11);
  data_.raw.receivedUs=esp_timer_get_time();
  auto &d=data_.derived;
  d.running=status&1; d.speedMode=status&2; d.initialized=status&0x80;
  d.presenceValid=d.running && d.initialized && !d.speedMode;
  d.present=d.presenceValid && (result[0]&1);
  d.targetCount=d.speedMode?result[0]:0;
  d.targetValid=d.running && d.initialized && d.speedMode && result[0]==1;
  // Preserve raw registers even when the selected operating mode makes these invalid.
  d.rangeMeters=int16_t(uint16_t(result[1]) | uint16_t(result[2])<<8)/100.0F;
  d.speedMetersPerSecond=int16_t(uint16_t(result[3]) | uint16_t(result[4])<<8)/100.0F;
  d.energy=uint16_t(result[5]) | uint16_t(result[6])<<8;
  m.good(now);
  if(!d.running || !d.initialized) m.health=SensorHealth::DEGRADED;
}
}
