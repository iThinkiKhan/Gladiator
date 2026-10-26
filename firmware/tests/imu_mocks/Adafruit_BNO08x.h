#pragma once
#include "Arduino.h"
#include "Wire.h"
#include <sh2.h>
#include <sh2_err.h>
#include <sh2_SensorValue.h>
struct ImuMock {unsigned begins=0,enables=0;int failReport=-1;bool reset=false;};
extern ImuMock imuMock;extern uint32_t mockNow;
class Adafruit_BNO08x {public:sh2_ProductIds_t prodIds={};explicit Adafruit_BNO08x(int8_t){}bool begin_I2C(uint8_t,TwoWire*){++imuMock.begins;return true;}bool wasReset(){bool r=imuMock.reset;imuMock.reset=false;return r;}bool enableReport(sh2_SensorId_t id,uint32_t){++imuMock.enables;if(int(id)==imuMock.failReport){mockNow+=500;return false;}return true;}};
