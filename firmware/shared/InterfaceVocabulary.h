#pragma once
#include <stdint.h>

namespace interfacev1 {
inline const char *healthName(uint8_t health) {
  static const char *names[]={"UNKNOWN","INITIALIZING","OK","DEGRADED","STALE","ERROR","OFFLINE","DISABLED"};
  return health<8?names[health]:"UNKNOWN";
}
enum class State : uint8_t { UNKNOWN, SAFE, ARMED, DRIVE };
enum class Battery : uint8_t { UNKNOWN, OK, WARN, LOCKOUT, CRITICAL, BENCH };
inline const char *name(State state) {
  switch(state) {
    case State::SAFE: return "SAFE";
    case State::ARMED: return "ARMED";
    case State::DRIVE: return "DRIVE";
    default: return "UNKNOWN";
  }
}
inline const char *name(Battery state) {
  switch(state) {
    case Battery::OK: return "OK";
    case Battery::WARN: return "WARN";
    case Battery::LOCKOUT: return "LOCKOUT";
    case Battery::CRITICAL: return "CRITICAL";
    case Battery::BENCH: return "BENCH";
    default: return "UNKNOWN";
  }
}
// Called by the S3 only, using actual motor outputs rather than requested motion.
inline State controllerState(bool armed, int left, int right) {
  return !armed ? State::SAFE : (left || right) ? State::DRIVE : State::ARMED;
}
}
