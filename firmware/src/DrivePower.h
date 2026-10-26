#pragma once
#include <stdint.h>

namespace drivepower {
// Normal power is a per-track PWM ceiling, including trim and a falling ramp.
// Scale together to retain steering/trim ratio; Overdrive remains explicit.
inline void cap(int8_t &left, int8_t &right, uint8_t power, bool overdrive) {
  if (overdrive) return;
  const int limit=power>100?100:power;
  const int l=left<0?-int(left):left, r=right<0?-int(right):right;
  const int peak=l>r?l:r;
  if(peak<=limit || !peak)return;
  const int nl=(l*limit+peak/2)/peak, nr=(r*limit+peak/2)/peak;
  left=int8_t(left<0?-nl:nl);right=int8_t(right<0?-nr:nr);
}
}
