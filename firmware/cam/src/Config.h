#pragma once
#if __has_include("LocalConfig.h")
#include "LocalConfig.h"
#endif
#ifndef VISUAL_WIFI_SSID
#define VISUAL_WIFI_SSID ""
#define VISUAL_WIFI_PASSWORD ""
#endif
#ifndef VISUAL_GATEWAY_URL
#define VISUAL_GATEWAY_URL "http://192.168.8.1"
#endif
#ifndef VISUAL_TFT_WIDTH
#define VISUAL_TFT_WIDTH 170
#endif
#ifndef VISUAL_TFT_HEIGHT
#define VISUAL_TFT_HEIGHT 320
#endif
#ifndef VISUAL_TFT_ROTATION
#define VISUAL_TFT_ROTATION 1
#endif
#ifndef VISUAL_OTA_KEY
#define VISUAL_OTA_KEY ""
#endif
#ifndef VISUAL_FIRMWARE_VERSION
#define VISUAL_FIRMWARE_VERSION "masthead-1.2.2-halloween"
#endif
namespace config {
constexpr bool HALLOWEEN_DISPLAY=true;
constexpr int SCLK=14,MOSI=13,DC=2,CS=15,RESET=-1;
// Physical panel is upside down. Apply offset even to existing LocalConfig.
constexpr uint8_t DISPLAY_ROTATION=(VISUAL_TFT_ROTATION+2)%4;
constexpr uint32_t STALE_MS=1500,FRAME_MS=67,POLL_MS=200;
constexpr char VERSION[]=VISUAL_FIRMWARE_VERSION;
constexpr uint32_t OTA_CONFIRM_MS=120000,RECOVERY_MS=30000;
}
