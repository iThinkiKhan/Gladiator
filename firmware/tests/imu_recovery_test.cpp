#include "BNO085Sensor.h"
#include "SensorProgress.h"
#include <cassert>
#include <cstdio>
TwoWire Wire;ImuMock imuMock;uint32_t mockNow=100;
namespace sensors {volatile uint32_t sensorStep=0,sensorOperation=0;}
uint32_t millis(){return mockNow;}int64_t esp_timer_get_time(){static int64_t sub;return int64_t(mockNow)*1000+(++sub%100);}
extern "C" {
int sh2_setSensorCallback(sh2_SensorCallback_t*,void*){return SH2_OK;}
void sh2_service(){}
int sh2_setCalConfig(uint8_t){return SH2_OK;}
int sh2_getCalConfig(uint8_t *b){*b=7;return SH2_OK;}
int sh2_saveDcdNow(){return SH2_OK;}
int sh2_decodeSensorEvent(sh2_SensorValue_t*,const sh2_SensorEvent_t*){return SH2_ERR;}
}
using namespace sensors;
void freshFusion(ImuData &d){d.meta.good(mockNow);auto &r=d.reports[IMU_GAME_INDEX];r.hasSample=true;r.lastUpdateMs=mockNow;d.gameDerived.valid=true;}
int main(){
 I2CBus bus;ImuData data;BNO085Sensor driver(bus,data);
 imuMock.failReport=SH2_ROTATION_VECTOR;uint32_t start=mockNow;driver.update();
 assert(imuMock.enables==1 && mockNow-start==500);assert(!data.meta.online&&data.configurationErrors==1);
 assert(!data.derived.valid&&!data.gameDerived.valid);
 mockNow+=1000;driver.update();assert(imuMock.begins==1);
 mockNow+=4000;imuMock.failReport=-1;driver.update();assert(imuMock.begins==2&&data.meta.online&&imuMock.enables==11);
 // Raw data keep meta fresh but never constitute a working orientation stream.
 mockNow+=5001;data.meta.good(mockNow);driver.update();assert(!data.meta.online&&data.recoveryCount==1);
 mockNow+=4999;driver.update();assert(imuMock.begins==2);
 mockNow+=1;driver.update();assert(imuMock.begins==3&&data.meta.online);
 for(int i=0;i<20;++i){mockNow+=500;freshFusion(data);driver.update();assert(data.meta.online&&data.recoveryCount==1);}
 // Once-live orientation stopping must also trigger peripheral recovery.
 mockNow+=5001;data.meta.good(mockNow);driver.update();assert(!data.meta.online&&data.recoveryCount==2);
 mockNow+=5000;driver.update();assert(data.meta.online);
 // A reset that cannot reconfigure fails promptly and clears stale pose flags.
 imuMock.failReport=SH2_GYROSCOPE_CALIBRATED;imuMock.reset=true;unsigned calls=imuMock.enables;driver.update();
 assert(imuMock.enables-calls==3&&!data.meta.online);assert(!data.gameDerived.valid);
 // The five-second recovery window survives the millis() wrap.
 ImuData wrap;BNO085Sensor other(bus,wrap);imuMock.failReport=-1;mockNow=0xfffffff0;other.update();mockNow+=4999;wrap.meta.good(mockNow);other.update();assert(wrap.recoveryCount==0);mockNow+=1;wrap.meta.good(mockNow);other.update();assert(wrap.recoveryCount==1);
 puts("IMU recovery passed: fail-fast config, retry backoff, raw-only and lost fusion recovery, healthy stream, sensor reset failure and clock wrap.");
}
