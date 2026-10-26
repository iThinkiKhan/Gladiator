#pragma once
#include "I2CBus.h"
#include "SensorData.h"

namespace sensors {
class INA226Sensor {
public:
  INA226Sensor(I2CBus &bus, PowerData &data) : bus_(bus), data_(data) {}
  void update();
private:
  bool begin();
  I2CBus &bus_;
  PowerData &data_;
  void resetWindow();
  uint32_t lastPollMs_ = 0, lastDiscoveryMs_ = 0, fastTick_ = 0;
  uint64_t lastFastUs_ = 0;
  // Accumulators for the current POWER_INTERVAL_MS window.
  int32_t shuntSum_ = 0, shuntMax_ = 0;
  uint32_t busSum_ = 0;
  uint16_t shuntN_ = 0, busN_ = 0, busMin_ = 0xFFFF;
  // Per-window extremes for the last 20 windows (one second), newest at winIdx_ - 1.
  static constexpr uint8_t WINDOWS = 20, RECENT = 5;
  float winPeak_[WINDOWS] = {}, winMinV_[WINDOWS] = {};
  uint8_t winIdx_ = 0;
  bool tried_ = false;
};
}
