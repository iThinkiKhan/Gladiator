#include "../src/Orientation.h"
#include "../src/BoardConfig.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

static_assert(board::LEFT_R_EN == 2 && board::LEFT_L_EN == 2 &&
              board::LEFT_RPWM == 42 && board::LEFT_LPWM == 41 && !board::LEFT_SEPARATE_ENABLES, "new left wiring");
static_assert(board::RIGHT_R_EN == 16 && board::RIGHT_L_EN == 11 &&
              board::RIGHT_RPWM == 17 && board::RIGHT_LPWM == 12 && board::RIGHT_SEPARATE_ENABLES, "new right wiring");
static_assert(orientation::MOTOR_INVERTED, "both physical track polarities reverse");

void oldTrim(int8_t &l, int8_t &r, bool overdrive) {
  if (overdrive || l != r || l == 0) return;
  int mag = std::abs(int(l)), a = std::max(1, (mag * (l > 0 ? 22 : 14) + 50) / 100);
  bool positive = l > 0;
  l = (positive ? 1 : -1) * std::max(0, mag - a);
  r = (positive ? 1 : -1) * std::min(100, mag + a);
}
float angleError(float a, float b) { return std::remainder(a - b, 360.0F); }
int main() {
  int commandChecks = 0, quaternionChecks = 0;
  for (bool overdrive : {false, true}) for (int l = -100; l <= 100; ++l) for (int r = -100; r <= 100; ++r) {
    int8_t newL = l, newR = r, oldL = -r, oldR = -l;
    orientation::straightTrim(newL, newR, overdrive);
    oldTrim(oldL, oldR, overdrive);
    // Logical new RIGHT controls former LEFT, both with negative polarity.
    assert(-newR == oldL && -newL == oldR);
    ++commandChecks;
  }
  constexpr float rad = 0.01745329251994F, deg = 57.295779513F;
  for (int roll = -80; roll <= 80; roll += 20) for (int pitch = -80; pitch <= 80; pitch += 20)
    for (int yaw = -180; yaw <= 180; yaw += 15) {
      const float cr = std::cos(roll*rad/2), sr = std::sin(roll*rad/2);
      const float cp = std::cos(pitch*rad/2), sp = std::sin(pitch*rad/2);
      const float cy = std::cos(yaw*rad/2), sy = std::sin(yaw*rad/2);
      float w = cr*cp*cy + sr*sp*sy, x = sr*cp*cy - cr*sp*sy;
      float y = cr*sp*cy + sr*cp*sy, z = cr*cp*sy - sr*sp*cy;
      orientation::rebaseQuaternion(w, x, y, z);
      float nr = std::atan2(2*(w*x+y*z), 1-2*(x*x+y*y))*deg;
      float np = std::asin(std::max(-1.0F,std::min(1.0F,2*(w*y-z*x))))*deg;
      float ny = std::atan2(2*(w*z+x*y), 1-2*(y*y+z*z))*deg;
      assert(std::abs(angleError(nr, -roll)) < 0.001F);
      assert(std::abs(np + pitch) < 0.001F);
      assert(std::abs(angleError(ny, yaw+180)) < 0.001F);
      ++quaternionChecks;
    }
  std::cout << commandChecks << " track/trim cases; " << quaternionChecks << " IMU frame cases passed\n";
}
