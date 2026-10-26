#pragma once
#include "INA226Sensor.h"
#include "BNO085Sensor.h"
#include "MatrixLidarSensor.h"
#include "PresenceSensor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sensors {
class SensorManager {
public:
  SensorManager();
  bool begin();
  bool snapshot(RobotSensors &out);
private:
  static void taskEntry(void *context);
  void run();
  I2CBus bus_;
  RobotSensors working_, published_;
  INA226Sensor power_;
  BNO085Sensor imu_;
  MatrixLidarSensor tof_;
  PresenceSensor presence_;
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
};
String sensorJson(const RobotSensors &data);
}
