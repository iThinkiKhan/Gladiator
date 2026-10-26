#include "INA226Sensor.h"
#include <esp_timer.h>
#include <math.h>

namespace sensors {
bool INA226Sensor::begin() {
  auto &m = data_.meta;
  m.lastAttemptMs = millis();
  m.health = SensorHealth::INITIALIZING;
  for (uint8_t a = 0x40; a <= 0x4F; ++a) {
    // 0x4A/B are reserved for BNO08x: register-style probes could consume SHTP data.
    if (a == 0x4A || a == 0x4B || !bus_.present(a)) continue;
    uint16_t manufacturer = 0, device = 0;
    if (!bus_.read16(a, 0xFE, manufacturer) || manufacturer != 0x5449 ||
        !bus_.read16(a, 0xFF, device) || (device & 0xFFF0) != 0x2260) continue;
    m.address = a;
    if (!bus_.write16(a, 0, board::INA226_CONFIG)) break;
    m.initialized = m.online = true;
    m.hasSample = false;
    m.consecutiveErrors = 0;
    lastPollMs_ = millis();
    resetWindow();
    for (uint8_t i = 0; i < WINDOWS; ++i) { winPeak_[i] = 0; winMinV_[i] = 99.0F; }
    return true;
  }
  m.error(millis()); m.online = false; m.health = SensorHealth::OFFLINE;
  return false;
}
void INA226Sensor::resetWindow() {
  shuntSum_ = shuntMax_ = 0;
  busSum_ = 0;
  shuntN_ = busN_ = 0;
  busMin_ = 0xFFFF;
}

void INA226Sensor::update() {
  const uint32_t now = millis();
  auto &m = data_.meta;
  m.age(now, 250, 1500);
  if (!m.online) {
    if (!tried_ || now - lastDiscoveryMs_ >= board::SENSOR_RETRY_MS) {
      tried_ = true; lastDiscoveryMs_ = now; begin();
    }
    return;
  }

  // Fast path: read the shunt every POWER_FAST_US and keep every reading. A failed read is only
  // counted if the whole window ends with nothing usable, so an I2C glitch cannot take the
  // monitor offline any faster than it could at the old 50 ms cadence.
  const uint64_t nowUs = esp_timer_get_time();
  if (nowUs - lastFastUs_ >= board::POWER_FAST_US) {
    lastFastUs_ = nowUs;
    uint16_t value = 0;
    if (bus_.read16(m.address, 1, value)) {
      const int32_t counts = static_cast<int16_t>(value);
      shuntSum_ += counts;
      ++shuntN_;
      const int32_t magnitude = counts < 0 ? -counts : counts;
      if (magnitude > shuntMax_) shuntMax_ = magnitude;
      if ((++fastTick_ & 3) == 0 && bus_.read16(m.address, 2, value)) {
        busSum_ += value;
        ++busN_;
        if (value < busMin_) busMin_ = value;
      }
    }
  }

  if (now - lastPollMs_ < board::POWER_INTERVAL_MS) return;
  lastPollMs_ = m.lastAttemptMs = now;
  if (!shuntN_ || !busN_) { resetWindow(); m.error(now); return; }

  const float shuntCounts = static_cast<float>(shuntSum_) / shuntN_;
  const uint16_t bus = static_cast<uint16_t>(busSum_ / busN_);
  const float peakAmps = shuntMax_ * 0.0000025F / board::SHUNT_OHMS;
  const float lowestVolts = busMin_ * 0.00125F;
  resetWindow();

  data_.raw.busCounts = bus;
  data_.raw.shuntCounts = static_cast<int16_t>(lroundf(shuntCounts));
  data_.raw.receivedUs = esp_timer_get_time();
  auto &d = data_.derived;
  d.busVolts = bus * 0.00125F;
  d.shuntVolts = shuntCounts * 0.0000025F;
  d.currentAmps = d.shuntVolts / board::SHUNT_OHMS;
  d.watts = d.busVolts * d.currentAmps;
  if (d.busVolts > 0 && (!d.hasMinimum || d.busVolts < d.minimumVolts)) {
    d.minimumVolts = d.busVolts; d.hasMinimum = true;
  }
  d.peakAbsAmps = fmaxf(d.peakAbsAmps, peakAmps);

  winPeak_[winIdx_] = peakAmps;
  winMinV_[winIdx_] = lowestVolts;
  winIdx_ = (winIdx_ + 1) % WINDOWS;
  float recent = 0, whole = 0, lowest = 99.0F;
  for (uint8_t age = 0; age < WINDOWS; ++age) {
    const uint8_t slot = (winIdx_ + WINDOWS - 1 - age) % WINDOWS;
    if (winPeak_[slot] > whole) whole = winPeak_[slot];
    if (age < RECENT && winPeak_[slot] > recent) recent = winPeak_[slot];
    if (winMinV_[slot] > 0 && winMinV_[slot] < lowest) lowest = winMinV_[slot];
  }
  d.recentPeakAmps = recent;
  d.peak1sAmps = whole;
  d.min1sVolts = lowest < 99.0F ? lowest : 0;
  m.good(now);
}
}
