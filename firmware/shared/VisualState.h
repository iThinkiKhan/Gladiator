#pragma once
#include <stdint.h>
#include <string.h>
namespace visual {
enum class Mode : uint8_t { OFFLINE, READY, SCANNING, ACTIVE, ALERT, GREETING };
inline const char *name(Mode m) {
  switch(m) {case Mode::READY:return "ready";case Mode::SCANNING:return "scanning";
    case Mode::ACTIVE:return "active";case Mode::ALERT:return "alert";
    case Mode::GREETING:return "greeting";default:return "offline";}
}
inline bool parseMode(const char *s,Mode &m) {
  for(unsigned i=0;i<6;++i) {
    if(!strcmp(s,name(Mode(i)))){m=Mode(i);return true;}
  }
  return false;
}
struct Input {
  bool controllerFresh=false,rangeValid=false,presenceValid=false,present=false,mastScanning=false;
  uint32_t nearestMm=0;
  char state[12]="UNKNOWN",battery[12]="UNKNOWN";
};
// Presentation only: use S3-reported states; never synthesize motor commands.
inline Mode automatic(const Input &s) {
  if(!s.controllerFresh)return Mode::OFFLINE;
  if(strcmp(s.state,"SAFE")&&strcmp(s.state,"ARMED")&&strcmp(s.state,"DRIVE"))return Mode::OFFLINE;
  if(!strcmp(s.battery,"LOCKOUT")||!strcmp(s.battery,"CRITICAL")||
     (strcmp(s.state,"SAFE")&&s.rangeValid&&s.nearestMm<500))return Mode::ALERT;
  if(!strcmp(s.state,"DRIVE"))return Mode::ACTIVE;
  if(s.presenceValid&&s.present)return Mode::GREETING;
  if(s.mastScanning)return Mode::SCANNING;
  if(!strcmp(s.state,"ARMED"))return Mode::SCANNING;
  if(!strcmp(s.state,"SAFE"))return Mode::READY;
  return Mode::OFFLINE;
}
inline bool fresh(uint32_t now,uint32_t received,uint32_t limit) {return uint32_t(now-received)<limit;}
inline Mode effective(Mode automaticMode,Mode requested,uint32_t now,uint32_t requestedMs,uint32_t lease) {
  if(automaticMode==Mode::OFFLINE||automaticMode==Mode::ALERT||!lease||!fresh(now,requestedMs,lease))return automaticMode;
  return requested;
}
}
