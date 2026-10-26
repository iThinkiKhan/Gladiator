#pragma once
#include <stdint.h>

namespace board {
// Installed N16R8 ESP32-S3. GPIO19/20 are reserved for native USB.
constexpr uint8_t RIGHT_R_EN = 16, RIGHT_RPWM = 17;
constexpr uint8_t RIGHT_L_EN = 11, RIGHT_LPWM = 12;
constexpr uint8_t LEFT_ENABLE = 2, LEFT_RPWM = 42, LEFT_LPWM = 41;
// Installed mast-front wiring; aliases preserve older bench source compatibility.
constexpr uint8_t LEFT_R_EN=LEFT_ENABLE,LEFT_L_EN=LEFT_ENABLE;
constexpr bool LEFT_SEPARATE_ENABLES=false,RIGHT_SEPARATE_ENABLES=true;
constexpr uint8_t MAST_PAN=10,MAST_TILT=13;
constexpr uint8_t I2C_SDA = 8, I2C_SCL = 9;
constexpr uint32_t I2C_HZ = 400000;
constexpr uint16_t I2C_TIMEOUT_MS = 10;
constexpr float SHUNT_OHMS = 0.002F;
constexpr uint32_t POWER_INTERVAL_MS = 50;
// 0x010F: no hardware averaging, bus 1.1 ms, shunt 204 us, continuous shunt+bus (about a 1.3 ms
// cycle). The old setting (0x0527) averaged 16 readings inside the chip, which flattened short
// current spikes before the firmware could see them. Averaging is now done in software over the
// POWER_INTERVAL_MS window, and every individual reading feeds the peak trackers.
constexpr uint16_t INA226_CONFIG = 0x010F;
// How often the shunt register is read for peak tracking (the bus voltage every 4th read).
constexpr uint32_t POWER_FAST_US = 4000;
constexpr uint32_t SENSOR_RETRY_MS = 5000;
// Crossed UART: S3 TX4 -> C6 RX17, C6 TX16 -> S3 RX5, common ground.
constexpr uint8_t GATEWAY_TX=4, GATEWAY_RX=5;
constexpr uint32_t GATEWAY_BAUD=460800;

// Ground GPIO7 with the bench-power harness/jumper to identify a regulated
// bench source. Voltage/current readings cannot safely distinguish a bench
// supply from a depleted 4S LiPo, so this explicit signal is authoritative.
constexpr uint8_t BENCH_POWER_DETECT_PIN = 7;
constexpr bool BENCH_POWER_DETECT_ACTIVE_LOW = true;
constexpr uint32_t BENCH_POWER_DEBOUNCE_MS = 500;
// Native USB power back-feeds the monitored logic rail near 4.2 V on this
// installed controller. With a live USB host, that range is a USB-bench
// condition for a 4S system; higher-voltage supplies use GPIO7.
constexpr float USB_BENCH_MIN_V = 3.80F;
constexpr float USB_BENCH_MAX_V = 5.50F;

// Onboard WS2812 status light. DevKitC-1 rev 1.x uses GPIO48; some clones and
// early boards use GPIO38. If the light stays dark, try 38 here first.
constexpr uint8_t STATUS_LED_PIN = 48;
// Full-scale WS2812 white is ~60 mA. The status light must not be a meaningful
// load on the battery it reports about, so cap every channel here.
constexpr uint8_t STATUS_LED_MAX = 40;
constexpr uint32_t STATUS_LED_MIN_WRITE_MS = 20;

// IMU mount tilt, measured 2026-09-08 by averaging roll and pitch over exactly
// three full chassis rotations. A whole number of turns cancels any planar
// floor slope, so this is the body-fixed component and stays valid on the
// uneven carpet it was measured on. Yaw has no equivalent: gravity gives no
// heading reference, so a yaw mounting offset is not observable this way and
// is deliberately left at zero.
constexpr float IMU_MOUNT_ROLL_DEG = -10.65F;
constexpr float IMU_MOUNT_PITCH_DEG = 4.96F;

// Pack protection. CELLS is inferred from observed voltage (15.7 V rested with
// charge remaining, 16.8 V being a full 4S) and is the one number every
// threshold below scales from - correct it first if the installed pack differs.
constexpr uint8_t BATTERY_CELLS = 4;
constexpr float CELL_WARN_V = 3.50F;     // annotate only
constexpr float CELL_LOCKOUT_V = 3.30F;  // refuse to arm, force coast
constexpr float CELL_RELEASE_V = 3.45F;  // hysteresis back out of lockout
constexpr float CELL_CRITICAL_V = 3.05F; // shed radios; below 3.0 V/cell damages cells
constexpr float BATTERY_WARN_V = BATTERY_CELLS * CELL_WARN_V;
constexpr float BATTERY_LOCKOUT_V = BATTERY_CELLS * CELL_LOCKOUT_V;
constexpr float BATTERY_RELEASE_V = BATTERY_CELLS * CELL_RELEASE_V;
constexpr float BATTERY_CRITICAL_V = BATTERY_CELLS * CELL_CRITICAL_V;
// Motor surges sag this rail hard - a 70% spin pulled it from 15.9 V to 14.1 V -
// so a threshold has to hold before it acts, or a launch current would trip it.
// Release is slower still so a rebounding pack does not flap the motors back on.
constexpr uint32_t BATTERY_LOCKOUT_MS = 3000;
constexpr uint32_t BATTERY_RELEASE_MS = 8000;
constexpr uint32_t BATTERY_CRITICAL_MS = 15000;
}
