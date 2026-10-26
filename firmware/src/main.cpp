#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Update.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <rom/rtc.h>
#include <esp_ota_ops.h>
#include "BoardConfig.h"
#include "Orientation.h"
#include "DrivePower.h"
#include "mast/MastHardware.h"
#include "sensors/SensorManager.h"
#include "status/StatusLed.h"
#include "blackbox/BlackBox.h"
#include "drive/StraightTrim.h"
#include "gateway/RobotLink.h"
#include "gateway/ControlJson.h"
#include "../shared/FieldPage.h"
#include "../shared/ServicePage.h"

namespace {

// Robot-relative mast-front pin map. The LEFT driver's enables are tied on GPIO2.
constexpr uint8_t LEFT_R_EN_PIN = board::LEFT_R_EN;
constexpr uint8_t LEFT_RPWM_PIN = board::LEFT_RPWM;
constexpr uint8_t LEFT_L_EN_PIN = board::LEFT_L_EN;
constexpr uint8_t LEFT_LPWM_PIN = board::LEFT_LPWM;

constexpr uint8_t RIGHT_R_EN_PIN = board::RIGHT_R_EN;
constexpr uint8_t RIGHT_L_EN_PIN = board::RIGHT_L_EN;
constexpr uint8_t RIGHT_RPWM_PIN = board::RIGHT_RPWM;
constexpr uint8_t RIGHT_LPWM_PIN = board::RIGHT_LPWM;

constexpr uint8_t LEFT_RPWM_CHANNEL = 0;
constexpr uint8_t LEFT_LPWM_CHANNEL = 1;
constexpr uint8_t RIGHT_RPWM_CHANNEL = 2;
constexpr uint8_t RIGHT_LPWM_CHANNEL = 3;

constexpr uint32_t PWM_FREQUENCY_HZ = 18000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1U;

// Confirmed by the first powered test: both BTS7960 boards need the same
// logical polarity. The earlier right-side inversion swapped translation and
// rotation (Right drove forward, Left drove backward).
constexpr bool LEFT_MOTOR_INVERTED = orientation::MOTOR_INVERTED;
constexpr bool RIGHT_MOTOR_INVERTED = orientation::MOTOR_INVERTED;

constexpr uint32_t COMMAND_TIMEOUT_MS = 350;
constexpr uint32_t STATUS_INTERVAL_MS = 250;
constexpr uint32_t RAMP_INTERVAL_MS = 20;
constexpr int8_t RAMP_STEP_PERCENT = 5;
constexpr uint32_t POWER_LOG_INTERVAL_MS = 1000;
// Measured 2026-09-08 on carpet with the BNO085 game rotation vector: eight
// forward runs at left+right=100 with the trim bypassed (unequal pairs skip
// applyForwardTrim) gave veer_rate = 0.580*d + 13.08 deg/s, where d = left-right.
// Straight running needs d = -22.5, bracketed by measurements at d=-18 (+2.44
// deg/s) and d=-24 (-1.12 deg/s). At left=50 this yields an 11 point shift, so
// 39/61. The direction was always correct; the previous value of 5 was simply
// about a quarter of the required magnitude.
//
// Caveats: measured only at left+right=100 and on one carpet, so the assumed
// proportionality to commanded power is untested at other speeds. Above roughly
// 78% the opposite track saturates at 100 and the correction stops growing. A
// 22% imbalance is large enough to be worth investigating mechanically rather
// than only compensating in software.
// Applies only to normal straight-ahead commands; turns, reverse and Overdrive
// retain their direct requested values.
//
// Re-measured 2026-10-09 after the new body (tools/tune_straight.py, BNO085 game
// rotation vector, 50% power): the old 22% over-corrected and the robot pulled
// right. Balance is now about d = -11.5 (noise-limited, +/-3 deg/s per run), so
// 12%. The chassis asymmetry is roughly half what it was in the old body.
//
// Trim is now a table by commanded power (25/50/75/100%), signed percent, positive holds the
// left track back (see drive/StraightTrim.h; the correction keeps its ratio when a track
// reaches 100). INTERIM values, 2026-10-10: the single 12% above was measured at 50% power
// and over-corrects at full power. On the field run it veered about 20 deg/s at 100% and,
// after the thrown track was repaired, about 30 deg/s forward. Refill these from on-ground
// sweeps at each power level (tools/tune_ratio.py).
constexpr drive::TrimTable FORWARD_TRIM = {{12, 12, 6, 0}};
// Reverse was never trimmed at all before this, so it still curved once forward
// was straight. Measured the same way (reverse always bypassed the trim, so a
// true d=0 point was available): veer_rate = 0.645*d - 9.30 deg/s, balancing at
// d = +14.4. The sign is opposite to forward only because both tracks invert;
// in magnitudes forward wants 39/61 and reverse 43/57, so the same track is
// held back either way, just by different amounts.
//
// Re-measured 2026-10-09 in the new body, alternating forward/reverse runs at
// 50% power: balance d = +20.4 (coarse fit 21.7, earlier sweep about 20), so 20%.
// Reverse now needs more correction than forward, the opposite of the old body.
// Above roughly 83% the right track saturates at -100 and the correction stops
// growing.
// INTERIM, as above.
constexpr drive::TrimTable REVERSE_TRIM = {{20, 20, 10, 0}};

constexpr uint8_t FRAME_MAGIC_COMMAND = 0x47; // 'G'
constexpr uint8_t FRAME_MAGIC_STATUS = 0x53;  // 'S'
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t FRAME_SIZE = 8;
constexpr uint8_t CHECKSUM_SEED = 0xA5;
constexpr uint8_t POWER_FRAME_MAGIC = 0x50; // 'P'
constexpr size_t POWER_FRAME_SIZE = 20;

constexpr uint8_t FLAG_ARMED = 0x01;
constexpr uint8_t FLAG_DEADMAN = 0x02;
constexpr uint8_t FLAG_OVERDRIVE = 0x04;

constexpr char DEVICE_NAME[] = "Gladiator";
constexpr char SERVICE_UUID[] = "2b7e0001-8c8f-4f6c-b8f8-47a117d1e001";
constexpr char COMMAND_UUID[] = "2b7e0002-8c8f-4f6c-b8f8-47a117d1e001";
constexpr char STATUS_UUID[] = "2b7e0003-8c8f-4f6c-b8f8-47a117d1e001";
constexpr char POWER_UUID[] = "2b7e0004-8c8f-4f6c-b8f8-47a117d1e001";

constexpr char MAINTENANCE_AP_NAME[] = "Gladiator-Setup";
constexpr char MAINTENANCE_AP_PASSWORD[] = GLADIATOR_MAINTENANCE_AP_PASSWORD;
constexpr char OTA_PASSWORD[] = GLADIATOR_OTA_PASSWORD;
constexpr size_t WEB_LOG_MAX_CHARS = 12000;

struct Motor {
  uint8_t enablePinA;
  uint8_t enablePinB;
  bool separateEnablePins;
  uint8_t forwardPwmPin;
  uint8_t reversePwmPin;
  uint8_t forwardPwmChannel;
  uint8_t reversePwmChannel;
  bool inverted;
  int8_t currentPercent;
};

enum class ControlSource : uint8_t { NONE, BLE, WEB, LINK, FIELD };
const char *controlSourceName(ControlSource source){
  switch(source){case ControlSource::BLE:return "BLE";case ControlSource::WEB:return "WEB";
    case ControlSource::LINK:return "LINK";case ControlSource::FIELD:return "FIELD";default:return "NONE";}
}

Motor leftMotor = {
    LEFT_R_EN_PIN,
    LEFT_L_EN_PIN,
    board::LEFT_SEPARATE_ENABLES,
    LEFT_RPWM_PIN,
    LEFT_LPWM_PIN,
    LEFT_RPWM_CHANNEL,
    LEFT_LPWM_CHANNEL,
    LEFT_MOTOR_INVERTED,
    0,
};

Motor rightMotor = {
    RIGHT_R_EN_PIN,
    RIGHT_L_EN_PIN,
    board::RIGHT_SEPARATE_ENABLES,
    RIGHT_RPWM_PIN,
    RIGHT_LPWM_PIN,
    RIGHT_RPWM_CHANNEL,
    RIGHT_LPWM_CHANNEL,
    RIGHT_MOTOR_INVERTED,
    0,
};

BLEServer *bleServer = nullptr;
BLECharacteristic *statusCharacteristic = nullptr;
BLECharacteristic *powerCharacteristic = nullptr;
WebServer maintenanceServer(80);
String webLog;

volatile bool bleConnected = false;
volatile bool commandArmed = false;
volatile bool commandDeadman = false;
volatile bool commandOverdrive = false;
volatile bool watchdogStopped = false;
volatile bool overdrivePending = false;
volatile ControlSource activeControlSource = ControlSource::NONE;
volatile int8_t targetLeftPercent = 0;
volatile int8_t targetRightPercent = 0;
volatile uint8_t selectedPowerMode = 25;
volatile uint8_t lastSequence = 0;
volatile uint32_t lastCommandMs = 0;
SemaphoreHandle_t commandMutex=nullptr;
struct ControlGuard {
  ControlGuard(){if(commandMutex)xSemaphoreTakeRecursive(commandMutex,portMAX_DELAY);}
  ~ControlGuard(){if(commandMutex)xSemaphoreGiveRecursive(commandMutex);}
};

uint32_t lastRampMs = 0;
uint32_t lastStatusMs = 0;
sensors::SensorManager sensorManager;
sensors::RobotSensors sensorData;
gateway::RobotLink gatewayLink;
robotlink::Session remoteSession;
uint32_t linkGeneration=0;
std::atomic<bool> revokeRemote{false};
bool webMustRelease=false;
volatile bool bleMustRelease=false;
bool sensorTaskStarted = false;
uint32_t loopMaxUs = 0;
uint32_t lastPowerLogMs = 0;

// Reset diagnostics. RTC_NOINIT survives a reset but not a power cycle, so a
// climbing count on a battery that never left the chassis means the controller
// is restarting itself rather than being restarted by someone.
RTC_NOINIT_ATTR uint32_t rtcBootMagic;
RTC_NOINIT_ATTR uint32_t rtcBootCount;
constexpr uint32_t BOOT_MAGIC = 0x474C4144; // 'GLAD'
esp_reset_reason_t bootReason = ESP_RST_UNKNOWN;
uint32_t bootRawReason = 0;
uint32_t bootCount = 0;

// OTA slot identity and confirmation state. With rollback enabled, an app that
// is still PENDING_VERIFY makes esp_ota_set_boot_partition refuse the next
// image, which surfaces only as a generic activation failure at the far end of
// a completed upload. Record it so the cause is visible without a cable.
esp_ota_handle_t webOtaHandle=0;const esp_partition_t *webOtaPartition=nullptr;
esp_err_t webOtaError=ESP_FAIL;bool webOtaComplete=false;
char otaPartition[8] = "?";
const char *otaStateName = "UNKNOWN";
bool otaMarkedValid = false;

status::StatusLed statusLed;
bool maintenanceApActive = false;
uint8_t apStationCount = 0;
uint32_t lastApPollMs = 0;

using BatteryState = interfacev1::Battery;
const char *batteryStateName(BatteryState state);
void coastAll();
BatteryState batteryState = BatteryState::UNKNOWN;
bool batteryLockout = false;
bool batteryRadiosShed = false;
uint32_t batteryAboveLockoutMs = 0, batteryBelowReleaseMs = 0, batteryAboveCriticalMs = 0;
bool benchPowerActive = false;
bool benchPowerCandidate = false;
bool benchPowerViaPin = false;
bool benchPowerViaUsb = false;
uint32_t benchPowerCandidateSinceMs = 0;

bool mastCalibrationInhibitsDrive();
bool batteryInhibitsMotion() {
  return mast::drivePowerInhibited(benchPowerActive,batteryLockout,batteryRadiosShed,mastCalibrationInhibitsDrive());
}

const char *benchPowerSourceName() {
  if (benchPowerViaPin && benchPowerViaUsb) return "GPIO7+USB";
  if (benchPowerViaPin) return "GPIO7";
  if (benchPowerViaUsb) return "USB_LOW_VOLTAGE";
  return "NONE";
}

bool ina226Present = false;
bool ina226SampleValid = false;
uint8_t ina226Address = 0;
uint8_t ina226SampleSequence = 0;
uint16_t busMillivolts = 0;
int32_t currentMilliamps = 0;
int32_t powerMilliwatts = 0;
uint16_t minimumBusMillivolts = UINT16_MAX;
uint16_t peakCurrentMilliamps = 0;

void appendLog(const String &message) {
  ControlGuard guard;
  const String line = String('[') + millis() + "] " + message + '\n';
  Serial.print(line);
  {
    // Flight recorder: every log line becomes one CSV row, newlines flattened.
    String row = String("E,") + millis() + ',' + message;
    row.replace('\n', ' ');
    blackbox::line(row.c_str());
  }
  webLog += line;
  if (webLog.length() > WEB_LOG_MAX_CHARS) {
    const int newline = webLog.indexOf('\n', webLog.length() - WEB_LOG_MAX_CHARS);
    webLog.remove(0, newline >= 0 ? newline + 1
                                 : webLog.length() - WEB_LOG_MAX_CHARS);
  }
}

void appendLogf(const char *format, ...) {
  char message[224];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  appendLog(message);
}

uint8_t checksum(const uint8_t *bytes, size_t count) {
  uint8_t value = CHECKSUM_SEED;
  for (size_t i = 0; i < count; ++i) {
    value ^= bytes[i];
  }
  return value;
}

void writeLe16(uint8_t *bytes, size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeLe32(uint8_t *bytes, size_t offset, int32_t value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  bytes[offset] = static_cast<uint8_t>(bits & 0xFF);
  bytes[offset + 1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<uint8_t>((bits >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<uint8_t>((bits >> 24) & 0xFF);
}

// Compatibility projection for the existing BLE/phone protocol. Native data
// remains in sensorData; rounding is confined to the legacy wire format.
void updateSensors() {
  sensorManager.snapshot(sensorData);
  const auto &p = sensorData.power;
  ina226Present = p.meta.online;
  ina226SampleValid = p.meta.hasSample && p.meta.health == sensors::SensorHealth::OK;
  ina226Address = p.meta.address;
  ina226SampleSequence = static_cast<uint8_t>(p.meta.updateCount);
  busMillivolts = static_cast<uint16_t>(constrain(lroundf(p.derived.busVolts * 1000), 0L, long(UINT16_MAX)));
  currentMilliamps = lroundf(p.derived.currentAmps * 1000);
  powerMilliwatts = lroundf(p.derived.watts * 1000);
  minimumBusMillivolts = p.derived.hasMinimum ? lroundf(p.derived.minimumVolts * 1000) : UINT16_MAX;
  peakCurrentMilliamps = static_cast<uint16_t>(constrain(lroundf(p.derived.peakAbsAmps * 1000), 0L, long(UINT16_MAX)));

  // Warning only. Without the power monitor there is no low-battery protection, but motion is
  // deliberately left exactly as it was: the operator decides what to do. The phone, the
  // status LED and the recorder all show it.
  static uint32_t powerBadSinceMs = 0;
  static bool powerWarned = false;
  const uint32_t nowMs = millis();
  if (ina226SampleValid) {
    if (powerWarned) appendLog("Power monitor back online; low-battery protection active again");
    powerWarned = false;
    powerBadSinceMs = 0;
  } else if (!powerBadSinceMs) {
    powerBadSinceMs = nowMs ? nowMs : 1;
  } else if (!powerWarned && nowMs > 5000 && nowMs - powerBadSinceMs >= 2000) {
    powerWarned = true;
    appendLog("WARNING: power monitor offline; low-battery protection is NOT active (motion unchanged)");
  }
}

String sensorStatusJson() {
  String json = sensors::sensorJson(sensorData);
  json.remove(json.length()-1);
  json += ",\"controller\":{\"state\":\"";
  json += commandArmed ? ((leftMotor.currentPercent || rightMotor.currentPercent) ? "DRIVE" : "ARMED") : "SAFE";
  json += "\",\"leftPercent\":" + String(leftMotor.currentPercent);
  json += ",\"rightPercent\":" + String(rightMotor.currentPercent);
  json += ",\"watchdogStopped\":" + String(watchdogStopped ? "true" : "false");
  json += ",\"sensorTaskStarted\":" + String(sensorTaskStarted ? "true" : "false");
  json += ",\"loopMaxUs\":" + String(loopMaxUs);
  json += ",\"led\":\"" + String(status::conditionName(statusLed.condition())) + "\"";
  json += ",\"apUp\":" + String(maintenanceApActive ? "true" : "false");
  json += ",\"apStations\":" + String(apStationCount);
  json += ",\"resetReason\":\"" + String(status::resetReasonName(bootReason)) + "\"";
  json += ",\"resetRaw\":" + String(bootRawReason);
  json += ",\"resetBlinkCode\":" + String(statusLed.code());
  json += ",\"bootCount\":" + String(bootCount);
  json += ",\"battery\":\"" + String(batteryStateName(batteryState)) + "\"";
  json += ",\"batteryLockout\":" + String(batteryLockout ? "true" : "false");
  json += ",\"benchPower\":" + String(benchPowerActive ? "true" : "false");
  json += ",\"benchPowerSource\":\"" + String(benchPowerSourceName()) + "\"}}";
  return json;
}

// USB commands cannot arm or move. The only write is 'bbclear', which erases the
// flight-recorder log (refused while armed).
#include "mast/MastApplication.h"
void updateSerialConsole() {
  static String command;
  static bool overflow=false;
  for (uint8_t budget=0; budget<64 && Serial.available(); ++budget) {
    char c=Serial.read();
    if (c=='\r') continue;
    if (c=='\n') {
      if(overflow){command="";overflow=false;Serial.println("USB command too long; discarded.");continue;}
      command.trim();
      if(mastSerial(command)){}
      else if (command == "sensors" || command == "status") {
        if (commandArmed) Serial.println("BUSY: disarm before USB snapshot.");
        else Serial.println(sensorStatusJson());
      } else if (command == "blackbox") {
        blackbox::manifest(Serial);
      } else if (command == "bbclear") {
        if (commandArmed) Serial.println("BUSY: disarm before clearing the blackbox.");
        else Serial.println(blackbox::clearAll() ? "blackbox cleared" : "blackbox not ready");
      } else if (command.length()) Serial.println("Commands: sensors | status | blackbox | bbclear");
      command="";
    } else if (!overflow&&command.length()<768) command += c;
    else {command="";overflow=true;}
  }
}

void logIna226() {
  const uint32_t now = millis();
  if (now - lastPowerLogMs < POWER_LOG_INTERVAL_MS) {
    return;
  }
  lastPowerLogMs = now;
  appendLogf("Sensors | POWER_MAIN %s %.1f Hz | IMU_BODY %s %.1f reports/s | state=%s | outputs=%d,%d",
             sensors::healthName(sensorData.power.meta.health), sensorData.power.meta.rateHz,
             sensors::healthName(sensorData.imu.meta.health), sensorData.imu.meta.rateHz,
             commandArmed ? "ARMED" : "SAFE", leftMotor.currentPercent, rightMotor.currentPercent);
  if (!ina226Present) {
    appendLog("INA226 not detected");
    return;
  }
  if (!ina226SampleValid) {
    appendLog("Runtime healthy | INA226 sample unavailable");
    return;
  }
  appendLogf(
      "VBUS: %.3f V | CURRENT: %.3f A | POWER: %.2f W | MIN: %.3f V | PEAK: %.3f A",
      busMillivolts / 1000.0F,
      currentMilliamps / 1000.0F,
      powerMilliwatts / 1000.0F,
      minimumBusMillivolts == UINT16_MAX ? 0.0F
                                         : minimumBusMillivolts / 1000.0F,
      peakCurrentMilliamps / 1000.0F);
}

const char *batteryStateName(BatteryState state) { return interfacev1::name(state); }
interfacev1::State currentInterfaceState() {
  return interfacev1::controllerState(commandArmed,leftMotor.currentPercent,rightMotor.currentPercent);
}

void updateBenchPower(uint32_t now) {
  benchPowerViaPin = digitalRead(board::BENCH_POWER_DETECT_PIN) ==
                     (board::BENCH_POWER_DETECT_ACTIVE_LOW ? LOW : HIGH);
  const float volts = sensorData.power.derived.busVolts;
  benchPowerViaUsb = Serial.isPlugged() && ina226SampleValid &&
                     volts >= board::USB_BENCH_MIN_V &&
                     volts <= board::USB_BENCH_MAX_V;
  const bool asserted = benchPowerViaPin || benchPowerViaUsb;
  if (asserted != benchPowerCandidate) {
    benchPowerCandidate = asserted;
    benchPowerCandidateSinceMs = now;
    return;
  }
  if (asserted == benchPowerActive ||
      now - benchPowerCandidateSinceMs < board::BENCH_POWER_DEBOUNCE_MS) return;

  benchPowerActive = asserted;
  batteryAboveLockoutMs = batteryBelowReleaseMs = batteryAboveCriticalMs = now;
  if (benchPowerActive) {
    batteryLockout = false;
    batteryState = BatteryState::BENCH;
    appendLogf("Bench power detected via %s; LiPo voltage protection bypassed",
               benchPowerSourceName());
  } else {
    batteryState = BatteryState::UNKNOWN;
    appendLog("Bench power signal released; LiPo voltage protection active");
  }
}

// Pack protection. Motors are the only load this firmware can actually remove,
// and they are by far the largest: idle draw is under a watt, a 70% spin pulled
// 0.8 A. At CRITICAL the radios go too, since below 3 V/cell the pack is being
// damaged and the light is a better use of the remaining energy than an AP.
void updateBatteryGuard(uint32_t now) {
  if (benchPowerActive) {
    batteryAboveLockoutMs = batteryBelowReleaseMs = batteryAboveCriticalMs = now;
    batteryLockout = false;
    batteryState = BatteryState::BENCH;
    return;
  }
  if (!ina226SampleValid) {
    // No trustworthy measurement: hold every timer at now so a missing monitor
    // cannot strand the robot. It also means the pack is unprotected meanwhile.
    batteryAboveLockoutMs = batteryBelowReleaseMs = batteryAboveCriticalMs = now;
    return;
  }
  const float volts = sensorData.power.derived.busVolts;
  const float perCell = volts / board::BATTERY_CELLS;
  if (volts > board::BATTERY_LOCKOUT_V) batteryAboveLockoutMs = now;
  if (volts < board::BATTERY_RELEASE_V) batteryBelowReleaseMs = now;
  if (volts > board::BATTERY_CRITICAL_V) batteryAboveCriticalMs = now;

  if (!batteryLockout && now - batteryAboveLockoutMs >= board::BATTERY_LOCKOUT_MS) {
    batteryLockout = true;
    appendLogf("Battery lockout: %.2f V (%.2f V/cell) held below %.2f V; motors disabled",
               volts, perCell, board::BATTERY_LOCKOUT_V);
  } else if (batteryLockout && now - batteryBelowReleaseMs >= board::BATTERY_RELEASE_MS) {
    batteryLockout = false;
    appendLogf("Battery recovered: %.2f V (%.2f V/cell); motors re-enabled", volts, perCell);
  }

  if (batteryLockout &&
      (commandArmed || commandDeadman || leftMotor.currentPercent || rightMotor.currentPercent)) {
    ControlGuard guard;
    // Applies regardless of which source is holding the command.
    remoteSession.invalidate();webMustRelease=true;bleMustRelease=true;
    activeControlSource = ControlSource::NONE;
    commandArmed = commandDeadman = commandOverdrive = false;
    watchdogStopped = true;
    coastAll();
  }

  if (!batteryRadiosShed && now - batteryAboveCriticalMs >= board::BATTERY_CRITICAL_MS) {
    batteryRadiosShed = true;
    appendLogf("Battery CRITICAL: %.2f V (%.2f V/cell). Shedding Wi-Fi and BLE. "
               "Charge the pack, then power cycle to restore them.", volts, perCell);
    delay(60); // let that line reach USB before the radios stop
    BLEDevice::deinit(true);
    bleConnected = false;
    maintenanceServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    maintenanceApActive = false;
    apStationCount = 0;
  }

  batteryState = batteryRadiosShed ? BatteryState::CRITICAL
                 : batteryLockout ? BatteryState::LOCKOUT
                 : volts < board::BATTERY_WARN_V ? BatteryState::WARN
                                                 : BatteryState::OK;
}

// The onboard light is the only status channel that survives having no cable,
// no phone and no Wi-Fi client. Priority order is documented in StatusLed.h.
void updateStatusLed(uint32_t now) {
  // Reading the station list is not free; the light does not need it at 500 Hz.
  if (now - lastApPollMs >= 500) {
    lastApPollMs = now;
    apStationCount = maintenanceApActive
                         ? static_cast<uint8_t>(WiFi.softAPgetStationNum())
                         : 0;
  }
  status::Inputs inputs;
  inputs.batteryLockout = batteryLockout;
  inputs.apUp = maintenanceApActive || gatewayLink.online();
  inputs.webClient = apStationCount > 0;
  inputs.bleConnected = bleConnected;
  inputs.armed = commandArmed;
  inputs.driving = leftMotor.currentPercent != 0 || rightMotor.currentPercent != 0;
  inputs.sensorFault = !sensorTaskStarted || !sensorData.busStarted ||
                       sensorData.power.meta.health == sensors::SensorHealth::OFFLINE;
  statusLed.update(inputs, now);
}

int dutyFromPercent(int percent) {
  percent = constrain(abs(percent), 0, 100);
  return map(percent, 0, 100, 0, PWM_MAX_DUTY);
}

void setMotorEnabled(const Motor &motor, bool enabled) {
  digitalWrite(motor.enablePinA, enabled ? HIGH : LOW);
  if (motor.separateEnablePins) {
    digitalWrite(motor.enablePinB, enabled ? HIGH : LOW);
  }
}

void applyMotor(Motor &motor, int8_t signedPercent) {
  signedPercent = constrain(signedPercent, -100, 100);
  if (motor.inverted) {
    signedPercent = -signedPercent;
  }

  if (signedPercent == 0) {
    ledcWrite(motor.forwardPwmChannel, 0);
    ledcWrite(motor.reversePwmChannel, 0);
    setMotorEnabled(motor, false);
    motor.currentPercent = 0;
    return;
  }

  const int duty = dutyFromPercent(signedPercent);
  setMotorEnabled(motor, true);
  if (signedPercent > 0) {
    ledcWrite(motor.reversePwmChannel, 0);
    ledcWrite(motor.forwardPwmChannel, duty);
  } else {
    ledcWrite(motor.forwardPwmChannel, 0);
    ledcWrite(motor.reversePwmChannel, duty);
  }

  // Store logical, not electrically inverted, direction for telemetry.
  motor.currentPercent = motor.inverted ? -signedPercent : signedPercent;
}

void coastAll() {
  targetLeftPercent = 0;
  targetRightPercent = 0;
  overdrivePending = false;
  applyMotor(leftMotor, 0);
  applyMotor(rightMotor, 0);
}



void forceSafeForUpdate() {
  ControlGuard guard;
  mastController.stop(mast::Reason::SAFETY);mastTest.stop();mastHardware.stop();
  remoteSession.invalidate();
  activeControlSource = ControlSource::NONE;
  commandArmed = false;
  commandDeadman = false;
  commandOverdrive = false;
  watchdogStopped = true;
  coastAll();
}

robotlink::Result applyFieldControl(const robotlink::Control &c,bool viaLink) {
  ControlGuard guard;
  const ControlSource source=viaLink?ControlSource::LINK:ControlSource::FIELD;
  if(batteryInhibitsMotion()){remoteSession.invalidate();return robotlink::Result::INVALID;}
  const bool busy=activeControlSource!=source&&commandArmed&&commandDeadman&&(targetLeftPercent||targetRightPercent);
  auto result=remoteSession.validate(c,millis(),busy);
  if(result!=robotlink::Result::ACCEPTED){
    if(!remoteSession.armed&&activeControlSource==source)forceSafeForUpdate();
    return result;
  }
  activeControlSource=remoteSession.armed?source:ControlSource::NONE;
  commandArmed=remoteSession.armed;commandDeadman=c.operation==2&&c.deadman;
  commandOverdrive=c.overdrive;selectedPowerMode=c.overdrive?0xff:c.power;
  targetLeftPercent=c.left;targetRightPercent=c.right;lastCommandMs=millis();
  watchdogStopped=false;overdrivePending=commandOverdrive&&commandDeadman;
  if(!commandArmed||!commandDeadman)coastAll();
  return result;
}
void serviceFieldControl(uint32_t now){
  ControlGuard guard;
  if(remoteSession.armed&&activeControlSource!=ControlSource::LINK&&activeControlSource!=ControlSource::FIELD)remoteSession.invalidate();
  const uint32_t generation=gatewayLink.generation();
  const bool changed=generation!=linkGeneration;linkGeneration=generation;
  if(revokeRemote.exchange(false)||(changed&&(activeControlSource==ControlSource::LINK||!remoteSession.armed))){
    remoteSession.invalidate();
    if(activeControlSource==ControlSource::LINK){forceSafeForUpdate();appendLog("Gladiator Link changed; field session revoked");}
  }
  if(remoteSession.expire(now)&&(activeControlSource==ControlSource::LINK||activeControlSource==ControlSource::FIELD))forceSafeForUpdate();
  gateway::PendingControl pending;
  for(unsigned budget=0;budget<6&&gatewayLink.takeControl(pending);++budget){
    auto result=robotlink::Result::OFFLINE;
    if(gatewayLink.online()&&pending.generation==linkGeneration&&uint32_t(now-pending.receivedMs)<=150)
      result=applyFieldControl(pending.command,true);
    gatewayLink.result(pending.command,result,remoteSession.epoch);
  }
}
String fieldStateJson(){
  JsonDocument doc;doc["requiresKey"]=false;doc["connected"]=true;doc["interface"]="s3-recovery";
  auto c=doc["controller"].to<JsonObject>();c["s3Ms"]=millis();c["epoch"]=remoteSession.epoch;c["client"]=remoteSession.client;
  c["armed"]=commandArmed;c["deadman"]=commandDeadman;c["owner"]=uint8_t(activeControlSource);
  c["state"]=interfacev1::name(currentInterfaceState());
  c["battery"]=batteryStateName(batteryState);c["power"]=selectedPowerMode;
  c["leftPercent"]=leftMotor.currentPercent;c["rightPercent"]=rightMotor.currentPercent;
  c["watchdogStopped"]=watchdogStopped;c["batteryLockout"]=batteryLockout;
  c["benchPower"]=benchPowerActive;
  c["benchPowerSource"]=benchPowerSourceName();
  doc["volts"]=sensorData.power.derived.busVolts;doc["amps"]=sensorData.power.derived.currentAmps;
  doc["powerValid"]=ina226SampleValid;
  doc["transport"]="WIFI";doc["telemetryAgeMs"]=0;
  auto robot=doc["sensors"].to<JsonObject>();
  auto meta=[&](const char *key,const sensors::SensorMeta &m){
    auto o=robot[key]["meta"].to<JsonObject>();o["health"]=sensors::healthName(m.health);
    o["hasSample"]=m.hasSample;if(m.hasSample)o["ageMs"]=uint32_t(millis()-m.lastUpdateMs);else o["ageMs"]=nullptr;
  };
  meta("power",sensorData.power.meta);meta("imu",sensorData.imu.meta);
  meta("tofFront",sensorData.tofFront.meta);meta("presence",sensorData.presence.meta);
  robot["imu"]["derived"]["headingValid"]=sensors::headingValid(sensorData.imu,millis());
  robot["imu"]["derived"]["yawDeg"]=sensorData.imu.derived.yawDeg;
  robot["tofFront"]["derived"]["valid"]=sensorData.tofFront.derived.valid;
  robot["tofFront"]["derived"]["nearestMm"]=sensorData.tofFront.derived.nearestMm;
  robot["presence"]["derived"]["presenceValid"]=sensorData.presence.derived.presenceValid;
  robot["presence"]["derived"]["present"]=sensorData.presence.derived.present;
  String s;serializeJson(doc,s);return s;
}
// Radio policy stays on the S3. Recovery AP is independent of association to C6.
void updateLinkRadios(uint32_t now){
  static uint32_t checked=0,lastAttempt=0,radioChangedMs=0,wantedSince=0;static bool stationMode=false;
  // Starting the station interface begins an asynchronous scan. Tearing Wi-Fi down while
  // it runs corrupts the driver (panics 2026-10-09: wifi task in scan_parse_beacon after
  // WIFI_OFF, 0.6 s after bring-up). A bring-up must therefore dwell before teardown, and
  // a teardown must settle before the next bring-up.
  constexpr uint32_t RADIO_MIN_UP_MS=15000,RADIO_MIN_DOWN_MS=1000;
  if(batteryRadiosShed||now-checked<500)return;checked=now;
  const bool wantedNow=gatewayLink.needsWifi();
  // A real UART loss already lasts LOSS_MS, so insisting on 1 s of continuous demand only
  // filters transients; it never delays a genuine failover noticeably.
  if(!wantedNow)wantedSince=0;else if(!wantedSince)wantedSince=now?now:1;
  const bool wanted=wantedNow&&wantedSince&&now-wantedSince>=1000;
  if(wanted&&!stationMode&&now>robotlink::LOSS_MS&&now-radioChangedMs>=RADIO_MIN_DOWN_MS){
    radioChangedMs=now;
    // The core re-applies its stored sleep mode on STA_START. WIFI_PS_NONE with BLE
    // up (software coexistence) aborts in the Wi-Fi task (panic 2026-10-09, pm_set_sleep_type),
    // so the stored mode must already be modem sleep before the station interface starts.
    WiFi.setSleep(true);WiFi.mode(WIFI_AP_STA);
    if(!maintenanceApActive){maintenanceApActive=WiFi.softAP(MAINTENANCE_AP_NAME,MAINTENANCE_AP_PASSWORD);maintenanceServer.begin();ArduinoOTA.begin();}
    stationMode=true;lastAttempt=now;WiFi.begin(robotlink::FIELD_SSID,robotlink::FIELD_PASSWORD);
    appendLog("Gladiator Link: seeking C6 field network; S3 recovery AP available");
  }else if(wanted&&stationMode&&WiFi.status()!=WL_CONNECTED&&now-lastAttempt>=10000){lastAttempt=now;WiFi.begin(robotlink::FIELD_SSID,robotlink::FIELD_PASSWORD);}
  if(!wantedNow&&now>15000&&now-radioChangedMs>=RADIO_MIN_UP_MS&&apStationCount==0&&activeControlSource!=ControlSource::WEB&&activeControlSource!=ControlSource::FIELD){
    if(maintenanceApActive||stationMode){
      maintenanceServer.stop();
      // Stop the station (and any scan) first, then the AP, letting each settle before OFF.
      // softAPdisconnect(true)/disconnect(true) would also switch the radio off, making
      // the WIFI_OFF below a second deinit that logs driver errors on every shed.
      if(stationMode){WiFi.disconnect();delay(50);}
      WiFi.softAPdisconnect(false);delay(50);
      WiFi.mode(WIFI_OFF);
      maintenanceApActive=false;stationMode=false;radioChangedMs=now;
      appendLog("Stable UART: S3 Wi-Fi off; BLE service remains available");
    }
  }
}

void setupMaintenanceWifi() {
  WiFi.mode(WIFI_AP);
  // Sleep mode only matters to the station interface, but the core stores it and
  // applies it whenever STA starts later (see updateLinkRadios), so never store NONE.
  WiFi.setSleep(true);
  if (!WiFi.softAP(MAINTENANCE_AP_NAME, MAINTENANCE_AP_PASSWORD)) {
    maintenanceApActive = false;
    appendLog("Maintenance Wi-Fi AP failed to start");
    return;
  }
  maintenanceApActive = true;

  maintenanceServer.on("/", HTTP_GET, []() {
    maintenanceServer.sendHeader("Location","/service");maintenanceServer.send(302,"text/plain","S3 recovery service");
  });
  maintenanceServer.on("/field", HTTP_GET, []() {maintenanceServer.send_P(200,"text/html",FIELD_PAGE);});
  maintenanceServer.on("/service", HTTP_GET, []() {
    maintenanceServer.send_P(200, "text/html", SERVICE_PAGE);
  });
  maintenanceServer.on("/api/field",HTTP_GET,[](){maintenanceServer.sendHeader("Cache-Control","no-store");maintenanceServer.send(200,"application/json",fieldStateJson());});
  maintenanceServer.on("/api/control",HTTP_POST,[](){
    JsonDocument doc;robotlink::Control c;
    if(maintenanceServer.arg("plain").length()>512||deserializeJson(doc,maintenanceServer.arg("plain"))||!robotlink::controlFromJson(doc,c)){
      maintenanceServer.send(400,"text/plain","Invalid field command");return;
    }
    auto r=applyFieldControl(c,false);JsonDocument reply;reply["result"]=uint8_t(r);reply["epoch"]=remoteSession.epoch;
    String text;serializeJson(reply,text);maintenanceServer.send(r==robotlink::Result::ACCEPTED?200:409,"application/json",text);
  });
  maintenanceServer.on("/api/sensors", HTTP_GET, []() {
    maintenanceServer.sendHeader("Cache-Control", "no-store");
    maintenanceServer.send(200, "application/json", sensorStatusJson());
  });
  maintenanceServer.on("/api/gateway", HTTP_GET, []() {
    maintenanceServer.sendHeader("Cache-Control", "no-store");
    maintenanceServer.send(200, "application/json", gatewayLink.statusJson());
  });
  maintenanceServer.on("/api/logs", HTTP_GET, []() {
    maintenanceServer.send(200, "text/plain", webLog);
  });
  maintenanceServer.on("/api/status", HTTP_GET, []() {
    String json;
    json.reserve(512);
    json = "{\"state\":\"" + String(interfacev1::name(currentInterfaceState())) + "\",\"armed\":";
    json += commandArmed ? "true" : "false";
    json += ",\"controlSource\":\"";
    json += controlSourceName(activeControlSource);
    json += '"';
    json += ",\"inaPresent\":";
    json += ina226Present ? "true" : "false";
    json += ",\"inaValid\":" + String(ina226SampleValid ? "true" : "false");
    json += ",\"inaAddress\":" + String(ina226Address);
    json += ",\"volts\":" + String(busMillivolts / 1000.0F, 3);
    json += ",\"amps\":" + String(currentMilliamps / 1000.0F, 3);
    json += ",\"watts\":" + String(powerMilliwatts / 1000.0F, 3);
    json += ",\"minimumVolts\":" +
            String(minimumBusMillivolts == UINT16_MAX
                       ? 0.0F
                       : minimumBusMillivolts / 1000.0F,
                   3);
    json += ",\"peakAmps\":" + String(peakCurrentMilliamps / 1000.0F, 3);
    json += ",\"led\":\"";
    json += status::conditionName(statusLed.condition());
    json += "\",\"apUp\":";
    json += maintenanceApActive ? "true" : "false";
    json += ",\"apStations\":" + String(apStationCount);
    json += ",\"resetReason\":\"";
    json += status::resetReasonName(bootReason);
    json += "\",\"resetRaw\":" + String(bootRawReason);
    json += ",\"bootCount\":" + String(bootCount);
    json += ",\"otaPartition\":\"" + String(otaPartition) + "\"";
    json += ",\"otaStateAtBoot\":\"" + String(otaStateName) + "\"";
    json += ",\"otaMarkedValid\":" + String(otaMarkedValid ? "true" : "false");
    json += ",\"battery\":\"" + String(batteryStateName(batteryState)) + "\"";
    json += ",\"batteryLockout\":" + String(batteryLockout ? "true" : "false");
    json += ",\"benchPower\":" + String(benchPowerActive ? "true" : "false");
    json += ",\"benchPowerSource\":\"" + String(benchPowerSourceName()) + "\"";
    json += ",\"powerSource\":\"" + String(benchPowerActive ? "BENCH" : "LIPO") + "\"";
    json += ",\"benchDetectPin\":" + String(board::BENCH_POWER_DETECT_PIN);
    json += ",\"batteryCells\":" + String(board::BATTERY_CELLS);
    json += ",\"voltsPerCell\":" + String(ina226SampleValid ? busMillivolts / 1000.0F / board::BATTERY_CELLS : 0.0F, 3);
    json += ",\"lockoutVolts\":" + String(board::BATTERY_LOCKOUT_V, 2);
    json += '}';
    maintenanceServer.send(200, "application/json", json);
  });
  maintenanceServer.on("/api/drive", HTTP_POST, []() {
    ControlGuard guard;
    if (batteryInhibitsMotion()) {
      coastAll();
      maintenanceServer.send(503, "text/plain",
                             "Battery lockout: pack too low, motors disabled");
      return;
    }
    if (activeControlSource != ControlSource::WEB && activeControlSource != ControlSource::NONE && commandArmed &&
        commandDeadman &&
        (targetLeftPercent != 0 || targetRightPercent != 0)) {
      maintenanceServer.send(409, "text/plain",
                             "BLE controller is actively driving");
      return;
    }

    const int left = maintenanceServer.arg("left").toInt();
    const int right = maintenanceServer.arg("right").toInt();
    const int requestedPower = maintenanceServer.arg("power").toInt();
    const bool armed = maintenanceServer.arg("armed") == "1";
    const bool deadman = maintenanceServer.arg("deadman") == "1";
    const bool overdrive = maintenanceServer.arg("overdrive") == "1";
    if(!armed)webMustRelease=false;
    if(webMustRelease&&armed){maintenanceServer.send(409,"text/plain","Release and arm again after timeout");return;}
    if (left < -100 || left > 100 || right < -100 || right > 100 ||
        requestedPower < 0 || requestedPower > 100) {
      forceSafeForUpdate();
      maintenanceServer.send(400, "text/plain", "Invalid drive command");
      return;
    }

    remoteSession.invalidate();
    activeControlSource = ControlSource::WEB;
    commandArmed = armed;
    commandDeadman = deadman;
    commandOverdrive = overdrive;
    targetLeftPercent = static_cast<int8_t>(left);
    targetRightPercent = static_cast<int8_t>(right);
    selectedPowerMode = overdrive ? 0xFF : requestedPower;
    ++lastSequence;
    lastCommandMs = millis();
    watchdogStopped = false;
    overdrivePending = overdrive && deadman;
    if (!armed || !deadman || (left == 0 && right == 0)) {
      coastAll();
    }
    if (!armed) {
      activeControlSource = ControlSource::NONE;
    }
    maintenanceServer.send(200, "application/json", "{\"accepted\":true}");
  });
  maintenanceServer.on("/api/command", HTTP_POST, []() {
    ControlGuard guard;
    String command = maintenanceServer.arg("command");
    command.trim();
    command.toLowerCase();
    if (command.isEmpty() || command.length() > 64) {
      maintenanceServer.send(400, "text/plain", "Enter a command up to 64 characters.");
      return;
    }

    if (command == "sensors") {
      maintenanceServer.send(200, "application/json", sensorStatusJson());
      return;
    }
    if (command == "help") {
      maintenanceServer.send(
          200, "text/plain",
          "help | status | sensors | stop | power 25|50|75|100|overdrive | "
          "pulse forward|reverse|left|right [power] | drive <left> <right>\n"
          "Motion is a single watchdog-limited pulse; use the buttons for held driving.");
      return;
    }
    if (command == "status") {
      String reply = commandArmed ? "ARMED" : "SAFE";
      reply += " | source=";
      reply += activeControlSource == ControlSource::WEB
                   ? "WEB"
                   : (activeControlSource == ControlSource::BLE ? "BLE" : "NONE");
      reply += " | left=" + String(leftMotor.currentPercent);
      reply += "% right=" + String(rightMotor.currentPercent);
      reply += "% | INA226=";
      reply += ina226Present ? "0x" + String(ina226Address, HEX) : "not found";
      maintenanceServer.send(200, "text/plain", reply);
      return;
    }
    if (command == "stop" || command == "disarm") {
      forceSafeForUpdate();
      appendLog("Web console STOP; motors coasted and disarmed");
      maintenanceServer.send(200, "text/plain", "SAFE: motors coasted and disarmed.");
      return;
    }

    if (activeControlSource != ControlSource::WEB && activeControlSource != ControlSource::NONE && commandArmed &&
        commandDeadman &&
        (targetLeftPercent != 0 || targetRightPercent != 0)) {
      maintenanceServer.send(409, "text/plain",
                             "BLE controller is actively driving.");
      return;
    }

    auto parsePower = [](String value, uint8_t &percent,
                         bool &overdrive) -> bool {
      value.trim();
      overdrive = value == "overdrive";
      if (overdrive) {
        percent = 100;
        return true;
      }
      const int parsed = value.toInt();
      if ((parsed != 25 && parsed != 50 && parsed != 75 && parsed != 100) ||
          value != String(parsed)) {
        return false;
      }
      percent = static_cast<uint8_t>(parsed);
      return true;
    };

    if (command.startsWith("power ")) {
      uint8_t percent = 0;
      bool overdrive = false;
      if (!parsePower(command.substring(6), percent, overdrive)) {
        maintenanceServer.send(400, "text/plain",
                               "Power must be 25, 50, 75, 100, or overdrive.");
        return;
      }
      selectedPowerMode = overdrive ? 0xFF : percent;
      commandOverdrive = overdrive;
      coastAll();
      appendLog("Web console power set to " +
                String(overdrive ? "OVERDRIVE" : String(percent) + "%"));
      maintenanceServer.send(200, "text/plain",
                             overdrive ? "Power set to OVERDRIVE."
                                       : "Power set to " + String(percent) + "%.");
      return;
    }

    int left = 0;
    int right = 0;
    uint8_t pulsePower = selectedPowerMode == 0xFF ? 100 : selectedPowerMode;
    bool pulseOverdrive = selectedPowerMode == 0xFF;
    bool validMotion = false;
    if (command.startsWith("pulse ")) {
      String arguments = command.substring(6);
      arguments.trim();
      const int separator = arguments.indexOf(' ');
      const String direction =
          separator < 0 ? arguments : arguments.substring(0, separator);
      if (separator >= 0 &&
          !parsePower(arguments.substring(separator + 1), pulsePower,
                      pulseOverdrive)) {
        maintenanceServer.send(400, "text/plain",
                               "Pulse power must be 25, 50, 75, 100, or overdrive.");
        return;
      }
      if (direction == "forward") {
        left = pulsePower;
        right = pulsePower;
        validMotion = true;
      } else if (direction == "reverse") {
        left = -pulsePower;
        right = -pulsePower;
        validMotion = true;
      } else if (direction == "left") {
        left = pulsePower;
        right = -pulsePower;
        validMotion = true;
      } else if (direction == "right") {
        left = -pulsePower;
        right = pulsePower;
        validMotion = true;
      }
    } else if (command.startsWith("drive ")) {
      String values = command.substring(6);
      values.trim();
      const int separator = values.indexOf(' ');
      if (separator > 0 && values.indexOf(' ', separator + 1) < 0) {
        const String leftText = values.substring(0, separator);
        const String rightText = values.substring(separator + 1);
        left = leftText.toInt();
        right = rightText.toInt();
        validMotion = left >= -100 && left <= 100 && right >= -100 &&
                      right <= 100 && leftText == String(left) &&
                      rightText == String(right);
        pulseOverdrive = false;
      }
    }

    if (!validMotion) {
      maintenanceServer.send(400, "text/plain",
                             "Unknown or invalid command. Send 'help' for syntax.");
      return;
    }
    if (left == 0 && right == 0) {
      forceSafeForUpdate();
      maintenanceServer.send(200, "text/plain", "SAFE: zero command coasted both motors.");
      return;
    }

    if (batteryInhibitsMotion()) {
      coastAll();
      maintenanceServer.send(503, "text/plain",
                             "Battery lockout: pack too low, motion refused");
      return;
    }

    activeControlSource = ControlSource::WEB;
    commandArmed = true;
    commandDeadman = true;
    commandOverdrive = pulseOverdrive;
    targetLeftPercent = static_cast<int8_t>(left);
    targetRightPercent = static_cast<int8_t>(right);
    selectedPowerMode = pulseOverdrive ? 0xFF : pulsePower;
    ++lastSequence;
    lastCommandMs = millis();
    watchdogStopped = false;
    overdrivePending = pulseOverdrive;
    appendLog("Web console pulse: left=" + String(left) +
              "% right=" + String(right) + "%" +
              (pulseOverdrive ? " OVERDRIVE" : ""));
    maintenanceServer.send(
        200, "text/plain",
        "Motion accepted for one 350 ms watchdog-limited pulse.");
  });
  // Native sequential OTA gives exact validation/activation errors and retains
  // the running slot on every failure. No image validation is bypassed.
  maintenanceServer.on("/update", HTTP_POST,
      [](){
        maintenanceServer.send(webOtaComplete?200:500,"text/plain",webOtaComplete?"Update complete. Gladiator is rebooting.":String("Update failed: ")+esp_err_to_name(webOtaError)+"; current firmware retained.");
        if(webOtaComplete){delay(250);ESP.restart();}
      },
      [](){
        HTTPUpload &upload=maintenanceServer.upload();
        if(upload.status==UPLOAD_FILE_START){
          forceSafeForUpdate();webOtaComplete=false;webOtaHandle=0;webOtaPartition=esp_ota_get_next_update_partition(nullptr);
          webOtaError=webOtaPartition?esp_ota_begin(webOtaPartition,OTA_WITH_SEQUENTIAL_WRITES,&webOtaHandle):ESP_ERR_NOT_FOUND;
          appendLogf("Native web OTA begin: %s",esp_err_to_name(webOtaError));
        }else if(upload.status==UPLOAD_FILE_WRITE){
          if(webOtaError==ESP_OK)webOtaError=esp_ota_write(webOtaHandle,upload.buf,upload.currentSize);
          feedLoopWDT();
        }else if(upload.status==UPLOAD_FILE_END){
          if(webOtaError==ESP_OK){webOtaError=esp_ota_end(webOtaHandle);webOtaHandle=0;}
          if(webOtaError==ESP_OK)webOtaError=esp_ota_set_boot_partition(webOtaPartition);
          if(webOtaHandle){esp_ota_abort(webOtaHandle);webOtaHandle=0;}
          webOtaComplete=webOtaError==ESP_OK;
          appendLogf("Native web OTA %u bytes: %s",upload.totalSize,esp_err_to_name(webOtaError));
        }else if(upload.status==UPLOAD_FILE_ABORTED){
          if(webOtaHandle)esp_ota_abort(webOtaHandle);webOtaHandle=0;webOtaError=ESP_FAIL;webOtaComplete=false;
          appendLog("Native web OTA aborted; running slot retained");
        }
      });
  maintenanceServer.begin();

  ArduinoOTA.setHostname("gladiator-s3");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    forceSafeForUpdate();
    appendLog("PlatformIO OTA starting; motors coasted and disarmed");
  });
  ArduinoOTA.onEnd([]() { appendLog("PlatformIO OTA complete; rebooting"); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) { feedLoopWDT(); });
  ArduinoOTA.onError([](ota_error_t error) {
    appendLogf("PlatformIO OTA failed: error %u", error);
  });
  ArduinoOTA.begin();

  appendLogf("Maintenance Wi-Fi: %s at http://%s", MAINTENANCE_AP_NAME,
             WiFi.softAPIP().toString().c_str());
}

int8_t rampToward(int8_t current, int8_t target) {
  if (current == target) {
    return current;
  }

  // A direction reversal always crosses zero before energizing the other PWM
  // input, which avoids commanding both halves of a BTS7960 during transition.
  if ((current > 0 && target < 0) || (current < 0 && target > 0)) {
    target = 0;
  }

  if (current < target) {
    return min<int>(current + RAMP_STEP_PERCENT, target);
  }
  return max<int>(current - RAMP_STEP_PERCENT, target);
}

// Straight-line trim for both directions. Only equal, non-zero commands are
// straight-ahead; turns carry their own differential and Overdrive is untrimmed
// by design.
void applyStraightTrim(int8_t &left, int8_t &right) {
  // Keep the installed mast-front trim until the actual event floor is rehearsed.
  // The rear-frame tables above are retained as measurement provenance.
  orientation::straightTrim(left, right, commandOverdrive);
}

void updateMotorOutputs() {
  ControlGuard guard;
  if(batteryInhibitsMotion()){coastAll();return;}
  const bool controlSourceOnline =
      activeControlSource == ControlSource::WEB ||
      (activeControlSource == ControlSource::FIELD && remoteSession.armed) ||
      (activeControlSource == ControlSource::LINK && gatewayLink.online() && remoteSession.armed) ||
      (activeControlSource == ControlSource::BLE && bleConnected);
  if (!controlSourceOnline || !commandArmed || !commandDeadman) {
    if (leftMotor.currentPercent != 0 || rightMotor.currentPercent != 0) {
      coastAll();
    }
    return;
  }

  int8_t outputLeft = targetLeftPercent;
  int8_t outputRight = targetRightPercent;
  applyStraightTrim(outputLeft, outputRight);
  drivepower::cap(outputLeft,outputRight,selectedPowerMode,commandOverdrive);

  // Overdrive intentionally bypasses soft start and commands full-duty values
  // immediately. Direction and magnitude still come from the signed frame.
  if (commandOverdrive && overdrivePending) {
    overdrivePending = false;
    applyMotor(leftMotor, outputLeft);
    applyMotor(rightMotor, outputRight);
    return;
  }

  const uint32_t now = millis();
  if (now - lastRampMs < RAMP_INTERVAL_MS) {
    return;
  }
  lastRampMs = now;

  int8_t nextLeft = rampToward(leftMotor.currentPercent, outputLeft);
  int8_t nextRight = rampToward(rightMotor.currentPercent, outputRight);
  drivepower::cap(nextLeft,nextRight,selectedPowerMode,commandOverdrive);
  applyMotor(leftMotor, nextLeft);
  applyMotor(rightMotor, nextRight);
}

void publishStatus(bool force = false) {
  if (!bleConnected || statusCharacteristic == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if (!force && now - lastStatusMs < STATUS_INTERVAL_MS) {
    return;
  }
  lastStatusMs = now;

  uint8_t frame[FRAME_SIZE] = {};
  frame[0] = FRAME_MAGIC_STATUS;
  frame[1] = PROTOCOL_VERSION;
  frame[2] = (bleConnected ? 0x01 : 0x00) |
             (commandArmed ? 0x02 : 0x00) |
             ((leftMotor.currentPercent != 0 || rightMotor.currentPercent != 0) ? 0x04 : 0x00) |
             (watchdogStopped ? 0x08 : 0x00) |
             (commandOverdrive ? 0x10 : 0x00);
  frame[3] = static_cast<uint8_t>(leftMotor.currentPercent);
  frame[4] = static_cast<uint8_t>(rightMotor.currentPercent);
  frame[5] = selectedPowerMode;
  frame[6] = lastSequence;
  frame[7] = checksum(frame, FRAME_SIZE - 1);

  statusCharacteristic->setValue(frame, sizeof(frame));
  statusCharacteristic->notify();
}

void publishPowerTelemetry(bool force = false) {
  if (!bleConnected || powerCharacteristic == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if (!force && now - lastStatusMs < STATUS_INTERVAL_MS) {
    return;
  }

  uint8_t frame[POWER_FRAME_SIZE] = {};
  frame[0] = POWER_FRAME_MAGIC;
  frame[1] = PROTOCOL_VERSION;
  frame[2] = (ina226Present ? 0x01 : 0x00) |
             (ina226SampleValid ? 0x02 : 0x00) |
             (currentMilliamps < 0 ? 0x04 : 0x00) |
             ((leftMotor.currentPercent != 0 || rightMotor.currentPercent != 0)
                  ? 0x08
                  : 0x00);
  frame[3] = ina226Address;
  writeLe16(frame, 4, busMillivolts);
  writeLe32(frame, 6, currentMilliamps);
  writeLe32(frame, 10, powerMilliwatts);
  writeLe16(frame, 14,
            minimumBusMillivolts == UINT16_MAX ? 0 : minimumBusMillivolts);
  writeLe16(frame, 16, peakCurrentMilliamps);
  frame[18] = ina226SampleSequence;
  frame[19] = checksum(frame, POWER_FRAME_SIZE - 1);

  powerCharacteristic->setValue(frame, sizeof(frame));
  powerCharacteristic->notify();
}

class CommandCallbacks final : public BLECharacteristicCallbacks {
  void rejectInput(){
    bleMustRelease=true;
    if(activeControlSource==ControlSource::BLE){
      commandArmed=commandDeadman=commandOverdrive=false;activeControlSource=ControlSource::NONE;
      targetLeftPercent=targetRightPercent=0;watchdogStopped=true;coastAll();
    }
  }
  void onWrite(BLECharacteristic *characteristic) override {
    ControlGuard guard;
    const std::string value = characteristic->getValue();
    if (value.size() != FRAME_SIZE) {
      rejectInput();
      return;
    }

    const auto *frame = reinterpret_cast<const uint8_t *>(value.data());
    if (frame[0] != FRAME_MAGIC_COMMAND ||
        frame[1] != PROTOCOL_VERSION ||
        frame[7] != checksum(frame, FRAME_SIZE - 1)) {
      rejectInput();
      return;
    }

    const int8_t left = static_cast<int8_t>(frame[4]);
    const int8_t right = static_cast<int8_t>(frame[5]);
    if (left < -100 || left > 100 || right < -100 || right > 100) {
      rejectInput();
      return;
    }

    const bool overdrive=(frame[3]&FLAG_OVERDRIVE)!=0;
    if((frame[3]&~uint8_t(FLAG_ARMED|FLAG_DEADMAN|FLAG_OVERDRIVE)) ||
       (overdrive?frame[6]!=0xff:frame[6]>100)){
      rejectInput();return;
    }

    if (activeControlSource != ControlSource::BLE && activeControlSource != ControlSource::NONE && commandArmed &&
        commandDeadman &&
        (targetLeftPercent != 0 || targetRightPercent != 0)) {
      appendLog("BLE command rejected while web controller is actively driving");
      return;
    }
    if (batteryInhibitsMotion()) {
      coastAll();
      return; // guard already logged; do not spam per frame
    }
    if(!(frame[3]&FLAG_ARMED))bleMustRelease=false;
    if(bleMustRelease&&(frame[3]&FLAG_ARMED))return;
    revokeRemote=true;
    activeControlSource = ControlSource::BLE;

    lastSequence = frame[2];
    commandArmed = (frame[3] & FLAG_ARMED) != 0;
    commandDeadman = (frame[3] & FLAG_DEADMAN) != 0;
    commandOverdrive = (frame[3] & FLAG_OVERDRIVE) != 0;
    targetLeftPercent = left;
    targetRightPercent = right;
    selectedPowerMode = frame[6];
    lastCommandMs = millis();
    watchdogStopped = false;
    overdrivePending = commandOverdrive && commandDeadman;

    if (!commandArmed || !commandDeadman || (left == 0 && right == 0)) {
      coastAll();
    }
    publishStatus(true);
    publishPowerTelemetry(true);
  }
};

class ServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    ControlGuard guard;
    bleConnected = true;
    if ((activeControlSource != ControlSource::WEB && activeControlSource != ControlSource::LINK && activeControlSource != ControlSource::FIELD) || !commandArmed) {
      commandArmed = false;
      commandDeadman = false;
      commandOverdrive = false;
      watchdogStopped = false;
      lastCommandMs = millis();
      coastAll();
    }
    appendLog("Phone connected; motors remain disarmed unless web control is active.");
  }

  void onDisconnect(BLEServer *server) override {
    ControlGuard guard;
    bleConnected = false;
    if (activeControlSource == ControlSource::BLE) {
      bleMustRelease=true;
      activeControlSource = ControlSource::NONE;
      commandArmed = false;
      commandDeadman = false;
      commandOverdrive = false;
      watchdogStopped = true;
      coastAll();
      appendLog("Phone disconnected; motors coasted and disarmed.");
    } else {
      appendLog("Phone disconnected; web controller ownership unchanged.");
    }
    delay(40);
    server->getAdvertising()->start();
  }
};

CommandCallbacks commandCallbacks;
ServerCallbacks serverCallbacks;

void setupPwmPin(uint8_t pin, uint8_t channel) {
  ledcSetup(channel, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0);
}

void setupMotors() {
  pinMode(LEFT_R_EN_PIN, OUTPUT);
  pinMode(LEFT_L_EN_PIN, OUTPUT);
  pinMode(RIGHT_R_EN_PIN, OUTPUT);
  pinMode(RIGHT_L_EN_PIN, OUTPUT);

  digitalWrite(LEFT_R_EN_PIN, LOW);
  digitalWrite(LEFT_L_EN_PIN, LOW);
  digitalWrite(RIGHT_R_EN_PIN, LOW);
  digitalWrite(RIGHT_L_EN_PIN, LOW);

  setupPwmPin(LEFT_RPWM_PIN, LEFT_RPWM_CHANNEL);
  setupPwmPin(LEFT_LPWM_PIN, LEFT_LPWM_CHANNEL);
  setupPwmPin(RIGHT_RPWM_PIN, RIGHT_RPWM_CHANNEL);
  setupPwmPin(RIGHT_LPWM_PIN, RIGHT_LPWM_CHANNEL);
  coastAll();
}

void setupBle() {
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(64);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(&serverCallbacks);

  BLEService *service = bleServer->createService(SERVICE_UUID);
  BLECharacteristic *commandCharacteristic = service->createCharacteristic(
      COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(&commandCallbacks);

  statusCharacteristic = service->createCharacteristic(
      STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->addDescriptor(new BLE2902());

  powerCharacteristic = service->createCharacteristic(
      POWER_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  powerCharacteristic->addDescriptor(new BLE2902());

  uint8_t initialStatus[FRAME_SIZE] = {
      FRAME_MAGIC_STATUS, PROTOCOL_VERSION, 0, 0, 0, 25, 0, 0};
  initialStatus[7] = checksum(initialStatus, FRAME_SIZE - 1);
  statusCharacteristic->setValue(initialStatus, sizeof(initialStatus));

  uint8_t initialPower[POWER_FRAME_SIZE] = {};
  initialPower[0] = POWER_FRAME_MAGIC;
  initialPower[1] = PROTOCOL_VERSION;
  initialPower[2] = ina226Present ? 0x01 : 0;
  initialPower[3] = ina226Address;
  initialPower[19] = checksum(initialPower, POWER_FRAME_SIZE - 1);
  powerCharacteristic->setValue(initialPower, sizeof(initialPower));

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  advertising->start();
}

} // namespace

void setup() {
  // Nothing in this project subscribed to the platform task watchdog before
  // this line, despite the SDK shipping one (5 s, panic-reboot) by default:
  // a hang anywhere in loop() - motors, Wi-Fi, BLE, the web server - froze
  // the board forever with no automatic recovery. Enable it first so it also
  // covers a hang during the rest of setup() itself.
  enableLoopWDT();
  commandMutex=xSemaphoreCreateRecursiveMutex();configASSERT(commandMutex);
  pinMode(board::BENCH_POWER_DETECT_PIN, INPUT_PULLUP);
  benchPowerViaPin = digitalRead(board::BENCH_POWER_DETECT_PIN) ==
                     (board::BENCH_POWER_DETECT_ACTIVE_LOW ? LOW : HIGH);
  benchPowerCandidate = benchPowerActive = benchPowerViaPin;
  benchPowerCandidateSinceMs = millis();
  setupMotors();
  mastHardware.begin(); // GPIO10/13 remain detached at boot.
  loadMastProfile(); // Never restores enabled state or current position.

  // Read why the controller restarted before anything else can overwrite it.
  bootReason = esp_reset_reason();
  bootRawReason = static_cast<uint32_t>(rtc_get_reset_reason(0));
  if (rtcBootMagic != BOOT_MAGIC) {
    rtcBootMagic = BOOT_MAGIC;
    rtcBootCount = 0;
  }
  bootCount = ++rtcBootCount;
  statusLed.begin(bootReason, bootRawReason, bootCount);

  // Confirm this image unconditionally. Leaving it PENDING_VERIFY blocks every
  // later OTA at activation, which is indistinguishable from a bad image.
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) {
    strlcpy(otaPartition, running->label, sizeof(otaPartition));
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
      switch (state) {
        case ESP_OTA_IMG_NEW: otaStateName = "NEW"; break;
        case ESP_OTA_IMG_PENDING_VERIFY: otaStateName = "PENDING_VERIFY"; break;
        case ESP_OTA_IMG_VALID: otaStateName = "VALID"; break;
        case ESP_OTA_IMG_INVALID: otaStateName = "INVALID"; break;
        case ESP_OTA_IMG_ABORTED: otaStateName = "ABORTED"; break;
        default: otaStateName = "UNDEFINED"; break;
      }
    }
    otaMarkedValid = esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
  }

  Serial.begin(115200);
  delay(500);
  Serial.println();
  blackbox::begin();
  appendLog("Gladiator tracked robot controller starting");
  appendLogf("Boot #%u since power-on | reset=%s (esp %d, raw 0x%02X) | blink code %u",
             bootCount, status::resetReasonName(bootReason), int(bootReason),
             bootRawReason, statusLed.code());
  appendLogf("OTA slot %s was %s at boot; mark-valid %s", otaPartition, otaStateName,
             otaMarkedValid ? "OK" : "FAILED");
  appendLog("Safety state: outputs disabled, waiting for BLE + ARM + held direction");
  appendLog(benchPowerActive
                ? "Bench power detected at boot on GPIO7; LiPo voltage protection bypassed"
                : "LiPo power policy active; ground GPIO7 only with the bench-power harness");

  sensorTaskStarted = sensorManager.begin();
  remoteSession.epoch=esp_random()|1;
  appendLog(gatewayLink.begin(sensorManager) ? "C6 UART link started on TX4/RX5 at 460800 baud" : "C6 UART task failed to start");
  appendLog(sensorTaskStarted ? "Sensor Core v1: I2C GPIO8/9 worker started" : "Sensor task failed to start");
  setupMaintenanceWifi();
  setupBle();

  appendLog("BLE advertising as Gladiator");
}

// Flight recorder rows (column names are written to the log at boot). Fast rows run at
// 5 Hz while the tracks move; system rows at 1 Hz, or every 5 s once idle for a minute,
// so a long outing fits in the flash partition. Link maxima cover the interval since the
// previous system row, which is what locates a dropout.
void recordBlackBox(uint32_t now) {
  static uint32_t lastFast = 0, lastSlow = 0, lastActiveMs = 0;
  static bool headerWritten = false;
  static BatteryState lastBattery = BatteryState::UNKNOWN;
  if (!headerWritten) {
    headerWritten = true;
    blackbox::line("#,cols,F,ms,left,right,targetLeft,targetRight,yawDeg,rollDeg,pitchDeg,volts,amps,peak250msAmps");
    blackbox::line("#,cols,S,ms,boot,volts,amps,minVolts,peakAmps,state,armed,deadman,owner,left,right,yawDeg,rollDeg,pitchDeg,imuOk,loopMaxUs,heap,minHeap,psram,tempC,battery,apUp,apStations,ble,linkOnline,transport,wantsWifi,uartAlive,uartStable,rx,tx,linkErr,rejected,drops,switches,goodAgeMaxMs,taskGapMaxMs,bbPending,bbDropped,bbWriteMaxMs,bbFileKB,bbUsedKB,bbFsErr,hbTotal,hbNoEcho,echoLag,powerOk,peak1sAmps,min1sVolts");
  }
  const bool moving = leftMotor.currentPercent != 0 || rightMotor.currentPercent != 0;
  if (moving || commandArmed) lastActiveMs = now;
  const auto &imu = sensorData.imu.gameDerived;
  const auto &power = sensorData.power.derived;
  // A dead monitor keeps its last numbers; log them as NaN rather than as live readings.
  const bool powerOk = ina226SampleValid;
  const float volts = powerOk ? power.busVolts : NAN, amps = powerOk ? power.currentAmps : NAN;
  const float minVolts = powerOk ? power.minimumVolts : NAN, peakAmps = powerOk ? power.peakAbsAmps : NAN;
  const float yaw = imu.valid ? imu.yawDeg : NAN, roll = imu.valid ? imu.rollDeg : NAN,
              pitch = imu.valid ? imu.pitchDeg : NAN;

  if (moving && now - lastFast >= 200) {
    lastFast = now;
    char row[160];
    snprintf(row, sizeof row, "F,%lu,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", (unsigned long)now,
             leftMotor.currentPercent, rightMotor.currentPercent, targetLeftPercent, targetRightPercent,
             yaw, roll, pitch, volts, amps,
             powerOk ? power.recentPeakAmps : NAN);
    blackbox::line(row);
  }

  const uint32_t slowInterval = (now - lastActiveMs > 60000) ? 5000 : 1000;
  if (now - lastSlow >= slowInterval) {
    lastSlow = now;
    const auto link = gatewayLink.takeDiagnostics();
    const auto stats = blackbox::takeStats();
    // Flush every second when the battery is low or unmonitored, so an abrupt loss of power
    // costs at most a second of data.
    blackbox::setUrgent(!powerOk || volts < board::BATTERY_WARN_V);
    char row[420];
    snprintf(row, sizeof row,
             "S,%lu,%u,%.3f,%.3f,%.3f,%.3f,%s,%d,%d,%u,%d,%d,%.2f,%.2f,%.2f,%d,%lu,%lu,%lu,%lu,%.1f,%s,%d,%u,%d,%d,%s,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%.3f,%.3f",
             (unsigned long)now, (unsigned)bootCount, volts, amps, minVolts,
             peakAmps, interfacev1::name(currentInterfaceState()), commandArmed, commandDeadman,
             (unsigned)activeControlSource, leftMotor.currentPercent, rightMotor.currentPercent, yaw, roll,
             pitch, imu.valid, (unsigned long)loopMaxUs, (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getFreePsram(), temperatureRead(),
             batteryStateName(batteryState), maintenanceApActive, (unsigned)apStationCount, bleConnected,
             link.online, robotlink::name(link.status.active), link.wantsWifi, link.status.uartAlive,
             link.status.uartStable, (unsigned long)link.status.rxFrames, (unsigned long)link.status.txFrames,
             (unsigned long)link.status.errors, (unsigned long)link.status.rejected,
             (unsigned long)link.status.drops, (unsigned long)link.status.switches,
             (unsigned long)link.goodAgeMaxMs, (unsigned long)link.taskGapMaxMs,
             (unsigned long)stats.pendingBytes, (unsigned long)stats.dropped, (unsigned long)stats.writeMaxMs,
             (unsigned long)(stats.fileBytes / 1024), (unsigned long)(stats.usedBytes / 1024),
             (unsigned long)stats.fsErrors, (unsigned long)link.status.hbTotal,
             (unsigned long)link.status.hbNoEcho, (unsigned long)link.status.echoLag, powerOk ? 1 : 0,
             powerOk ? power.peak1sAmps : NAN, powerOk ? power.min1sVolts : NAN);
    blackbox::line(row);
  }

  if (batteryState != lastBattery) {
    if (lastBattery != BatteryState::UNKNOWN) {
      blackbox::linef("E,%lu,battery state %s -> %s at %.2f V", (unsigned long)now,
                      batteryStateName(lastBattery), batteryStateName(batteryState), power.busVolts);
      blackbox::flushSoon();
    }
    lastBattery = batteryState;
  }
}

void loop() {
  const uint32_t loopStartedUs = micros();
  const uint32_t now = millis();
  if (commandArmed && now - lastCommandMs > COMMAND_TIMEOUT_MS) {
    ControlGuard guard;
    // The release latch stops a held button resuming motion after a link lapse. An armed but
    // idle link has nothing to resume. The phone app arms with ONE frame and only streams
    // while a direction is held, so latching on an idle trip made every later armed frame
    // be dropped until a disarm that the app never sends (drive keys did nothing).
    const bool wasMoving = commandDeadman && (targetLeftPercent != 0 || targetRightPercent != 0);
    webMustRelease=true;if(wasMoving)bleMustRelease=true;remoteSession.invalidate();
    activeControlSource = ControlSource::NONE;
    commandArmed = false;
    commandDeadman = false;
    commandOverdrive = false;
    watchdogStopped = true;
    coastAll();
    appendLog("Command watchdog expired; motors coasted and disarmed.");
    publishStatus(true);
  }

  updateSensors();
  updateBenchPower(now);
  updateBatteryGuard(now);
  serviceFieldControl(now);
  updateMotorOutputs();
  serviceMast();
  updateSerialConsole();
  logIna226();
  publishPowerTelemetry();
  publishStatus();
  maintenanceServer.handleClient();
  ArduinoOTA.handle();
  updateLinkRadios(now);
  updateStatusLed(now);
  recordBlackBox(now);
  gateway::ControllerState linkController;
  linkController.armed=commandArmed;
  linkController.state=currentInterfaceState();linkController.battery=batteryState;
  linkController.deadman=commandDeadman;linkController.owner=uint8_t(activeControlSource);linkController.power=selectedPowerMode;
  linkController.batteryLockout=batteryLockout;linkController.radiosShed=batteryRadiosShed;linkController.benchPower=benchPowerActive;
  linkController.epoch=remoteSession.epoch;linkController.client=remoteSession.client;
  linkController.leftPercent=leftMotor.currentPercent;
  linkController.rightPercent=rightMotor.currentPercent;
  linkController.watchdogStopped=watchdogStopped;
  linkController.sensorTaskStarted=sensorTaskStarted;
  linkController.loopMaxUs=loopMaxUs;
  linkController.bootCount=bootCount;
  gatewayLink.setController(linkController);
  static uint32_t lastSystem=0;
  if(now-lastSystem>=1000){ControlGuard guard;lastSystem=now;JsonDocument system;
    system["firmware"]="Gladiator S3 Interface v1 / Link v2 / Mast operator v14 / I2C bus recovery / 15 Hz range grid / IMU recovery / temporary servo calibration / radio dwell / bounded IMU transport";system["orientation"]=orientation::NAME;system["build"]=__DATE__ " " __TIME__;system["freeHeap"]=ESP.getFreeHeap();
    system["resetReason"]=status::resetReasonName(bootReason);system["bootCount"]=bootCount;system["otaSlot"]=otaPartition;
    system["batteryState"]=batteryStateName(batteryState);system["powerSource"]=benchPowerActive?"BENCH":"LIPO";
    system["benchPower"]=benchPowerActive;system["benchPowerSource"]=benchPowerSourceName();
    system["benchDetectPin"]=board::BENCH_POWER_DETECT_PIN;
    system["radioShed"]=batteryRadiosShed;system["apUp"]=maintenanceApActive;
    system["logTail"]=webLog.substring(webLog.length()>450?webLog.length()-450:0);
    String s;serializeJson(system,s);gatewayLink.setSystem(s,webLog);
  }
  loopMaxUs = max(loopMaxUs, uint32_t(micros()-loopStartedUs));
  delay(2);
}
