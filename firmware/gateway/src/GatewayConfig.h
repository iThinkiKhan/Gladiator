#pragma once
#include "driver/gpio.h"
#include "driver/uart.h"
#include "GladiatorLink.h"
namespace config {
constexpr auto UART=UART_NUM_1;
constexpr int TX=16,RX=17,BAUD=460800;
// Seeed XIAO: GPIO3 low enables the RF switch, GPIO14 high selects U.FL.
constexpr auto RF_ENABLE=GPIO_NUM_3,RF_SELECT=GPIO_NUM_14;
constexpr auto &AP_SSID=robotlink::FIELD_SSID;
constexpr auto &AP_PASSWORD=robotlink::FIELD_PASSWORD;
constexpr char HOSTNAME[]="gladiator-gateway";
constexpr char VERSION[]="gateway-2.4.9";
constexpr uint32_t STALE_MS=2000;
}
