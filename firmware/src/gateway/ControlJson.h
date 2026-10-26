#pragma once
#include <ArduinoJson.h>
#include "../../shared/GladiatorLink.h"
namespace robotlink {
inline bool controlFromJson(const JsonDocument &d,Control &c){
  for(const char *key:{"epoch","client","sequence","observedMs"})if(!d[key].is<uint32_t>())return false;
  for(const char *key:{"operation","deadman","overdrive","power"})if(!d[key].is<uint8_t>())return false;
  if(!d["left"].is<int8_t>()||!d["right"].is<int8_t>())return false;
  c.epoch=d["epoch"];c.client=d["client"];c.sequence=d["sequence"];c.observedMs=d["observedMs"];
  c.operation=d["operation"];c.deadman=d["deadman"];c.overdrive=d["overdrive"];c.power=d["power"];c.left=d["left"];c.right=d["right"];return true;
}
}
