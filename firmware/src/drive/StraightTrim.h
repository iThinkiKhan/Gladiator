#pragma once
#include <stdint.h>

// Straight-line trim for equal-track commands.
//
// The two tracks are not equally strong, and how unequal they are changes with commanded
// power, so the correction is a table by power (not one constant). Each entry is a signed
// percent T: T > 0 holds the LEFT track back, T < 0 holds the RIGHT track back, 0 is none.
//
// The correction is a ratio. For a commanded power p and |T| = a, the strong track runs at
// p * (100 + a) / 100 and the weak one at p * (100 - a) / 100, so the mean stays p. When the
// strong track would pass 100 it is pinned at 100 and the weak track keeps the SAME ratio
// (weak = strong * (100 - a) / (100 + a)). Clipping only the strong track and leaving the
// weak one alone, as a plain +/- shift does, silently halves the correction at full power.
namespace drive {

constexpr int TRIM_POINTS = 4;
constexpr int TRIM_POWER[TRIM_POINTS] = {25, 50, 75, 100};

struct TrimTable {
  int8_t percent[TRIM_POINTS];
};

// Linear interpolation between the table's power points; flat below the first and above the last.
inline int trimPercentAt(const TrimTable &table, int power) {
  if (power <= TRIM_POWER[0]) return table.percent[0];
  for (int i = 1; i < TRIM_POINTS; ++i) {
    if (power <= TRIM_POWER[i]) {
      const int span = TRIM_POWER[i] - TRIM_POWER[i - 1];
      const int low = table.percent[i - 1], high = table.percent[i];
      const int numerator = (high - low) * (power - TRIM_POWER[i - 1]);
      // Round half away from zero so a symmetric table gives a symmetric result.
      const int step = numerator >= 0 ? (numerator + span / 2) / span : -((-numerator + span / 2) / span);
      return low + step;
    }
  }
  return table.percent[TRIM_POINTS - 1];
}

// Rewrites an equal, non-zero (left, right) command in place. Unequal pairs (turns), zero and
// Overdrive are left exactly as requested.
inline void applyStraightTrim(int8_t &left, int8_t &right, const TrimTable &forward,
                              const TrimTable &reverse, bool overdrive) {
  if (overdrive || left != right || left == 0) return;
  const bool ahead = left > 0;
  const int power = ahead ? left : -left;
  const int percent = trimPercentAt(ahead ? forward : reverse, power);
  if (percent == 0) return;
  const int a = percent > 0 ? percent : -percent;

  int strong = (power * (100 + a) + 50) / 100;
  if (strong > 100) strong = 100;
  const int weak = (strong * (100 - a) + (100 + a) / 2) / (100 + a);

  const int leftMagnitude = percent > 0 ? weak : strong;
  const int rightMagnitude = percent > 0 ? strong : weak;
  left = static_cast<int8_t>(ahead ? leftMagnitude : -leftMagnitude);
  right = static_cast<int8_t>(ahead ? rightMagnitude : -rightMagnitude);
}

}  // namespace drive
