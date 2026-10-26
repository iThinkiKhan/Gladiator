#pragma once
#include <stdint.h>
#include "../../shared/InterfaceVocabulary.h"

namespace sensors {
enum class SensorHealth : uint8_t { UNKNOWN, INITIALIZING, OK, DEGRADED, STALE, ERROR, OFFLINE, NOT_CONFIGURED };
inline const char *healthName(SensorHealth h) {
  return interfacev1::healthName(uint8_t(h));
}
struct SensorMeta {
  bool enabled = true, initialized = false, online = false, hasSample = false;
  uint8_t address = 0;
  SensorHealth health = SensorHealth::UNKNOWN;
  uint32_t lastAttemptMs = 0, lastUpdateMs = 0, lastGoodUpdateMs = 0;
  uint32_t updateCount = 0, errorCount = 0, consecutiveErrors = 0;
  uint32_t rateWindowMs = 0, rateWindowCount = 0;
  float rateHz = 0;
  void good(uint32_t now) {
    online = hasSample = true;
    lastUpdateMs = lastGoodUpdateMs = now;
    ++updateCount;
    consecutiveErrors = 0;
    health = SensorHealth::OK;
  }
  void error(uint32_t now) {
    lastAttemptMs = now;
    ++errorCount;
    ++consecutiveErrors;
    health = consecutiveErrors >= 3 ? SensorHealth::OFFLINE : SensorHealth::ERROR;
    if (consecutiveErrors >= 3) online = false;
  }
  void age(uint32_t now, uint32_t staleMs, uint32_t offlineMs) {
    if (!enabled) { health = SensorHealth::NOT_CONFIGURED; return; }
    if (now - rateWindowMs >= 1000) {
      rateHz = (updateCount - rateWindowCount) * 1000.0F / (now - rateWindowMs);
      rateWindowCount = updateCount;
      rateWindowMs = now;
    }
    if (!initialized) return;
    const uint32_t elapsed = now - (hasSample ? lastUpdateMs : lastAttemptMs);
    if (elapsed > offlineMs) { health = SensorHealth::OFFLINE; online = false; }
    else if (elapsed > staleMs && health != SensorHealth::OFFLINE) health = SensorHealth::STALE;
  }
};
}
