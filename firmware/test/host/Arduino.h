#pragma once
#include <algorithm>
#include <stdint.h>
#include <stddef.h>
using std::min;
using std::max;
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
