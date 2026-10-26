#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include "../BoardConfig.h"

namespace sensors {
// Only the sensor worker owns Wire. UI/control code uses copied snapshots.
//
// A read that times out part-way can leave a sensor in the middle of a byte,
// driving SDA low until it sees more clocks. The ESP-IDF 4.4 I2C driver can then
// wait forever inside i2c_master_cmd_begin, which trips the task watchdog; a
// software reset does not power-cycle the sensors, so the next boot hangs the same
// way. Every transaction therefore checks that both lines are idle first, and
// clocks a stuck sensor free (9+ SCL pulses and a STOP) before touching the driver.
class I2CBus {
public:
  bool begin() {
    clearBus();   // a stuck sensor survives the S3's own reset
    return start();
  }
  // Wire leaves its open-drain pins with input enabled, so this reads the wires.
  bool idle() const {
    return gpio_get_level(gpio_num_t(board::I2C_SDA)) && gpio_get_level(gpio_num_t(board::I2C_SCL));
  }
  // True when the bus is idle, recovering it first if a line is held low.
  bool ensureIdle() {
    if (idle()) return true;
    ++stuck_;
    const int sda = gpio_get_level(gpio_num_t(board::I2C_SDA)), scl = gpio_get_level(gpio_num_t(board::I2C_SCL));
    Wire.end();
    const bool cleared = clearBus();
    start();
    if (cleared) ++recovered_;
    ets_printf("I2C bus stuck (SDA=%d SCL=%d): %s\n", sda, scl, cleared ? "cleared" : "still held low");
    return cleared && idle();
  }
  uint32_t stuckEvents() const { return stuck_; }
  uint32_t recoveries() const { return recovered_; }

  bool present(uint8_t address) {
    if (!ensureIdle()) return false;
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }
  bool read(uint8_t address, uint8_t *data, uint8_t size) {
    if (!ensureIdle()) return false;
    if (Wire.requestFrom(address, size) != size) return false;
    for (uint8_t i=0; i<size; ++i) data[i]=Wire.read();
    return true;
  }
  bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data, uint8_t size) {
    if (!ensureIdle()) return false;
    Wire.beginTransmission(address); Wire.write(reg);
    return Wire.endTransmission(false)==0 && read(address,data,size);
  }
  bool write(uint8_t address, const uint8_t *data, uint8_t size) {
    if (!ensureIdle()) return false;
    Wire.beginTransmission(address); Wire.write(data,size);
    return Wire.endTransmission()==0;
  }
  bool read16(uint8_t address, uint8_t reg, uint16_t &value) {
    if (!ensureIdle()) return false;
    Wire.beginTransmission(address); Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, uint8_t(2)) != 2) return false;
    value = uint16_t(Wire.read()) << 8;
    value |= uint16_t(Wire.read());
    return true;
  }
  bool write16(uint8_t address, uint8_t reg, uint16_t value) {
    if (!ensureIdle()) return false;
    Wire.beginTransmission(address); Wire.write(reg);
    Wire.write(uint8_t(value >> 8)); Wire.write(uint8_t(value));
    return Wire.endTransmission() == 0;
  }
  TwoWire &wire() { return Wire; }

private:
  bool start() {
    const bool ok = Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    Wire.setTimeOut(board::I2C_TIMEOUT_MS);
    return ok;
  }
  // Standard I2C bus clear, bit-banged with the I2C peripheral released.
  static bool clearBus() {
    const uint8_t sda = board::I2C_SDA, scl = board::I2C_SCL;
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, OUTPUT_OPEN_DRAIN); digitalWrite(scl, HIGH);
    delayMicroseconds(10);
    // Nine clocks finish any byte a sensor is sending; allow two bytes' worth.
    for (int i = 0; i < 18 && !digitalRead(sda); ++i) {
      digitalWrite(scl, LOW); delayMicroseconds(10);
      digitalWrite(scl, HIGH); delayMicroseconds(10);
      for (int w = 0; w < 100 && !digitalRead(scl); ++w) delayMicroseconds(10); // clock stretch, max 1 ms
    }
    // STOP: SDA rises while SCL is high, leaving every sensor idle.
    pinMode(sda, OUTPUT_OPEN_DRAIN); digitalWrite(sda, LOW); delayMicroseconds(10);
    digitalWrite(scl, HIGH); delayMicroseconds(10);
    digitalWrite(sda, HIGH); delayMicroseconds(10);
    pinMode(sda, INPUT_PULLUP); pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(10);
    return digitalRead(sda) && digitalRead(scl);
  }
  uint32_t stuck_ = 0, recovered_ = 0;
};
}
