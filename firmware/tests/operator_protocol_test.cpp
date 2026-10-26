#include "../shared/OperatorProtocol.h"
#include <cassert>
#include <cstdio>
using namespace opstatus;
int main(){StaticJsonDocument<2048> d;
 assert(!deserializeJson(d,R"({"schema":1,"fresh":true,"mode":"ready","state":"SAFE","battery":"OK","volts":15.3,"diagnostics":{"brain":"OK","power":"OK","imu":"NO_POSE","lidar":"OK","radar":"OFFLINE","mast":"MANUAL","stop":"TIMEOUT","link":"UART","restarting":true,"restarts":3}})"));
 auto f=readFacts(d["diagnostics"]);assert(f.available&&f.power==Health::OK&&f.imu==Health::NO_POSE&&f.radar==Health::OFFLINE&&f.restarting&&f.restarts==3);assert(!strcmp(f.stop,"TIMEOUT"));
 assert(!readFacts(d["absent"]).available);d["diagnostics"]=7;assert(!readFacts(d["diagnostics"]).available);
 d.clear();auto j=d.createNestedObject("diagnostics");j["imu"]="invented";j["mast"]="abcdefghijklmnopqrstuvwxyz";j["stop"]=9;f=readFacts(d["diagnostics"]);assert(f.imu==Health::UNKNOWN&&strlen(f.mast)==11&&!strcmp(f.stop,"NONE"));
 puts("CAM protocol passed: new diagnostic facts, legacy/malformed feed, unknown enums and bounded text using production ArduinoJson6 parser.");}
