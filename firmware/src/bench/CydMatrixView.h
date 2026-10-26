#pragma once
// SensorData.h expects the Arduino/sh2 headers to have supplied size_t already,
// which main.cpp does on the S3. Include it here so this header stands alone.
#include <Arduino.h>
#include "../sensors/SensorData.h"

namespace bench {
// Visualisations for the SEN0628's 64 zones on the CYD's 320x240 panel.
enum class ViewMode : uint8_t { HEATMAP, PROFILE, CLOUD, TREND, DIAG, COUNT };
const char *modeName(ViewMode m);

bool begin();
// Startup text, shown before the sensor produces its first frame.
void note(const char *line);
void setScanResult(const char *text);

// Feed every new frame so the trend history advances at the sensor's rate.
void sample(const sensors::ToFData &tof);
void render(const sensors::ToFData &tof, bool frozen);

ViewMode mode();
void setMode(ViewMode m);
void cycle(int8_t delta);

// A tap advances the mode, a press beyond 700 ms toggles freeze. Both are
// deliberately independent of touch calibration. Returns true on freeze.
bool pollTouch();
}
