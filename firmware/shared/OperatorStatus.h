#pragma once
#include <stdint.h>
#include <string.h>
namespace opstatus {
enum class Health:uint8_t { UNKNOWN,OK,WAIT,STALE,OFFLINE,DEGRADED,NO_POSE };
inline const char *name(Health h){switch(h){case Health::OK:return "OK";case Health::WAIT:return "WAIT";case Health::STALE:return "STALE";case Health::OFFLINE:return "OFFLINE";case Health::DEGRADED:return "DEGRADED";case Health::NO_POSE:return "NO_POSE";default:return "UNKNOWN";}}
inline Health parse(const char *s){if(s)for(unsigned i=0;i<=unsigned(Health::NO_POSE);++i)if(!strcmp(s,name(Health(i))))return Health(i);return Health::UNKNOWN;}
struct Facts {
 bool available=false,restarting=false;Health brain=Health::UNKNOWN,power=Health::UNKNOWN,imu=Health::UNKNOWN,lidar=Health::UNKNOWN,radar=Health::UNKNOWN;
 char mast[12]="UNKNOWN",stop[12]="NONE",link[8]="NONE";uint32_t restarts=0;
};
struct Local {bool wifi=false,gateway=false,controller=false,camera=false;bool starting=false;int cameraError=0;const char *battery="UNKNOWN";};
struct Message {uint8_t level;const char *code,*title,*action;};
inline Message remote(const Facts &f,const Local &l){
 if(!l.wifi)return {2,"WIFI","Wi-Fi disconnected","Check robot power and Wi-Fi/router"};
 if(!l.gateway)return l.starting?Message{0,"CONNECTING","Connecting to gateway","Waiting for the communications board"}:Message{2,"GATEWAY","Gateway not responding","Check C6 power and Wi-Fi connection"};
 if(!l.controller)return f.restarting?Message{2,"RESTARTING","S3 keeps restarting","Check power/sensors; see S3 status LED"}:Message{2,"BRAIN","S3 controller offline","Gateway is up; check S3 power and link"};
 if(!strcmp(l.battery,"CRITICAL"))return {2,"BATTERY_CRITICAL","Battery critically low","Switch off and recharge the pack"};
 if(!strcmp(l.battery,"LOCKOUT"))return {2,"BATTERY_LOW","Battery low: motion off","Recharge the 4-cell battery pack"};
 if(f.available&&(f.power==Health::OFFLINE||f.power==Health::STALE))return {2,"POWER","Power monitor offline","Check the INA power/data connector"};
 if(f.available&&(f.power==Health::WAIT||f.power==Health::UNKNOWN))return {1,"POWER_WAIT","Waiting for power data","Battery voltage is not available yet"};
 if(f.available&&(f.imu==Health::NO_POSE))return {1,"IMU_POSE","IMU orientation missing","IMU recovery is retrying the sensor"};
 if(f.available&&(f.imu==Health::OFFLINE||f.imu==Health::STALE))return {1,"IMU","IMU not responding","Check IMU power and data connections"};
 if(f.available&&f.lidar!=Health::OK)return {1,"LIDAR","LiDAR data unavailable","Check range sensor power and I2C"};
 if(f.available&&f.imu==Health::DEGRADED)return {1,"IMU_CAL","IMU calibrating","Keep the robot still while it settles"};
 if(!strcmp(l.battery,"WARN"))return {1,"BATTERY_WARN","Battery getting low","Recharge before the next demonstration"};
 if(!f.available)return {0,"BASIC","Basic status feed","Update gateway for sensor diagnostics"};
 return {0,"READY","Ready for manual control","Live controller and sensor status"};
}
inline Message cameraMessage(const Local &l){if(!l.camera&&l.cameraError==0x101)return {2,"CAMERA_MEMORY","Camera memory unavailable","Restart CAM; check power and PSRAM"};return l.camera?Message{0,"CAMERA_OK","Camera ready","Video available on the control page"}:Message{2,"CAMERA","Camera not detected","Check camera ribbon/plug, then restart"};}
inline Message select(const Facts &f,const Local &l,uint32_t now){auto r=remote(f,l);auto c=cameraMessage(l);
 if(!l.wifi||!l.gateway||!l.controller)return r;
 if(r.level==2)return r;
 if(!l.camera){if(r.level&&((now/6000)%2))return r;return c;}return r;
}
inline bool dashboard(const Message &m,uint32_t now){return m.level>0||(now%12000)>=8000;}
inline const char *shortName(Health h){switch(h){case Health::OK:return "OK";case Health::WAIT:return "STARTING";case Health::STALE:return "STALE";case Health::OFFLINE:return "OFFLINE";case Health::DEGRADED:return "CALIBRATING";case Health::NO_POSE:return "NO ORIENTATION";default:return "UNKNOWN";}}
}
