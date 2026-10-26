#include "SensorManager.h"
#include "SensorProgress.h"
#include <rom/ets_sys.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <esp_system.h>

// Survives the watchdog's software reset (not a power-on): how many boots in a
// row ended with the sensor worker hung. Breaks the boot loop a wedged bus causes.
namespace { constexpr uint32_t HANG_MAGIC = 0x5E4B0B5Bu; constexpr uint32_t HANG_LIMIT = 3; }
RTC_NOINIT_ATTR static uint32_t hangMagic, hangCount;

extern "C" void esp_task_wdt_isr_user_handler(void){
  ets_printf("SENSOR WATCHDOG stage=%lu operation=%lu\n",(unsigned long)sensors::sensorStep,(unsigned long)sensors::sensorOperation);
  if (sensors::sensorStep != 0) {
    if (hangMagic != HANG_MAGIC) { hangMagic = HANG_MAGIC; hangCount = 0; }
    ++hangCount;
  }
}
namespace sensors {
volatile uint32_t sensorStep=0,sensorOperation=0;
SensorManager::SensorManager() : power_(bus_, working_.power), imu_(bus_, working_.imu),
    tof_(bus_, working_.tofFront), presence_(bus_, working_.presence) {
  published_ = working_;
}
bool SensorManager::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;
  // A driver call that blocks on a wedged I2C device must not freeze silently
  // forever; the task watchdog turns a genuine hang into a reboot. After
  // HANG_LIMIT such reboots in a row, stay up without that watchdog instead:
  // drive, link and display keep working and the sensor fault is reported.
  const esp_reset_reason_t why = esp_reset_reason();
  if (hangMagic != HANG_MAGIC || (why != ESP_RST_PANIC && why != ESP_RST_TASK_WDT && why != ESP_RST_INT_WDT)) {
    hangMagic = HANG_MAGIC; hangCount = 0;
  }
  working_.hangResets = hangCount;
  working_.busWatchdog = hangCount < HANG_LIMIT;
  published_ = working_;
  // Core 0 owns sensor I/O. The Arduino motor/control loop runs on core 1.
  if (xTaskCreatePinnedToCore(taskEntry, "sensors", 8192, this, 1, &task_, 0) != pdPASS) return false;
  if (working_.busWatchdog) esp_task_wdt_add(task_);
  else ets_printf("SENSOR WATCHDOG OFF after %lu hangs in a row; power-cycle to retry sensors\n", (unsigned long)hangCount);
  return true;
}
bool SensorManager::snapshot(RobotSensors &out) {
  if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE) return false;
  out = published_;
  xSemaphoreGive(mutex_);
  // Age the consumer copy as well: a stalled worker must not leave an OK badge.
  const uint32_t now = millis();
  out.power.meta.age(now, 250, 1500);
  out.imu.meta.age(now, 300, 2000);
  out.tofFront.meta.age(now,500,2500);
  out.presence.meta.age(now,800,2500);
  if(!out.tofFront.meta.hasSample || now-out.tofFront.meta.lastUpdateMs>500) out.tofFront.derived.valid=false;
  if(!out.presence.meta.hasSample || now-out.presence.meta.lastUpdateMs>800) {
    out.presence.derived.presenceValid=false; out.presence.derived.targetValid=false;
  }
  if (!out.imu.reports[0].hasSample || now-out.imu.reports[0].lastUpdateMs>300) out.imu.derived.valid=false;
  return true;
}
void SensorManager::taskEntry(void *context) { static_cast<SensorManager *>(context)->run(); }
void SensorManager::run() {
  working_.busStarted = bus_.begin();
  for (;;) {
    const uint64_t start = esp_timer_get_time();
    if (working_.busStarted) {
      // Each step starts on an idle bus; a line still held low skips that step
      // rather than handing the driver a bus it can hang on.
      if (bus_.ensureIdle()) { sensorProgress(SensorStep::POWER);power_.update(); }
      if (bus_.ensureIdle()) { sensorProgress(SensorStep::IMU);imu_.update(); }
      if (bus_.ensureIdle()) { sensorProgress(SensorStep::RANGE);tof_.update(); }
      if (bus_.ensureIdle()) { sensorProgress(SensorStep::PRESENCE);presence_.update(); }
      working_.busStuck = bus_.stuckEvents(); working_.busRecoveries = bus_.recoveries();
    }
    else {
      working_.power.meta.health = working_.imu.meta.health = SensorHealth::ERROR;
    }
    working_.workerMaxUs = max(working_.workerMaxUs, uint32_t(esp_timer_get_time()-start));
    working_.snapshotUs = esp_timer_get_time();
    if (xSemaphoreTake(mutex_, 0) == pdTRUE) {
      published_ = working_;
      xSemaphoreGive(mutex_);
    }
    sensorProgress(SensorStep::IDLE);
    // A minute of normal passes ends the hang streak.
    if (hangCount && millis() > 60000) hangCount = 0;
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}
