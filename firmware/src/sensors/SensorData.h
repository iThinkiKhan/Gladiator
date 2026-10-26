#pragma once
#include "SensorHealth.h"
#include <sh2_SensorValue.h>

namespace sensors {
struct PowerData {
  SensorMeta meta;
  struct { uint16_t busCounts = 0; int16_t shuntCounts = 0; uint64_t receivedUs = 0; } raw;
  struct {
    float busVolts = 0, shuntVolts = 0, currentAmps = 0, watts = 0;
    float minimumVolts = 0, peakAbsAmps = 0;
    // True peak of any single reading (not the 50 ms average) over sliding windows, and the
    // lowest single bus reading over the last second: the sag under load.
    float recentPeakAmps = 0, peak1sAmps = 0, min1sVolts = 0;
    bool hasMinimum = false;
  } derived;
};
constexpr size_t IMU_REPORT_COUNT = 10;
// Fixed positions inside ImuData::reports, set by the driver's id table.
constexpr size_t IMU_ROTATION_INDEX = 0; // SH2_ROTATION_VECTOR, needs the magnetometer
constexpr size_t IMU_GYRO_INDEX = 2;     // SH2_GYROSCOPE_CALIBRATED
constexpr size_t IMU_GAME_INDEX = 9;     // SH2_GAME_ROTATION_VECTOR, no magnetometer
struct ImuReport {
  sh2_SensorId_t id = SH2_ROTATION_VECTOR;
  uint32_t intervalUs = 10000;
  bool enabled = false, hasSample = false, sequenceKnown = false;
  uint32_t count = 0, missedReports = 0, lastUpdateMs = 0;
  uint64_t receivedUs = 0;
  // Keep the complete native decoded report, including status, delay and timestamp.
  sh2_SensorValue_t native = {};
};
struct ImuData {
  SensorMeta meta;
  ImuReport reports[IMU_REPORT_COUNT];
  uint32_t resetCount = 0, configurationErrors = 0, decodeErrors = 0,recoveryCount=0;
  sh2_ProductIds_t product = {};
  struct { bool valid = false; float rollDeg = 0, pitchDeg = 0, yawDeg = 0; } derived;
  // Game rotation vector: gyro + accelerometer only. Absolute roll and pitch,
  // relative yaw that drifts slowly. Usable beside motor magnets, where the
  // magnetometer-backed rotation vector above may never calibrate.
  struct { bool valid = false; float rollDeg = 0, pitchDeg = 0, yawDeg = 0; } gameDerived;
  // The SH2 default calibration set leaves gyro and magnetometer dynamic
  // calibration disabled, so their accuracy bytes stay 0 no matter how much
  // the robot moves. Requested/active are the sh2 SH2_CAL_* bitmasks.
  struct {
    uint8_t requested = 0, active = 0, savedAccuracy = 0;
    bool known = false;
    uint32_t configErrors = 0, saves = 0, saveErrors = 0;
  } calibration;
};
// Compass validity is decided on S3, including the native report accuracy.
inline bool headingValid(const ImuData &imu, uint32_t now) {
  const auto &r=imu.reports[IMU_ROTATION_INDEX];
  return imu.derived.valid && imu.meta.health==SensorHealth::OK && r.hasSample &&
         uint32_t(now-r.lastUpdateMs)<=300 && (r.native.status & 3)>=2;
}
struct ToFData {
  SensorMeta meta;
  struct {
    // SEN0628's RP2040 protocol exposes only 64 unsigned distances.
    uint16_t distanceMm[64] = {};
    uint8_t responseStatus = 0, responseCommand = 0;
    uint64_t receivedUs = 0;
  } raw;
  struct { bool valid=false; uint16_t nearestMm=0; uint8_t nearestZone=0, usableZones=0; } derived;
  uint32_t protocolErrors=0;
};
struct PresenceData {
  SensorMeta meta;
  struct {
    uint8_t status=0, firmwareVersion=0;
    uint8_t result[7]={}; // 0x10..0x16; interpretation depends on workMode.
    uint8_t configuration[11]={}; // 0x20..0x2A, preserved without rewriting.
    uint64_t receivedUs=0;
  } raw;
  struct {
    bool running=false, initialized=false, speedMode=false;
    bool presenceValid=false, present=false, targetValid=false;
    uint8_t targetCount=0;
    float rangeMeters=0, speedMetersPerSecond=0;
    uint16_t energy=0;
  } derived;
};
struct RobotSensors {
  PowerData power;
  ImuData imu;
  ToFData tofFront;
  PresenceData presence;
  bool busStarted = false;
  // I2C bus clears (stuck SDA/SCL), and sensor-task hangs that reset the S3 in a row.
  uint32_t busStuck = 0, busRecoveries = 0, hangResets = 0;
  bool busWatchdog = true;
  uint64_t snapshotUs = 0;
  uint32_t workerMaxUs = 0;
};
}
