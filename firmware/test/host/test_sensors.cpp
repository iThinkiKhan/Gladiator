#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "sensors/INA226Sensor.h"
#include "sensors/MatrixLidarSensor.h"
#include "sensors/PresenceSensor.h"
uint32_t fakeMillis=0;
TwoWire Wire;
using namespace sensors;
// Exercise real 4 ms acquisition ticks between the 50 ms published windows.
static void powerUntil(INA226Sensor &sensor, uint32_t until) {
  while(fakeMillis < until) { fakeMillis += 2; sensor.update(); }
}
static void powerTests() {
  Wire=TwoWire{}; fakeMillis=0;
  auto &regs=Wire.registers16[0x40]; regs[0xFE]=0x5449; regs[0xFF]=0x2260;
  regs[2]=13096; regs[1]=uint16_t(-3);
  I2CBus bus; PowerData data; INA226Sensor sensor(bus,data);
  sensor.update(); powerUntil(sensor,50);
  assert(data.meta.health==SensorHealth::OK && data.raw.busCounts==13096 && data.raw.shuntCounts==-3);
  assert(fabs(data.derived.busVolts-16.37F)<0.00001F);
  assert(fabs(data.derived.currentAmps+0.00375F)<0.000001F);
  assert(data.derived.watts<0 && data.derived.hasMinimum);
  regs[2]=12000; regs[1]=100; powerUntil(sensor,100);
  assert(data.derived.minimumVolts==15.0F && fabs(data.derived.peakAbsAmps-0.125F)<0.000001F);
  Wire.shortRead=true; powerUntil(sensor,250);
  assert(!data.meta.online && data.raw.busCounts==12000);
  Wire.shortRead=false; fakeMillis=5000; sensor.update(); powerUntil(sensor,5050);
  assert(data.meta.online && data.meta.health==SensorHealth::OK);
  assert(data.meta.errorCount==3 && data.meta.updateCount==3);
  puts("PASS power: native resolution, negative current, extrema, short reads, recovery");
}
static void powerWindowTests() {
  Wire=TwoWire{}; fakeMillis=0;
  auto &regs=Wire.registers16[0x40]; regs[0xFE]=0x5449; regs[0xFF]=0x2260;
  regs[2]=12000; regs[1]=100;
  I2CBus bus; PowerData data; INA226Sensor sensor(bus,data);
  sensor.update(); powerUntil(sensor,50);
  // A brief load peak and voltage sag must survive window averaging.
  powerUntil(sensor,62); regs[1]=1000; regs[2]=11200; powerUntil(sensor,64);
  regs[1]=100; regs[2]=12000; powerUntil(sensor,100);
  assert(data.derived.currentAmps>0.125F && data.derived.currentAmps<1.25F);
  assert(fabs(data.derived.peakAbsAmps-1.25F)<0.000001F);
  assert(fabs(data.derived.recentPeakAmps-1.25F)<0.000001F);
  assert(fabs(data.derived.min1sVolts-14.0F)<0.000001F);
  assert(data.derived.busVolts>14.0F && data.derived.busVolts<15.0F);
  // A single failed sample must not discard an otherwise usable window.
  Wire.shortRead=true; powerUntil(sensor,104); Wire.shortRead=false; powerUntil(sensor,150);
  assert(data.meta.health==SensorHealth::OK && data.meta.errorCount==0);
  powerUntil(sensor,1200);
  assert(fabs(data.derived.recentPeakAmps-0.125F)<0.000001F);
  assert(fabs(data.derived.peak1sAmps-0.125F)<0.000001F);
  assert(fabs(data.derived.min1sVolts-15.0F)<0.000001F);
  assert(fabs(data.derived.peakAbsAmps-1.25F)<0.000001F);
  puts("PASS power windows: averaging, brief current peaks/voltage sags, transient read failure, peak expiry");
}
static void healthTests() {
  SensorMeta m; m.initialized=true; m.good(0xFFFFFFF0);
  m.age(20,100,1000); assert(m.health==SensorHealth::OK);
  m.age(150,100,1000); assert(m.health==SensorHealth::STALE);
  m.age(1100,100,1000); assert(m.health==SensorHealth::OFFLINE && !m.online);
  m.good(1200); assert(m.health==SensorHealth::OK && m.online && m.errorCount==0);
  m.error(1201); m.error(1202); m.error(1203);
  assert(m.health==SensorHealth::OFFLINE && m.errorCount==3 && !m.online);
  m.good(1300); assert(m.consecutiveErrors==0 && m.errorCount==3);
  puts("PASS health: rollover, stale/offline, errors, recovery");
}
static void matrixTests() {
  Wire=TwoWire{}; fakeMillis=0;
  Wire.streams[0x33]={0x53,1,0,0};
  I2CBus bus; ToFData data; MatrixLidarSensor sensor(bus,data);
  sensor.update(); assert(Wire.writes.back()==std::vector<uint8_t>({0x55,0,5,1,0,0,0,8}));
  fakeMillis=5; sensor.update();
  fakeMillis=5004; sensor.update(); assert(!data.meta.hasSample);
  fakeMillis=5005; sensor.update();
  auto &stream=Wire.streams[0x33]; stream={0x53,2,128,0};
  for(unsigned i=0;i<64;++i) { uint16_t v=i==37?612:1000+i; stream.push_back(v&255);stream.push_back(v>>8); }
  fakeMillis=5010; sensor.update();
  assert(data.meta.health==SensorHealth::OK && data.raw.distanceMm[37]==612);
  assert(data.raw.distanceMm[63]==1063 && data.derived.nearestZone==37 && data.derived.usableZones==64);
  // 4000 is the no-return sentinel, not a 4 m measurement: it must be retained
  // raw but excluded from the derived usable count and from nearest.
  stream={0x53,2,128,0};
  for(unsigned i=0;i<64;++i) { uint16_t v=i<2?4000:(i==37?612:1000+i); stream.push_back(v&255);stream.push_back(v>>8); }
  fakeMillis=5077; sensor.update();
  fakeMillis=5082; sensor.update();
  assert(data.raw.distanceMm[0]==4000 && data.raw.distanceMm[1]==4000);
  assert(data.derived.usableZones==62 && data.derived.nearestZone==37 && data.derived.nearestMm==612);
  const auto old=data.raw.receivedUs;
  fakeMillis=5150; sensor.update();
  stream={0x53,2,255,255}; fakeMillis=5155; sensor.update();
  assert(data.protocolErrors==1 && data.raw.receivedUs==old && !data.derived.valid);
  puts("PASS matrix: nonblocking startup, 64 zones, nearest, oversized payload rejected without overwriting evidence");
}
static void presenceTests() {
  Wire=TwoWire{}; fakeMillis=0;
  auto &regs=Wire.registers[0x2A]; regs[0]=0x81; regs[3]=3; regs[0x10]=1;
  I2CBus bus; PresenceData data; PresenceSensor sensor(bus,data);
  sensor.update(); fakeMillis=200; sensor.update();
  assert(data.meta.health==SensorHealth::OK && data.derived.presenceValid && data.derived.present);
  assert(!data.derived.targetValid && Wire.writes.empty());
  regs[0]=0x83; regs[0x11]=0x1C; regs[0x12]=1; // 2.84m
  regs[0x13]=0x85; regs[0x14]=0xFF; // -1.23m/s
  regs[0x15]=0xCD; regs[0x16]=0xAB;
  fakeMillis=400; sensor.update();
  assert(!data.derived.presenceValid && data.derived.targetValid);
  assert(fabs(data.derived.rangeMeters-2.84F)<0.0001F && fabs(data.derived.speedMetersPerSecond+1.23F)<0.0001F);
  assert(data.derived.energy==0xABCD);
  regs[0x10]=0; fakeMillis=600; sensor.update(); assert(!data.derived.targetValid);
  const auto old=data.raw.receivedUs; Wire.shortRead=true;
  fakeMillis=800; sensor.update(); assert(data.meta.health==SensorHealth::ERROR && data.raw.receivedUs==old);
  fakeMillis=1000; sensor.update(); fakeMillis=1200; sensor.update(); assert(!data.meta.online);
  Wire.shortRead=false; fakeMillis=6000; sensor.update(); fakeMillis=6200; sensor.update();
  assert(data.meta.online && data.meta.health==SensorHealth::OK);
  puts("PASS presence: mode distinction, signed velocity, no target latch, short reads, recovery");
}
static void headingTests() {
  ImuData imu;auto &r=imu.reports[IMU_ROTATION_INDEX];
  imu.meta.health=SensorHealth::OK;imu.derived.valid=true;r.hasSample=true;r.lastUpdateMs=100;r.native.status=1;
  assert(!headingValid(imu,100));r.native.status=2;assert(headingValid(imu,400));assert(!headingValid(imu,401));
  imu.meta.health=SensorHealth::ERROR;assert(!headingValid(imu,100));imu.meta.health=SensorHealth::OK;
  r.hasSample=false;assert(!headingValid(imu,100));r.hasSample=true;imu.derived.valid=false;assert(!headingValid(imu,100));
  imu.derived.valid=true;r.lastUpdateMs=0xfffffff0;assert(headingValid(imu,20));
  puts("PASS heading: S3 accuracy, report freshness, health, validity and clock wrap");
}
int main() { healthTests(); powerTests(); powerWindowTests(); matrixTests(); presenceTests(); headingTests(); }
