#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace camota {
// Fixed after esp_app_desc_t in the first image segment. This tags both the
// application and its 4 MB/min_spiffs partition contract; it is not a signature.
struct Identity {char application[24];char layout[32];uint32_t protocol;uint32_t reserved;};
constexpr Identity IDENTITY={"Gladiator-CAM-OTA","esp32-4m-min-spiffs-v1",1,0};
static_assert(sizeof(Identity)==64,"OTA identity layout changed");
constexpr size_t IDENTITY_OFFSET=24+8+256;
inline bool validHex(const char *s,size_t length) {
  if(!s||strlen(s)!=length)return false;
  for(size_t i=0;i<length;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;
  return true;
}
inline bool matchesKey(const char *supplied,const char *expected) {
  if(!validHex(supplied,32)||!validHex(expected,32))return false;
  uint8_t difference=0;for(size_t i=0;i<32;++i)difference|=uint8_t(supplied[i]^expected[i]);return difference==0;
}
inline bool validPrefix(const uint8_t *bytes,size_t size) {
  if(size<IDENTITY_OFFSET+sizeof(Identity)||bytes[0]!=0xe9||bytes[1]==0||bytes[1]>16)return false;
  // ESP32 chip ID is 0; require the appended SHA-256 image digest.
  if(bytes[12]!=0||bytes[13]!=0||bytes[23]!=1)return false;
  uint32_t descriptor=0;memcpy(&descriptor,bytes+32,4);
  return descriptor==0xabcd5432 && !memcmp(bytes+IDENTITY_OFFSET,&IDENTITY,sizeof(Identity));
}
inline void digestHex(const uint8_t *digest,char *out) {
  constexpr char hex[]="0123456789abcdef";
  for(size_t i=0;i<32;++i){out[i*2]=hex[digest[i]>>4];out[i*2+1]=hex[digest[i]&15];}out[64]=0;
}
}
