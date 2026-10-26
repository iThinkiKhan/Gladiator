#pragma once
#include "I2CBus.h"
#include "SensorData.h"

namespace sensors {
// SEN0628 protocol from DFRobot_MatrixLidar, implemented as a cooperative
// transaction state machine instead of the vendor's 5s/8s blocking waits.
class MatrixLidarSensor {
public:
  MatrixLidarSensor(I2CBus &bus, ToFData &data): bus_(bus), data_(data) {}
  void update();
private:
  enum class State { DISCOVER, RESPONSE, SETTLE, SAMPLE };
  void fail(bool protocol=false);
  bool command(uint8_t id);
  bool response();
  I2CBus &bus_;
  ToFData &data_;
  State state_=State::DISCOVER;
  uint32_t stateMs_=0, lastPollMs_=0;
  bool tried_=false;
  uint8_t command_=0, status_=0;
};
}
