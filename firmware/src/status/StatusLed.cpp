#include "StatusLed.h"

namespace status {
namespace {
// Scale a hue component by the global cap and a 0..255 brightness envelope.
inline uint8_t mix(uint16_t channel, uint16_t level) {
  return uint8_t((uint32_t(channel) * board::STATUS_LED_MAX * level) / (255U * 255U));
}
// Raw ROM reset codes for a host driving the USB serial line. Deliberate, not a fault.
constexpr uint32_t RAW_USB_UART_CHIP_RESET = 0x15;
constexpr uint32_t RAW_USB_JTAG_CHIP_RESET = 0x16;
} // namespace

const char *conditionName(Condition condition) {
  switch (condition) {
    case Condition::BOOT_CODE: return "BOOT_CODE";
    case Condition::BATTERY: return "BATTERY_LOW";
    case Condition::DRIVING: return "DRIVING";
    case Condition::ARMED: return "ARMED";
    case Condition::AP_DOWN: return "AP_DOWN";
    case Condition::SENSOR_FAULT: return "SENSOR_FAULT";
    case Condition::BLE_LINK: return "BLE_LINK";
    case Condition::WEB_LINK: return "WEB_LINK";
    default: return "IDLE";
  }
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

uint8_t StatusLed::blinkCode(esp_reset_reason_t reason, uint32_t rawReason) {
  if (rawReason == RAW_USB_UART_CHIP_RESET || rawReason == RAW_USB_JTAG_CHIP_RESET) {
    return 0;
  }
  switch (reason) {
    case ESP_RST_BROWNOUT: return 2;
    case ESP_RST_PANIC: return 3;
    case ESP_RST_TASK_WDT: return 4;
    case ESP_RST_INT_WDT: return 5;
    case ESP_RST_WDT: return 6;
    case ESP_RST_UNKNOWN: return 7;
    default: return 0; // POWERON, EXT, SW, DEEPSLEEP, SDIO are ordinary starts.
  }
}

void StatusLed::begin(esp_reset_reason_t reason, uint32_t rawReason, uint32_t bootCount) {
  (void)bootCount;
  code_ = blinkCode(reason, rawReason);
  startedMs_ = lastWriteMs_ = millis();
  announcing_ = begun_ = true;
  condition_ = Condition::BOOT_CODE;
  red_ = green_ = blue_ = 0;
  neopixelWrite(board::STATUS_LED_PIN, 0, 0, 0);
}

void StatusLed::show(uint8_t r, uint8_t g, uint8_t b, uint32_t nowMs) {
  if (r == red_ && g == green_ && b == blue_) return;
  // Rate limit the RMT transfer; a skipped write is retried on the next pass.
  if (nowMs - lastWriteMs_ < board::STATUS_LED_MIN_WRITE_MS) return;
  lastWriteMs_ = nowMs;
  red_ = r; green_ = g; blue_ = b;
  neopixelWrite(board::STATUS_LED_PIN, r, g, b);
}

void StatusLed::update(const Inputs &in, uint32_t nowMs) {
  if (!begun_) return;
  const uint32_t elapsed = nowMs - startedMs_;

  if (announcing_) {
    if (code_ == 0) {
      if (elapsed < 900) { // Two short green flashes: ordinary start.
        const bool on = (elapsed % 450) < 180;
        show(0, on ? mix(255, 255) : 0, 0, nowMs);
        return;
      }
    } else {
      constexpr uint32_t UNIT = 400, GAP = 1200, REPEATS = 3;
      const uint32_t cycle = code_ * UNIT + GAP;
      if (elapsed < cycle * REPEATS) {
        const uint32_t position = elapsed % cycle;
        const bool on = position < code_ * UNIT && (position % UNIT) < 180;
        show(on ? mix(255, 255) : 0, 0, 0, nowMs);
        return;
      }
    }
    announcing_ = false;
  }

  if (in.batteryLockout) {
    // Outranks everything: motors are already locked out, and once the radios
    // are shed to save the pack this light is the only channel left.
    condition_ = Condition::BATTERY;
    const uint32_t position = nowMs % 1400;
    const bool on = position < 120 || (position >= 240 && position < 360)
                    || (position >= 480 && position < 600);
    show(on ? mix(255, 255) : 0, 0, 0, nowMs);
    return;
  }
  if (in.driving) {
    condition_ = Condition::DRIVING;
    show(mix(255, 255), mix(110, 255), 0, nowMs);
    return;
  }
  if (in.armed) {
    condition_ = Condition::ARMED;
    const bool on = (nowMs % 500) < 250;
    show(on ? mix(255, 255) : 0, on ? mix(110, 255) : 0, 0, nowMs);
    return;
  }
  if (!in.apUp) {
    condition_ = Condition::AP_DOWN;
    const bool on = (nowMs % 1000) < 500;
    show(on ? mix(255, 255) : 0, 0, 0, nowMs);
    return;
  }
  if (in.sensorFault) {
    condition_ = Condition::SENSOR_FAULT;
    const uint32_t position = nowMs % 1600;
    const bool on = position < 150 || (position >= 300 && position < 450);
    show(on ? mix(255, 255) : 0, 0, on ? mix(255, 255) : 0, nowMs);
    return;
  }
  if (in.bleConnected) {
    condition_ = Condition::BLE_LINK;
    show(0, mix(60, 255), mix(255, 255), nowMs);
    return;
  }
  if (in.webClient) {
    condition_ = Condition::WEB_LINK;
    show(0, mix(220, 255), mix(255, 255), nowMs);
    return;
  }
  condition_ = Condition::IDLE;
  // Slow triangle breathe so "healthy and idle" is obvious from across a room.
  const uint32_t phase = nowMs % 4000;
  const uint32_t rising = phase < 2000 ? phase : 4000 - phase;
  show(0, mix(255, uint16_t(12 + (rising * 243) / 2000)), 0, nowMs);
}

} // namespace status
