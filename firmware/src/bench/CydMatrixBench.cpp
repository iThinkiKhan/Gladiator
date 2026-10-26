// Bench harness: exercise the real SEN0628 driver on a CYD (ESP32-2432S028R)
// instead of the S3, so the shipping protocol code is what gets validated, and
// render the 64 zones on the CYD panel.
// Wiring: CYD CN1 -> GND, GPIO27 = SDA, GPIO22 = SCL, 3V3 -> 3V3.
#include <Arduino.h>
#include <Wire.h>
#include "CydMatrixView.h"
#include "../sensors/I2CBus.h"
#include "../sensors/MatrixLidarSensor.h"
#include "../sensors/SensorData.h"

namespace {
constexpr uint8_t BENCH_SDA = 27, BENCH_SCL = 22;
constexpr uint32_t BENCH_HZ = 400000;

sensors::ToFData tof;
sensors::ToFData held;        // snapshot the display keeps while frozen
sensors::I2CBus bus;
sensors::MatrixLidarSensor lidar(bus, tof);
sensors::SensorHealth reported = sensors::SensorHealth::UNKNOWN;

bool frozen = false, serialGrid = true;
uint32_t lastSerialMs = 0, lastRenderMs = 0, lastFrames = 0;

void scan() {
  char summary[48] = "";
  uint8_t found = 0;
  Serial.println("I2C scan 0x08..0x77:");
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    if (!bus.present(a)) continue;
    ++found;
    const bool sensor = a >= 0x30 && a <= 0x33;
    Serial.printf("  0x%02X%s\n", a, sensor ? "  <- SEN0628 address range" : "");
    if (strlen(summary) < 32) snprintf(summary + strlen(summary), sizeof summary - strlen(summary),
                                       "%s0x%02X", summary[0] ? " " : "", a);
  }
  if (!found) {
    Serial.println("  nothing responded - check SDA/SCL, ground and 3V3");
    strcpy(summary, "no devices");
  }
  bench::setScanResult(summary);
  bench::note(found ? summary : "I2C: no devices found");
}

void printFrame(const sensors::ToFData &d) {
  const auto &m = d.meta;
  Serial.printf("\n[%lus] addr 0x%02X  %s  %.1f Hz  frames %lu  errors %lu/%lu%s\n",
                millis() / 1000UL, m.address, sensors::healthName(m.health), m.rateHz,
                (unsigned long)m.updateCount, (unsigned long)m.errorCount,
                (unsigned long)d.protocolErrors, frozen ? "  [HOLD]" : "");
  if (!m.hasSample) { Serial.println("  no sample yet"); return; }
  if (serialGrid) {
    for (uint8_t row = 0; row < 8; ++row) {
      Serial.print("  ");
      for (uint8_t col = 0; col < 8; ++col) {
        const uint16_t mm = d.raw.distanceMm[row * 8 + col];
        // Out-of-range zones print as dashes so a real reading of 0 is not hidden.
        if (mm < 20 || mm > 4000) Serial.print("   ---");
        else Serial.printf(" %5u", mm);
      }
      Serial.println();
    }
  }
  if (d.derived.valid)
    Serial.printf("  nearest %u mm at zone %u (row %u col %u), %u/64 usable\n",
                  d.derived.nearestMm, d.derived.nearestZone, d.derived.nearestZone / 8,
                  d.derived.nearestZone % 8, d.derived.usableZones);
  else
    Serial.println("  no zone in range");
}

void toggleFreeze() {
  frozen = !frozen;
  if (frozen) held = tof;
  Serial.printf("[hold] %s\n", frozen ? "frozen" : "live");
}

void handleSerial() {
  while (Serial.available()) {
    const int c = Serial.read();
    switch (c) {
      case 'n': bench::cycle(1); break;
      case 'p': bench::cycle(-1); break;
      case 'f': toggleFreeze(); break;
      case 's': scan(); break;
      case 'g': serialGrid = !serialGrid;
                Serial.printf("[serial] grid dump %s\n", serialGrid ? "on" : "off"); break;
      case 'h': Serial.println("n/p view, f freeze, s rescan, g grid dump, 0-4 view"); break;
      default:
        if (c >= '0' && c <= '4') bench::setMode(bench::ViewMode(c - '0'));
        break;
    }
    if (c >= ' ') Serial.printf("[view] %s\n", bench::modeName(bench::mode()));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\nSEN0628 bench on CYD");
  Serial.printf("SDA=GPIO%u SCL=GPIO%u @ %lu Hz\n", BENCH_SDA, BENCH_SCL, (unsigned long)BENCH_HZ);
  Serial.println("keys: n/p view, f freeze, s rescan, g grid dump, 0-4 view");
  const bool sprite = bench::begin();
  Serial.printf("display up, body sprite %s, %lu B heap free\n",
                sprite ? "allocated" : "FAILED", (unsigned long)ESP.getFreeHeap());
  bench::note("SEN0628 bench");
  bench::note("SDA GPIO27  SCL GPIO22");
  // The driver never touches the pins; only bus start-up is board specific.
  if (!Wire.begin(BENCH_SDA, BENCH_SCL, BENCH_HZ)) {
    Serial.println("Wire.begin FAILED");
    bench::note("Wire.begin FAILED");
  }
  Wire.setTimeOut(board::I2C_TIMEOUT_MS);
  scan();
  Serial.println("Driver starting; first frame follows the sensor's 5 s settle.");
  bench::note("waiting for first frame...");
}

void loop() {
  lidar.update();
  handleSerial();
  if (bench::pollTouch()) toggleFreeze();

  const uint32_t now = millis();
  if (tof.meta.health != reported) {
    reported = tof.meta.health;
    Serial.printf("[%lums] health -> %s\n", (unsigned long)now, sensors::healthName(reported));
  }
  const bool newFrame = tof.meta.updateCount != lastFrames;
  if (newFrame && !frozen) bench::sample(tof);

  // Redraw on a new frame but no faster than the panel can usefully take, and
  // at least twice a second so age, rate and health keep moving when it stalls.
  if ((newFrame || now - lastRenderMs >= 500) && now - lastRenderMs >= 60) {
    lastRenderMs = now;
    bench::render(frozen ? held : tof, frozen);
  }
  if (newFrame) lastFrames = tof.meta.updateCount;

  if (now - lastSerialMs >= 1000) {
    lastSerialMs = now;
    printFrame(frozen ? held : tof);
  }
}
