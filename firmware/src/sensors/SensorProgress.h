#pragma once
#include <stdint.h>
namespace sensors {
enum class SensorStep:uint32_t { IDLE=0, POWER=1, IMU=2, RANGE=3, PRESENCE=4, IMU_OPEN=10, IMU_REPORT=11, IMU_CAL_SET=12, IMU_CAL_GET=13, IMU_SERVICE=14, IMU_SAVE=15 };
extern volatile uint32_t sensorStep,sensorOperation;
inline void sensorProgress(SensorStep step,uint32_t operation=0){sensorOperation=operation;sensorStep=uint32_t(step);}
}
