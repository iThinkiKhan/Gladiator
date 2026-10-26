#pragma once
#include "I2CBus.h"
#include "SensorData.h"

namespace sensors {
class PresenceSensor {
public:
  PresenceSensor(I2CBus &bus, PresenceData &data):bus_(bus),data_(data) {}
  void update();
private:
  I2CBus &bus_;
  PresenceData &data_;
  uint32_t lastPollMs_=0,lastDiscoveryMs_=0;
  bool tried_=false;
};
}
