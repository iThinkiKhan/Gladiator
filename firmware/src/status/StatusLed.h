#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include "../BoardConfig.h"

namespace status {

// Highest priority first: the first true condition owns the light. Motion
// states outrank link states because they describe what the chassis can do,
// not what a phone can see.
enum class Condition : uint8_t {
  BOOT_CODE,    // announcing the previous reset reason
  BATTERY,      // red triple-blink: pack too low, motors locked out
  DRIVING,      // amber solid: outputs are live
  ARMED,        // amber 2 Hz: disarmed only by command or watchdog
  AP_DOWN,      // red 1 Hz: maintenance AP is not running
  SENSOR_FAULT, // magenta double-blink: bus or power monitor lost
  BLE_LINK,     // blue solid
  WEB_LINK,     // cyan solid
  IDLE,         // green breathe: AP up, safe, nobody connected
};
const char *conditionName(Condition condition);
const char *resetReasonName(esp_reset_reason_t reason);

struct Inputs {
  bool batteryLockout = false;
  bool apUp = false;
  bool webClient = false;
  bool bleConnected = false;
  bool armed = false;
  bool driving = false;
  bool sensorFault = false;
};

// Drives the onboard WS2812. Every call is nonblocking apart from the ~30 us
// RMT transfer, which is rate limited and only issued when the colour changes.
class StatusLed {
public:
  // Blinks the previous reset reason in red before live status takes over. A
  // controller that keeps resetting never finishes the announcement, so a
  // repeating code is itself the boot-loop signature.
  void begin(esp_reset_reason_t reason, uint32_t rawReason, uint32_t bootCount);
  void update(const Inputs &in, uint32_t nowMs);
  Condition condition() const { return condition_; }
  bool announcing() const { return announcing_; }
  uint8_t code() const { return code_; }
  // 0 means an ordinary reset that needs no announcement.
  static uint8_t blinkCode(esp_reset_reason_t reason, uint32_t rawReason);

private:
  void show(uint8_t r, uint8_t g, uint8_t b, uint32_t nowMs);
  uint32_t startedMs_ = 0, lastWriteMs_ = 0;
  uint8_t code_ = 0;
  // Seeded to an unreachable value so the first update always writes.
  uint8_t red_ = 0xFF, green_ = 0xFF, blue_ = 0xFF;
  bool announcing_ = false, begun_ = false;
  Condition condition_ = Condition::BOOT_CODE;
};

} // namespace status
