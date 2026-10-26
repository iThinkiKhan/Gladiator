#include "SensorManager.h"
#include <ArduinoJson.h>
#include <esp_timer.h>

namespace sensors {
namespace {
void metaJson(JsonObject out, const SensorMeta &m, const char *id, const char *type) {
  out["id"]=id; out["type"]=type; out["enabled"]=m.enabled;
  out["initialized"]=m.initialized; out["online"]=m.online; out["hasSample"]=m.hasSample;
  out["health"]=healthName(m.health);
  if (m.address) out["address"]=m.address; else out["address"]=nullptr;
  out["lastAttemptMs"]=m.lastAttemptMs; out["lastUpdateMs"]=m.lastUpdateMs;
  out["lastGoodUpdateMs"]=m.lastGoodUpdateMs;
  if (m.hasSample) out["ageMs"]=uint32_t(millis()-m.lastUpdateMs); else out["ageMs"]=nullptr;
  out["updateCount"]=m.updateCount; out["errorCount"]=m.errorCount;
  out["consecutiveErrors"]=m.consecutiveErrors; out["rateHz"]=m.rateHz;
}
template<typename T> void vectorJson(JsonObject out, const T &v) { out["x"]=v.x; out["y"]=v.y; out["z"]=v.z; }
void reportJson(JsonObject out, const ImuReport &report) {
  out["reportId"]=int(report.id); out["enabled"]=report.enabled;
  out["requestedIntervalUs"]=report.intervalUs; out["hasSample"]=report.hasSample;
  out["count"]=report.count; out["missedReports"]=report.missedReports;
  const bool fresh = report.hasSample && millis()-report.lastUpdateMs<=300;
  out["fresh"]=fresh;
  if (!report.hasSample) { out["native"]=nullptr; return; }
  out["ageMs"]=uint32_t(millis()-report.lastUpdateMs);
  out["receivedUs"]=report.receivedUs;
  const auto &v=report.native;
  auto native=out["native"].to<JsonObject>();
  native["sensorTimestampUs"]=v.timestamp; native["sequence"]=v.sequence;
  native["status"]=v.status; native["accuracy"]=v.status & 3; native["delay"]=v.delay;
  switch (report.id) {
    case SH2_ROTATION_VECTOR:
      native["i"]=v.un.rotationVector.i; native["j"]=v.un.rotationVector.j;
      native["k"]=v.un.rotationVector.k; native["real"]=v.un.rotationVector.real;
      native["accuracyRad"]=v.un.rotationVector.accuracy; break;
    case SH2_GAME_ROTATION_VECTOR:
      native["i"]=v.un.gameRotationVector.i; native["j"]=v.un.gameRotationVector.j;
      native["k"]=v.un.gameRotationVector.k; native["real"]=v.un.gameRotationVector.real;
      // No magnetometer in this fusion, so the report carries no accuracy estimate.
      native["units"]="quaternion, no magnetometer"; break;
    case SH2_ACCELEROMETER: vectorJson(native,v.un.accelerometer); native["units"]="m/s^2"; break;
    case SH2_GYROSCOPE_CALIBRATED: vectorJson(native,v.un.gyroscope); native["units"]="rad/s"; break;
    case SH2_MAGNETIC_FIELD_CALIBRATED: vectorJson(native,v.un.magneticField); native["units"]="uT"; break;
    case SH2_LINEAR_ACCELERATION: vectorJson(native,v.un.linearAcceleration); native["units"]="m/s^2"; break;
    case SH2_GRAVITY: vectorJson(native,v.un.gravity); native["units"]="m/s^2"; break;
    case SH2_RAW_ACCELEROMETER:
      vectorJson(native,v.un.rawAccelerometer); native["units"]="ADC counts";
      native["rawTimestampUs"]=v.un.rawAccelerometer.timestamp; break;
    case SH2_RAW_GYROSCOPE:
      vectorJson(native,v.un.rawGyroscope); native["units"]="ADC counts";
      native["rawTimestampUs"]=v.un.rawGyroscope.timestamp;
      native["temperatureCounts"]=v.un.rawGyroscope.temperature; break;
    case SH2_RAW_MAGNETOMETER:
      vectorJson(native,v.un.rawMagnetometer); native["units"]="ADC counts";
      native["rawTimestampUs"]=v.un.rawMagnetometer.timestamp; break;
    default: break;
  }
}
}
String sensorJson(const RobotSensors &data) {
  JsonDocument doc;
  doc["schemaVersion"]=1; doc["firmware"]="Gladiator Sensor Core v1";
  doc["uptimeUs"]=uint64_t(esp_timer_get_time()); doc["snapshotUs"]=data.snapshotUs;
  auto bus=doc["bus"].to<JsonObject>();
  bus["started"]=data.busStarted; bus["sda"]=board::I2C_SDA; bus["scl"]=board::I2C_SCL;
  bus["clockHz"]=board::I2C_HZ; bus["timeoutMs"]=board::I2C_TIMEOUT_MS;
  bus["stuck"]=data.busStuck; bus["recoveries"]=data.busRecoveries;
  bus["hangResets"]=data.hangResets; bus["watchdog"]=data.busWatchdog;
  doc["workerMaxUs"]=data.workerMaxUs;
  auto power=doc["power"].to<JsonObject>();
  metaJson(power["meta"].to<JsonObject>(),data.power.meta,"POWER_MAIN","INA226");
  power["configuration"]["shuntOhms"]=board::SHUNT_OHMS;
  power["configuration"]["register"]=board::INA226_CONFIG;
  power["configuration"]["sampleIntervalMs"]=board::POWER_INTERVAL_MS;
  power["configuration"]["hardwareAverages"]=16;
  if (data.power.meta.hasSample) {
    auto raw=power["raw"].to<JsonObject>();
    raw["busCounts"]=data.power.raw.busCounts; raw["shuntCounts"]=data.power.raw.shuntCounts;
    raw["receivedUs"]=data.power.raw.receivedUs;
    const auto &p=data.power.derived;
    auto derived=power["derived"].to<JsonObject>();
    derived["busVolts"]=p.busVolts; derived["shuntVolts"]=p.shuntVolts;
    derived["currentAmps"]=p.currentAmps; derived["watts"]=p.watts;
    if (p.hasMinimum) derived["minimumVolts"]=p.minimumVolts; else derived["minimumVolts"]=nullptr;
    derived["peakAbsAmps"]=p.peakAbsAmps;
    derived["recentPeakAmps"]=p.recentPeakAmps; derived["peak1sAmps"]=p.peak1sAmps; derived["min1sVolts"]=p.min1sVolts;
  } else { power["raw"]=nullptr; power["derived"]=nullptr; }
  auto imu=doc["imu"].to<JsonObject>();
  metaJson(imu["meta"].to<JsonObject>(),data.imu.meta,"IMU_BODY","BNO085");
  imu["resetCount"]=data.imu.resetCount; imu["recoveryCount"]=data.imu.recoveryCount; imu["configurationErrors"]=data.imu.configurationErrors;
  imu["decodeErrors"]=data.imu.decodeErrors;
  auto products=imu["productIds"].to<JsonArray>();
  for (uint8_t i=0; i<data.imu.product.numEntries; ++i) {
    auto item=products.add<JsonObject>(); const auto &p=data.imu.product.entry[i];
    item["partNumber"]=p.swPartNumber; item["major"]=p.swVersionMajor;
    item["minor"]=p.swVersionMinor; item["patch"]=p.swVersionPatch;
    item["build"]=p.swBuildNumber; item["resetCause"]=p.resetCause;
  }
  for (const auto &report:data.imu.reports) reportJson(imu["reports"][reportName(report.id)].to<JsonObject>(),report);
  auto calibration=imu["calibration"].to<JsonObject>();
  const auto &cal=data.imu.calibration;
  calibration["requested"]=cal.requested;
  if (cal.known) calibration["active"]=cal.active; else calibration["active"]=nullptr;
  calibration["accel"]=bool(cal.active & SH2_CAL_ACCEL);
  calibration["gyro"]=bool(cal.active & SH2_CAL_GYRO);
  calibration["magnetometer"]=bool(cal.active & SH2_CAL_MAG);
  calibration["configErrors"]=cal.configErrors;
  calibration["saves"]=cal.saves; calibration["saveErrors"]=cal.saveErrors;
  calibration["savedAccuracy"]=cal.savedAccuracy;
  calibration["note"]="Dynamic calibration bitmask actually in effect, read back from the device.";
  auto mounting=imu["mounting"].to<JsonObject>();
  mounting["applied"]=true;
  mounting["rollDeg"]=board::IMU_MOUNT_ROLL_DEG;
  mounting["pitchDeg"]=board::IMU_MOUNT_PITCH_DEG;
  mounting["yawDeg"]=nullptr;
  mounting["note"]="Both derived blocks are in chassis frame: the mount tilt is removed from a copy. Native quaternions are unmodified. Yaw offset is not observable from gravity and is not corrected.";
  imu["derived"]["valid"]=data.imu.derived.valid;
  imu["derived"]["headingValid"]=headingValid(data.imu,millis());
  if (data.imu.derived.valid) {
    imu["derived"]["rollDeg"]=data.imu.derived.rollDeg;
    imu["derived"]["pitchDeg"]=data.imu.derived.pitchDeg;
    imu["derived"]["yawDeg"]=data.imu.derived.yawDeg;
  }
  imu["derived"]["source"]="rotationVector";
  imu["derived"]["frame"]="chassis";
  imu["derived"]["note"]="Magnetometer-backed, mount tilt removed. Yaw is unusable until magnetometer accuracy reaches 2.";
  auto game=imu["gameDerived"].to<JsonObject>();
  game["valid"]=data.imu.gameDerived.valid;
  if (data.imu.gameDerived.valid) {
    game["rollDeg"]=data.imu.gameDerived.rollDeg;
    game["pitchDeg"]=data.imu.gameDerived.pitchDeg;
    game["yawDeg"]=data.imu.gameDerived.yawDeg;
  }
  game["source"]="gameRotationVector";
  game["frame"]="chassis";
  game["note"]="Gyro and accelerometer only, mount tilt removed. Absolute roll/pitch; yaw is relative and drifts, not a compass bearing.";
  auto tof=doc["tofFront"].to<JsonObject>();
  metaJson(tof["meta"].to<JsonObject>(),data.tofFront.meta,"TOF_FRONT","SEN0628");
  tof["capabilities"]["zones"]=64;
  tof["capabilities"]["zoneStatus"]=false; tof["capabilities"]["signalStrength"]=false;
  tof["protocolErrors"]=data.tofFront.protocolErrors;
  tof["note"]="Native 8x8 distances in device order; status and signal are not exposed by the SEN0628 interface.";
  if(data.tofFront.meta.hasSample) {
    auto distances=tof["raw"]["distanceMm"].to<JsonArray>();
    for(auto mm:data.tofFront.raw.distanceMm) distances.add(mm);
    tof["raw"]["receivedUs"]=data.tofFront.raw.receivedUs;
    tof["raw"]["responseStatus"]=data.tofFront.raw.responseStatus;
    tof["raw"]["responseCommand"]=data.tofFront.raw.responseCommand;
  } else tof["raw"]=nullptr;
  tof["derived"]["valid"]=data.tofFront.derived.valid;
  tof["derived"]["usableZones"]=data.tofFront.derived.usableZones;
  if(data.tofFront.derived.valid) {
    tof["derived"]["nearestMm"]=data.tofFront.derived.nearestMm;
    tof["derived"]["nearestZone"]=data.tofFront.derived.nearestZone;
  }
  auto presence=doc["presence"].to<JsonObject>();
  metaJson(presence["meta"].to<JsonObject>(),data.presence.meta,"PRESENCE_FRONT","C4001 SEN0610");
  presence["note"]="Existing mode and sensitivity retained; target data and presence have separate validity.";
  if(data.presence.meta.hasSample) {
    const auto &r=data.presence.raw; const auto &d=data.presence.derived;
    presence["raw"]["status"]=r.status; presence["raw"]["firmwareVersion"]=r.firmwareVersion;
    presence["raw"]["receivedUs"]=r.receivedUs;
    auto result=presence["raw"]["resultRegisters"].to<JsonArray>();
    for(auto value:r.result) result.add(value);
    auto config=presence["raw"]["configurationRegisters"].to<JsonArray>();
    for(auto value:r.configuration) config.add(value);
    auto derived=presence["derived"].to<JsonObject>();
    derived["running"]=d.running; derived["initialized"]=d.initialized;
    derived["mode"]=d.speedMode?"SPEED":"PRESENCE";
    derived["presenceValid"]=d.presenceValid; derived["targetValid"]=d.targetValid;
    if(d.presenceValid) derived["present"]=d.present; else derived["present"]=nullptr;
    if(d.targetValid) {
      derived["targetCount"]=d.targetCount; derived["rangeMeters"]=d.rangeMeters;
      derived["speedMetersPerSecond"]=d.speedMetersPerSecond; derived["energy"]=d.energy;
    }
  } else { presence["raw"]=nullptr; presence["derived"]=nullptr; }
  String result; result.reserve(measureJson(doc)+1); serializeJson(doc,result); return result;
}
}
