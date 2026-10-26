#pragma once
#include <stdint.h>

// User decision 2026-10-09: mast is FRONT. CAD +Y is forward; +X is right.
// Relative to the former chassis frame: new L/R = -old R/L. No rewiring.
namespace orientation {
constexpr char NAME[] = "MAST_FRONT";
constexpr float CHASSIS_YAW_REBASE_DEG = 180.0F;
constexpr bool MOTOR_INVERTED = true;

// Re-express the existing calibration, not a new physical calibration:
// new forward uses old reverse (14%); new reverse uses old forward (22%).
// The same physical track is favored, now logical LEFT.
inline void straightTrim(int8_t &left, int8_t &right, bool overdrive) {
  if (overdrive || left != right || left == 0) return;
  const int magnitude = left > 0 ? left : -left;
  const int percent = left > 0 ? 14 : 22;
  int adjustment = (magnitude * percent + 50) / 100;
  if (adjustment < 1) adjustment = 1;
  const int boosted = magnitude + adjustment > 100 ? 100 : magnitude + adjustment;
  const int reduced = magnitude - adjustment < 0 ? 0 : magnitude - adjustment;
  const bool forward = left > 0;
  left = static_cast<int8_t>(forward ? boosted : -boosted);
  right = static_cast<int8_t>(forward ? reduced : -reduced);
}

// q_new_chassis = q_old_chassis * Rz(pi). Right multiplication changes the
// body frame, not the world heading reference. Raw sensor quaternion untouched.
inline void rebaseQuaternion(float &w, float &x, float &y, float &z) {
  const float oldW = w, oldX = x, oldY = y, oldZ = z;
  w = -oldZ; x = oldY; y = -oldX; z = oldW;
}
}
