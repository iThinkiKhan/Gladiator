#include "CydMatrixView.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>

namespace bench {
namespace {

constexpr int16_t SCREEN_W = 320, SCREEN_H = 240;
constexpr int16_t HEADER_H = 26, FOOTER_H = 22;
constexpr int16_t BODY_Y = HEADER_H, BODY_H = SCREEN_H - HEADER_H - FOOTER_H;

// SEN0628 optics: 64 zones across a 60 degree square field of view.
constexpr float FOV_DEG = 60.0F, ZONE_DEG = FOV_DEG / 8.0F;
// Zone 0 is one corner of the array; which corner depends on how the board is
// bolted down. Flip these once the sensor has a fixed mounting.
constexpr bool MIRROR_COLUMNS = false, MIRROR_ROWS = false;

// The driver's own usability window. Bench measurement on 2026-09-09 showed the
// sensor reports exactly 4000 for a zone with no return, while real readings
// jitter by tens of mm, so the top of the range is a sentinel and not a
// distance. The views exclude it; MatrixLidarSensor still counts it as usable.
constexpr uint16_t MIN_MM = 20, MAX_MM = 4000, NO_RETURN_MM = 4000;
inline bool zoneValid(uint16_t mm) { return mm >= MIN_MM && mm < NO_RETURN_MM; }

// The panel is on HSPI; the touch controller shares none of its pins and needs
// its own bus. IRQ (GPIO36) is unused: polling avoids a shared interrupt path.
constexpr uint8_t TOUCH_CLK = 25, TOUCH_MISO = 39, TOUCH_MOSI = 32, TOUCH_CS = 33;

TFT_eSPI tft;
TFT_eSprite body(&tft);
bool spriteOk = false;
SPIClass touchSpi(VSPI);

// The XPT2046 is a 12-bit SAR with a one-byte command: start, channel, mode,
// SER/DFR, power. Driving it directly keeps the bench free of a touch library
// whose only job here is "is a finger down, and roughly where".
uint16_t xptRead(uint8_t command) {
  touchSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TOUCH_CS, LOW);
  touchSpi.transfer(command);
  const uint16_t high = touchSpi.transfer(0), low = touchSpi.transfer(0);
  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.endTransaction();
  return ((high << 8) | low) >> 3;   // 12 significant bits, left aligned
}

// Pressure from the two cross-panel resistance reads. Below the threshold the
// X/Y conversions are noise, so they are not worth taking.
constexpr int16_t TOUCH_Z_THRESHOLD = 400;
bool readTouch(uint16_t &x, uint16_t &y) {
  const uint16_t z1 = xptRead(0xB1), z2 = xptRead(0xC1);
  if (int16_t(z1) + 4095 - int16_t(z2) < TOUCH_Z_THRESHOLD) return false;
  uint32_t sx = 0, sy = 0;
  for (uint8_t i = 0; i < 3; ++i) { sx += xptRead(0xD1); sy += xptRead(0x91); }
  x = sx / 3;
  y = sy / 3;
  return true;
}

ViewMode current = ViewMode::HEATMAP;
char scanText[48] = "not scanned";
uint16_t touchRawX = 0, touchRawY = 0;
uint32_t touchDownMs = 0;
bool touchWasDown = false, longFired = false;

// Colour scale bounds, slewed toward the live extremes so the palette does not
// flicker frame to frame while still using the whole gradient.
float scaleLo = 200, scaleHi = 2000;

// Trend history of the nearest return, one entry per accepted frame.
constexpr uint16_t TREND_N = 300;
uint16_t trend[TREND_N] = {};
uint16_t trendCount = 0, trendHead = 0;

// Heatmap cell cache: redrawing only changed cells keeps the grid flicker-free
// without a true-colour sprite, which would not fit beside the 8-bit one.
uint16_t cellColour[64] = {};
int16_t cellValue[64] = {};
bool heatmapDrawn = false;
ViewMode drawnMode = ViewMode::COUNT;

uint16_t heatColour(uint16_t mm) {
  if (!zoneValid(mm)) return tft.color565(24, 24, 28);
  float t = (float(mm) - scaleLo) / (scaleHi - scaleLo);
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  // Near is warm, far is cold: red, orange, yellow, green, cyan, blue.
  static const uint8_t stops[6][3] = {
    {200, 30, 30}, {235, 120, 20}, {230, 220, 40},
    {60, 200, 70}, {40, 190, 210}, {50, 80, 210}};
  const float f = t * 5.0F;
  const uint8_t i = uint8_t(f);
  const uint8_t j = i >= 5 ? 5 : i + 1;
  const float k = f - i;
  return tft.color565(uint8_t(stops[i][0] + (stops[j][0] - stops[i][0]) * k),
                      uint8_t(stops[i][1] + (stops[j][1] - stops[i][1]) * k),
                      uint8_t(stops[i][2] + (stops[j][2] - stops[i][2]) * k));
}

uint8_t zoneIndex(uint8_t row, uint8_t col) {
  const uint8_t r = MIRROR_ROWS ? 7 - row : row;
  const uint8_t c = MIRROR_COLUMNS ? 7 - col : col;
  return r * 8 + c;
}

// Zones with a real return this frame. Recounted here rather than taken from
// ToFData::derived, which still treats the 4000 sentinel as a usable distance.
uint8_t usableNow = 0;
uint16_t nearestNow = 0;

void updateScale(const sensors::ToFData &tof) {
  uint16_t lo = MAX_MM, hi = MIN_MM;
  usableNow = 0;
  for (uint8_t i = 0; i < 64; ++i) {
    const uint16_t mm = tof.raw.distanceMm[i];
    if (!zoneValid(mm)) continue;
    ++usableNow;
    if (mm < lo) lo = mm;
    if (mm > hi) hi = mm;
  }
  if (!usableNow) { nearestNow = 0; return; }
  nearestNow = lo;
  // Hold at least 300 mm of span so a flat scene does not amplify sensor noise
  // into a full-range rainbow.
  if (hi - lo < 300) {
    const uint16_t mid = (lo + hi) / 2;
    lo = mid > 150 ? mid - 150 : 0;
    hi = lo + 300;
  }
  scaleLo += (lo - scaleLo) * 0.15F;
  scaleHi += (hi - scaleHi) * 0.15F;
}

// --- chrome -----------------------------------------------------------------

void drawHeader(const sensors::ToFData &tof, bool frozen) {
  static char last[56] = "";
  const uint16_t bg = tft.color565(16, 18, 24);
  const auto &m = tof.meta;
  char line[56];
  if (usableNow)
    snprintf(line, sizeof line, "%4u mm  %2u/64  %4.1fHz %s%s", nearestNow, usableNow,
             m.rateHz, sensors::healthName(m.health), frozen ? " HOLD" : "");
  else
    snprintf(line, sizeof line, "  --  mm   0/64  %4.1fHz %s%s", m.rateHz,
             sensors::healthName(m.health), frozen ? " HOLD" : "");
  if (!strcmp(line, last)) return;
  strcpy(last, line);
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, bg);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(m.online ? TFT_WHITE : tft.color565(240, 90, 90), bg);
  tft.drawString(line, 6, HEADER_H / 2, 2);
}

void drawFooter() {
  const int16_t y = SCREEN_H - FOOTER_H, w = SCREEN_W / uint8_t(ViewMode::COUNT);
  const uint16_t bg = tft.color565(16, 18, 24), on = tft.color565(40, 80, 150);
  tft.fillRect(0, y, SCREEN_W, FOOTER_H, bg);
  tft.setTextDatum(MC_DATUM);
  for (uint8_t i = 0; i < uint8_t(ViewMode::COUNT); ++i) {
    const bool sel = i == uint8_t(current);
    if (sel) tft.fillRect(i * w + 2, y + 2, w - 4, FOOTER_H - 4, on);
    tft.setTextColor(sel ? TFT_WHITE : tft.color565(130, 135, 145), sel ? on : bg);
    tft.drawString(modeName(ViewMode(i)), i * w + w / 2, y + FOOTER_H / 2, 1);
  }
}

// --- views ------------------------------------------------------------------

void drawHeatmap(const sensors::ToFData &tof) {
  constexpr int16_t CELL = BODY_H / 8;           // 24 px, square cells
  constexpr int16_t GX = 8, GY = BODY_Y;
  constexpr int16_t LX = GX + 8 * CELL + 20;     // legend bar
  if (!heatmapDrawn)
    for (uint8_t i = 0; i < 64; ++i) { cellColour[i] = 1; cellValue[i] = -2; }
  for (uint8_t row = 0; row < 8; ++row) {
    for (uint8_t col = 0; col < 8; ++col) {
      const uint16_t mm = tof.raw.distanceMm[zoneIndex(row, col)];
      const bool ok = zoneValid(mm);
      const uint16_t c = heatColour(mm);
      const int16_t shown = ok ? int16_t(mm / 10) : -1;   // centimetres
      const uint8_t slot = row * 8 + col;
      if (c == cellColour[slot] && shown == cellValue[slot]) continue;
      cellColour[slot] = c;
      cellValue[slot] = shown;
      const int16_t x = GX + col * CELL, y = GY + row * CELL;
      tft.fillRect(x, y, CELL - 1, CELL - 1, c);
      tft.setTextDatum(MC_DATUM);
      // Dark text over the bright middle of the ramp, light text at either end.
      const bool bright = ok && mm > scaleLo * 0.9F && mm < scaleHi * 0.9F;
      tft.setTextColor(bright ? TFT_BLACK : TFT_WHITE, c);
      char t[6];
      if (ok) snprintf(t, sizeof t, "%d", shown); else strcpy(t, "-");
      tft.drawString(t, x + CELL / 2 - 1, y + CELL / 2 - 1, 1);
    }
  }
  // Legend: the ramp plus its live bounds, so the colours stay quantitative.
  static float legendLo = -1, legendHi = -1;
  if (!heatmapDrawn || fabsf(legendLo - scaleLo) > 25 || fabsf(legendHi - scaleHi) > 25) {
    legendLo = scaleLo;
    legendHi = scaleHi;
    for (int16_t i = 0; i < BODY_H - 34; ++i) {
      const uint16_t mm = uint16_t(scaleLo + (scaleHi - scaleLo) * i / float(BODY_H - 35));
      tft.drawFastHLine(LX, BODY_Y + 16 + i, 18, heatColour(mm));
    }
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char t[12];
    snprintf(t, sizeof t, "%3dcm  ", int(scaleLo / 10));
    tft.drawString(t, LX - 6, BODY_Y + 2, 1);
    snprintf(t, sizeof t, "%3dcm  ", int(scaleHi / 10));
    tft.drawString(t, LX - 6, BODY_Y + BODY_H - 12, 1);
    tft.setTextColor(tft.color565(150, 155, 165), TFT_BLACK);
    tft.drawString("near", LX + 22, BODY_Y + 16, 1);
    tft.drawString("cells", LX + 22, BODY_Y + 74, 1);
    tft.drawString("in cm", LX + 22, BODY_Y + 86, 1);
    tft.drawString("far", LX + 22, BODY_Y + BODY_H - 26, 1);
  }
  heatmapDrawn = true;
}

// Top-down plan view: the nearest return in each of the 8 columns, drawn as a
// 60 degree fan. This is the view that answers "what is in front of the robot".
void drawProfile(const sensors::ToFData &tof) {
  body.fillSprite(TFT_BLACK);
  const int16_t ox = SCREEN_W / 2, oy = BODY_H - 6;
  constexpr float R = BODY_H - 18;
  for (uint8_t m = 1; m <= 4; ++m) {
    const int16_t r = int16_t(R * m / 4.0F);
    body.drawCircle(ox, oy, r, tft.color565(45, 48, 55));
    body.setTextDatum(BC_DATUM);
    body.setTextColor(tft.color565(110, 115, 125), TFT_BLACK);
    body.drawNumber(m, ox, oy - r + 11, 1);
  }
  for (uint8_t col = 0; col < 8; ++col) {
    uint16_t nearest = 0;
    for (uint8_t row = 0; row < 8; ++row) {
      const uint16_t mm = tof.raw.distanceMm[zoneIndex(row, col)];
      if (!zoneValid(mm)) continue;
      if (!nearest || mm < nearest) nearest = mm;
    }
    const float a0 = (col - 4.0F) * ZONE_DEG * DEG_TO_RAD;
    const float a1 = (col - 3.0F) * ZONE_DEG * DEG_TO_RAD;
    const float r = nearest ? R * nearest / float(MAX_MM) : R;
    const int16_t x0 = ox + int16_t(sinf(a0) * r), y0 = oy - int16_t(cosf(a0) * r);
    const int16_t x1 = ox + int16_t(sinf(a1) * r), y1 = oy - int16_t(cosf(a1) * r);
    if (nearest) {
      body.fillTriangle(ox, oy, x0, y0, x1, y1, heatColour(nearest));
      body.drawLine(x0, y0, x1, y1, TFT_WHITE);
    } else {
      // No return at all: outline the sector so a blind column stays visible.
      body.drawLine(ox, oy, x0, y0, tft.color565(60, 62, 70));
      body.drawLine(ox, oy, x1, y1, tft.color565(60, 62, 70));
    }
  }
  body.setTextDatum(TL_DATUM);
  body.setTextColor(tft.color565(150, 155, 165), TFT_BLACK);
  body.drawString("nearest per column, rings = metres", 4, 3, 1);
  body.pushSprite(0, BODY_Y);
}

// Three-quarter view of all 64 zones as a point cloud, using the pinhole model
// implied by the 60 degree field of view.
void drawCloud(const sensors::ToFData &tof) {
  body.fillSprite(TFT_BLACK);
  const int16_t cx = SCREEN_W / 2 - 20, cy = BODY_H / 2 + 20;
  const float s = (BODY_H * 0.5F) / float(MAX_MM);
  for (uint8_t row = 0; row < 8; ++row) {
    for (uint8_t col = 0; col < 8; ++col) {
      const uint16_t mm = tof.raw.distanceMm[zoneIndex(row, col)];
      if (!zoneValid(mm)) continue;
      const float ax = (col - 3.5F) * ZONE_DEG * DEG_TO_RAD;
      const float ay = (row - 3.5F) * ZONE_DEG * DEG_TO_RAD;
      const float X = mm * tanf(ax), Y = mm * tanf(ay), Z = mm;
      const int16_t px = cx + int16_t((X * 0.85F + Z * 0.34F) * s);
      const int16_t py = cy + int16_t((Y * 0.80F - Z * 0.30F) * s);
      if (px < 2 || px > SCREEN_W - 3 || py < 2 || py > BODY_H - 3) continue;
      // Near points draw larger, which reads as depth without a z-buffer.
      const uint8_t r = mm < 600 ? 4 : mm < 1500 ? 3 : mm < 2500 ? 2 : 1;
      body.fillCircle(px, py, r, heatColour(mm));
    }
  }
  body.setTextDatum(TL_DATUM);
  body.setTextColor(tft.color565(150, 155, 165), TFT_BLACK);
  body.drawString("64 zones, 3/4 view, depth toward upper right", 4, 3, 1);
  body.pushSprite(0, BODY_Y);
}

// Strip chart of the nearest return: the view that exposes jitter, drift and
// dropouts, which a per-frame grid hides.
void drawTrend() {
  body.fillSprite(TFT_BLACK);
  uint16_t lo = MAX_MM, hi = 0;
  for (uint16_t i = 0; i < trendCount; ++i) {
    const uint16_t v = trend[i];
    if (!v) continue;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi <= lo) { lo = lo > 100 ? lo - 100 : 0; hi = lo + 200; }
  const uint16_t pad = (hi - lo) / 8 + 10;
  lo = lo > pad ? lo - pad : 0;
  hi += pad;
  constexpr int16_t TOP = 16, BOT = BODY_H - 12;
  for (uint8_t g = 0; g <= 4; ++g) {
    const int16_t y = TOP + (BOT - TOP) * g / 4;
    body.drawFastHLine(36, y, SCREEN_W - 40, tft.color565(38, 40, 48));
    body.setTextDatum(MR_DATUM);
    body.setTextColor(tft.color565(120, 125, 135), TFT_BLACK);
    body.drawNumber(hi - (hi - lo) * g / 4, 34, y, 1);
  }
  int16_t prevX = -1, prevY = 0;
  for (uint16_t i = 0; i < trendCount; ++i) {
    const uint16_t idx = (trendHead + TREND_N - trendCount + i) % TREND_N;
    const uint16_t v = trend[idx];
    const int16_t x = 38 + int16_t((SCREEN_W - 42) * (i + 1) / float(TREND_N));
    if (!v) {
      // A dropout is a fact about the sensor, so draw it rather than interpolate.
      body.drawFastVLine(x, TOP, BOT - TOP, tft.color565(70, 28, 28));
      prevX = -1;
      continue;
    }
    const int16_t y = BOT - int16_t((BOT - TOP) * float(v - lo) / float(hi - lo));
    if (prevX >= 0) body.drawLine(prevX, prevY, x, y, heatColour(v));
    else body.drawPixel(x, y, heatColour(v));
    prevX = x;
    prevY = y;
  }
  body.setTextDatum(TL_DATUM);
  body.setTextColor(tft.color565(150, 155, 165), TFT_BLACK);
  body.drawString("nearest mm, last 300 frames, red = no return", 4, 2, 1);
  body.pushSprite(0, BODY_Y);
}

void drawDiag(const sensors::ToFData &tof) {
  body.fillSprite(TFT_BLACK);
  const auto &m = tof.meta;
  char l[64];
  int16_t y = 3;
  auto line = [&](const char *text, uint16_t colour) {
    body.setTextDatum(TL_DATUM);
    body.setTextColor(colour, TFT_BLACK);
    body.drawString(text, 6, y, 2);
    y += 17;
  };
  snprintf(l, sizeof l, "address   0x%02X  %s", m.address, sensors::healthName(m.health));
  line(l, m.online ? TFT_WHITE : tft.color565(240, 100, 100));
  snprintf(l, sizeof l, "frames    %lu at %.1f Hz", (unsigned long)m.updateCount, m.rateHz);
  line(l, TFT_WHITE);
  snprintf(l, sizeof l, "errors    %lu bus, %lu protocol", (unsigned long)m.errorCount,
           (unsigned long)tof.protocolErrors);
  line(l, (m.errorCount || tof.protocolErrors) ? tft.color565(250, 190, 80) : TFT_WHITE);
  snprintf(l, sizeof l, "streak    %lu consecutive", (unsigned long)m.consecutiveErrors);
  line(l, TFT_WHITE);
  snprintf(l, sizeof l, "last resp 0x%02X cmd %u", tof.raw.responseStatus, tof.raw.responseCommand);
  line(l, TFT_WHITE);
  snprintf(l, sizeof l, "age       %lu ms", (unsigned long)(millis() - m.lastUpdateMs));
  line(l, TFT_WHITE);
  snprintf(l, sizeof l, "i2c       %s", scanText);
  line(l, tft.color565(150, 200, 255));
  snprintf(l, sizeof l, "touch raw %u, %u", touchRawX, touchRawY);
  line(l, tft.color565(140, 145, 155));
  snprintf(l, sizeof l, "heap      %lu B free", (unsigned long)ESP.getFreeHeap());
  line(l, tft.color565(140, 145, 155));
  body.setTextDatum(TL_DATUM);
  body.setTextColor(tft.color565(140, 145, 155), TFT_BLACK);
  body.drawString("tap = next view, hold = freeze", 6, BODY_H - 11, 1);
  body.pushSprite(0, BODY_Y);
}

}  // namespace

const char *modeName(ViewMode m) {
  switch (m) {
    case ViewMode::HEATMAP: return "HEAT";
    case ViewMode::PROFILE: return "PLAN";
    case ViewMode::CLOUD: return "CLOUD";
    case ViewMode::TREND: return "TREND";
    default: return "DIAG";
  }
}

bool begin() {
  tft.init();
  tft.setRotation(1);            // landscape, ribbon to the left
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  // 8-bit depth: 61 kB for a full-width body sprite, which fits on the CYD's
  // ESP32 with no PSRAM. Only the heatmap needs true colour, and it draws
  // straight to the panel with per-cell change detection instead.
  body.setColorDepth(8);
  spriteOk = body.createSprite(SCREEN_W, BODY_H) != nullptr;
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  drawFooter();
  return spriteOk;
}

void note(const char *line) {
  static int16_t y = BODY_Y + 6;
  if (y > BODY_Y + BODY_H - 16) {
    tft.fillRect(0, BODY_Y, SCREEN_W, BODY_H, TFT_BLACK);
    y = BODY_Y + 6;
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tft.color565(180, 190, 200), TFT_BLACK);
  tft.drawString(line, 6, y, 2);
  y += 18;
}

void setScanResult(const char *text) { snprintf(scanText, sizeof scanText, "%s", text); }

void sample(const sensors::ToFData &tof) {
  // 0 marks a dropout in the chart, which is also where the sentinel belongs.
  const uint16_t nearest = tof.derived.valid ? tof.derived.nearestMm : 0;
  trend[trendHead] = zoneValid(nearest) ? nearest : 0;
  trendHead = (trendHead + 1) % TREND_N;
  if (trendCount < TREND_N) ++trendCount;
}

ViewMode mode() { return current; }

void setMode(ViewMode m) {
  if (m == current || m >= ViewMode::COUNT) return;
  current = m;
  heatmapDrawn = false;
  drawnMode = ViewMode::COUNT;
  tft.fillRect(0, BODY_Y, SCREEN_W, BODY_H, TFT_BLACK);
  drawFooter();
}

void cycle(int8_t delta) {
  const int8_t n = int8_t(ViewMode::COUNT);
  setMode(ViewMode((int8_t(current) + delta + n) % n));
}

void render(const sensors::ToFData &tof, bool frozen) {
  updateScale(tof);
  drawHeader(tof, frozen);
  if (drawnMode != current) { drawnMode = current; drawFooter(); }
  if (!spriteOk && current != ViewMode::HEATMAP) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(tft.color565(250, 120, 120), TFT_BLACK);
    tft.drawString("body sprite alloc failed", SCREEN_W / 2, BODY_Y + BODY_H / 2, 2);
    return;
  }
  switch (current) {
    case ViewMode::HEATMAP: drawHeatmap(tof); break;
    case ViewMode::PROFILE: drawProfile(tof); break;
    case ViewMode::CLOUD: drawCloud(tof); break;
    case ViewMode::TREND: drawTrend(); break;
    default: drawDiag(tof); break;
  }
}

bool pollTouch() {
  bool freeze = false;
  uint16_t x = 0, y = 0;
  if (readTouch(x, y)) {
    touchRawX = x;
    touchRawY = y;
    if (!touchWasDown) {
      touchWasDown = true;
      touchDownMs = millis();
      longFired = false;
    } else if (!longFired && millis() - touchDownMs > 700) {
      longFired = true;
      freeze = true;
    }
  } else if (touchWasDown) {
    touchWasDown = false;
    // A short tap advances; a long press already acted while still held.
    if (!longFired && millis() - touchDownMs > 40) cycle(1);
  }
  return freeze;
}
}  // namespace bench
