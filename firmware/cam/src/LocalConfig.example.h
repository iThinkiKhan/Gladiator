#pragma once
// Copy to LocalConfig.h (ignored). Join the C6 AP or the same trusted LAN.
#define VISUAL_WIFI_SSID "replace-with-gateway-ap-ssid"
#define VISUAL_WIFI_PASSWORD "replace-with-gateway-ap-password"
#define VISUAL_GATEWAY_URL "http://192.168.8.1"
// Optional 32-hex-character maintenance key. Empty generates a persistent key
// on first boot, obtainable over physical USB with the command: credentials
// Save the key before sealing the enclosure. Existing NVS settings win on OTA.
#define VISUAL_OTA_KEY ""
// Common 1.9-inch panel default; confirm the actual module.
#define VISUAL_TFT_WIDTH 170
#define VISUAL_TFT_HEIGHT 320
#define VISUAL_TFT_ROTATION 1
