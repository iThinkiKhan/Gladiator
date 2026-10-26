#pragma once
#include <Adafruit_BNO08x.h>
#include "I2CBus.h"
#include "SensorData.h"

namespace sensors {
class BNO085Sensor {
public:
  BNO085Sensor(I2CBus &bus, ImuData &data);
  void update();
private:
  bool begin();
  bool configureReports();
  void configurationFailed(uint32_t now);
  void configureCalibration();
  void maybeSaveCalibration(uint32_t now);
  static void onReport(void *cookie, sh2_SensorEvent_t *event);
  void receive(sh2_SensorEvent_t *event);
  I2CBus &bus_;
  ImuData &data_;
  Adafruit_BNO08x device_;
  uint32_t lastDiscoveryMs_ = 0,lastFusionMs_=0;
  bool tried_ = false;
  uint8_t savedAccuracy_ = 0;
};
const char *reportName(sh2_SensorId_t id);
}
