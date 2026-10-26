#pragma once
#include <ArduinoJson.h>
#include <stdio.h>
#include "OperatorStatus.h"
namespace opstatus {
inline Facts readFacts(JsonVariantConst value){Facts f;if(!value.is<JsonObjectConst>())return f;auto d=value.as<JsonObjectConst>();f.available=true;
 f.brain=parse(d["brain"]|"UNKNOWN");f.power=parse(d["power"]|"UNKNOWN");f.imu=parse(d["imu"]|"UNKNOWN");f.lidar=parse(d["lidar"]|"UNKNOWN");f.radar=parse(d["radar"]|"UNKNOWN");
 f.restarting=d["restarting"]|false;f.restarts=d["restarts"]|0u;
 snprintf(f.mast,sizeof(f.mast),"%s",d["mast"]|"UNKNOWN");snprintf(f.stop,sizeof(f.stop),"%s",d["stop"]|"NONE");snprintf(f.link,sizeof(f.link),"%s",d["link"]|"NONE");return f;
}
}
