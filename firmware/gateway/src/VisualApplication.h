#pragma once
#include "../../shared/OperatorStatus.h"
// Included within the existing gateway namespace after its HTTP utilities.
char visualCameraIp[16]="192.168.8.2",visualMessage[49]={};
visual::Mode visualOverride=visual::Mode::READY;
uint32_t visualOverrideMs=0,visualOverrideLease=0;
void loadVisualSettings() {
  nvs_handle_t nvs;
  if(nvs_open("visual",NVS_READONLY,&nvs)==ESP_OK){char stored[16]={};size_t n=sizeof(stored);if(nvs_get_str(nvs,"cameraIp",stored,&n)==ESP_OK)strlcpy(visualCameraIp,stored,sizeof(visualCameraIp));nvs_close(nvs);}
}
bool visualGood(cJSON *robot,const char *key,uint32_t maxAge) {
  auto topic=cJSON_GetObjectItemCaseSensitive(robot,key),meta=cJSON_GetObjectItemCaseSensitive(topic,"meta");
  auto age=cJSON_GetObjectItemCaseSensitive(meta,"ageMs");
  return cJSON_IsNumber(age)&&age->valuedouble>=0&&age->valuedouble<maxAge&&!strcmp(textValue(meta,"health"),"OK");
}
opstatus::Health visualSensorHealth(cJSON *robot,const char *key,uint32_t limit){
 auto topic=cJSON_GetObjectItemCaseSensitive(robot,key),meta=cJSON_GetObjectItemCaseSensitive(topic,"meta");
 const char *health=textValue(meta,"health");
 if(!strcmp(health,"OFFLINE")||!strcmp(health,"ERROR"))return opstatus::Health::OFFLINE;
 if(!strcmp(health,"INITIALIZING"))return opstatus::Health::WAIT;
 if(!strcmp(health,"STALE"))return opstatus::Health::STALE;
 if(!strcmp(health,"OK")||!strcmp(health,"DEGRADED")){
  auto age=cJSON_GetObjectItemCaseSensitive(meta,"ageMs");
  if(!cJSON_IsNumber(age)||age->valuedouble<0||age->valuedouble>=limit)return opstatus::Health::STALE;
  return !strcmp(health,"OK")?opstatus::Health::OK:opstatus::Health::DEGRADED;
 }
 return opstatus::Health::UNKNOWN;
}
void addOperatorDiagnostics(cJSON *doc,cJSON *robot,const State &snapshot,bool fresh){
 static uint32_t peer=0,restartCount=0,lastRestart=0;const uint32_t now=nowMs();
 if(snapshot.peerBootId&&peer&&snapshot.peerBootId!=peer){restartCount=uint32_t(now-lastRestart)<20000?restartCount+1:1;lastRestart=now;}
 if(snapshot.peerBootId)peer=snapshot.peerBootId;
 auto d=cJSON_AddObjectToObject(doc,"diagnostics");
 cJSON_AddStringToObject(d,"brain",fresh?"OK":"OFFLINE");
 cJSON_AddBoolToObject(d,"restarting",restartCount>=2&&uint32_t(now-lastRestart)<20000);number(d,"restarts",restartCount);
 cJSON_AddStringToObject(d,"link",snapshot.active==Transport::UART?"UART":snapshot.active==Transport::WIFI?"WIFI":"NONE");
 for(auto mapping:{std::pair<const char *,uint32_t>{"power",500},{"imu",1000},{"tofFront",1000},{"presence",2000}}){
  auto h=fresh?visualSensorHealth(robot,mapping.first,mapping.second):opstatus::Health::UNKNOWN;
  if(fresh&&!strcmp(mapping.first,"imu")&&(h==opstatus::Health::OK||h==opstatus::Health::DEGRADED)){
   auto i=cJSON_GetObjectItemCaseSensitive(robot,"imu"),v=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(i,"gameDerived"),"valid");
   if(cJSON_IsFalse(v))h=opstatus::Health::NO_POSE;
  }
  cJSON_AddStringToObject(d,!strcmp(mapping.first,"tofFront")?"lidar":!strcmp(mapping.first,"presence")?"radar":mapping.first,opstatus::name(h));
 }
 auto mast=cJSON_GetObjectItemCaseSensitive(robot,"mast");
 const char *mode=!fresh?"UNKNOWN":cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mast,"testActive"))?"TEST":cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mast,"enabled"))?"ON":cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mast,"calibrated"))?"OFF":"MANUAL";
 cJSON_AddStringToObject(d,"mast",mode);
 const char *reason=textValue(mast,"testReason");
 const char *stop=strstr(reason,"lease")?"TIMEOUT":strstr(reason,"timing")?"TIMING":strstr(reason,"safety")?"POWER":strstr(reason,"PWM")?"PWM":"NONE";
 cJSON_AddStringToObject(d,"stop",stop);
}
esp_err_t visualHandler(httpd_req_t *req) {
  lock();cJSON *robot=copyRobot();State snapshot=state;
  char camera[16],message[49];strlcpy(camera,visualCameraIp,sizeof(camera));strlcpy(message,visualMessage,sizeof(message));
  auto requested=visualOverride;uint32_t requestedMs=visualOverrideMs,lease=visualOverrideLease;unlock();
  visual::Input input;input.controllerFresh=snapshot.receivedUs&&esp_timer_get_time()-snapshot.receivedUs<500000;
  auto controller=cJSON_GetObjectItemCaseSensitive(robot,"controller");
  strlcpy(input.state,textValue(controller,"state"),sizeof(input.state));strlcpy(input.battery,textValue(controller,"battery"),sizeof(input.battery));
  auto range=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(robot,"tofFront"),"derived");
  auto mm=cJSON_GetObjectItemCaseSensitive(range,"nearestMm");
  input.rangeValid=input.controllerFresh&&visualGood(robot,"tofFront",500)&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(range,"valid"))&&cJSON_IsNumber(mm)&&mm->valuedouble>=20&&mm->valuedouble<4000;
  if(input.rangeValid)input.nearestMm=mm->valuedouble;
  auto presence=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(robot,"presence"),"derived");
  input.presenceValid=input.controllerFresh&&visualGood(robot,"presence",1000)&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(presence,"presenceValid"));
  input.present=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(presence,"present"));
  auto mast=cJSON_GetObjectItemCaseSensitive(robot,"mast"),mastAge=cJSON_GetObjectItemCaseSensitive(mast,"transportAgeMs");
  auto sourceAge=cJSON_GetObjectItemCaseSensitive(mast,"sourceAgeMs");
  input.mastScanning=input.controllerFresh&&cJSON_IsNumber(mastAge)&&mastAge->valuedouble<250&&
    cJSON_IsNumber(sourceAge)&&sourceAge->valuedouble>=0&&sourceAge->valuedouble<250&&
    cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mast,"enabled"))&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(mast,"scanning"));
  auto automatic=visual::automatic(input);
  const uint32_t visualNow=nowMs();
  bool overriding=lease&&visual::fresh(visualNow,requestedMs,lease)&&automatic!=visual::Mode::OFFLINE&&automatic!=visual::Mode::ALERT;
  auto effective=visual::effective(automatic,requested,visualNow,requestedMs,lease);
  cJSON *doc=cJSON_CreateObject();number(doc,"schema",1);number(doc,"gatewayMs",nowMs());
  cJSON_AddBoolToObject(doc,"fresh",input.controllerFresh);cJSON_AddStringToObject(doc,"mode",visual::name(effective));
  cJSON_AddStringToObject(doc,"state",input.controllerFresh?input.state:"UNKNOWN");cJSON_AddStringToObject(doc,"battery",input.controllerFresh?input.battery:"UNKNOWN");
  cJSON_AddStringToObject(doc,"alertReason",effective!=visual::Mode::ALERT?"NONE":!strcmp(input.battery,"LOCKOUT")||!strcmp(input.battery,"CRITICAL")?"BATTERY":input.rangeValid&&input.nearestMm<500?"PROXIMITY":"EXPRESSION");
  cJSON_AddBoolToObject(doc,"override",overriding);cJSON_AddStringToObject(doc,"message",overriding?message:"");
  if(input.rangeValid)number(doc,"nearestMm",input.nearestMm);else cJSON_AddNullToObject(doc,"nearestMm");
  if(input.presenceValid)cJSON_AddBoolToObject(doc,"present",input.present);else cJSON_AddNullToObject(doc,"present");
  auto power=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(robot,"power"),"derived"),volts=cJSON_GetObjectItemCaseSensitive(power,"busVolts");
  if(input.controllerFresh&&visualGood(robot,"power",500)&&cJSON_IsNumber(volts)&&std::isfinite(volts->valuedouble))number(doc,"volts",volts->valuedouble);else cJSON_AddNullToObject(doc,"volts");
  cJSON_AddStringToObject(doc,"cameraIp",camera);
  char url[64]={};if(camera[0])snprintf(url,sizeof(url),"http://%s:81/stream",camera);
  cJSON_AddStringToObject(doc,"streamUrl",url);if(!strcmp(req->uri,"/api/crowd"))addCrowdGrid(doc,input.controllerFresh);else addOperatorDiagnostics(doc,robot,snapshot,input.controllerFresh);cJSON_Delete(robot);return replyJson(req,serialize(doc));
}
esp_err_t visualCommandHandler(httpd_req_t *req) {
  // Temporary field expressions are key-free; camera/NVS configuration is maintenance.
  std::string body;if(!readBody(req,body,256))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid visual command");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());const char *mode=textValue(doc,"mode"),*message=textValue(doc,"message");
  visual::Mode selected;double lease=0;bool automatic=!strcmp(mode,"auto");
  bool valid=cJSON_IsObject(doc)&&(automatic||visual::parseMode(mode,selected))&&strlen(message)<=48;
  if(!automatic)valid=valid&&integerValue(doc,"leaseMs",1000,60000,lease);
  for(size_t i=0;i<strlen(message);++i)if(uint8_t(message[i])<32||uint8_t(message[i])>126)valid=false;
  if(!valid){cJSON_Delete(doc);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Mode auto/ready/scanning/active/alert/greeting/offline; lease 1000-60000 ms; message <=48 ASCII characters");}
  lock();visualOverrideLease=automatic?0:uint32_t(lease);visualOverrideMs=nowMs();
  if(!automatic)visualOverride=selected;
  strlcpy(visualMessage,message,sizeof(visualMessage));unlock();cJSON_Delete(doc);
  return replyJson(req,"{\"accepted\":true,\"scope\":\"display-only\"}");
}
esp_err_t visualConfigHandler(httpd_req_t *req) {
  if(!authorize(req))return ESP_OK;
  std::string body;if(!readBody(req,body,64))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid camera configuration");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());const char *ip=textValue(doc,"cameraIp");in_addr parsed={};
  bool valid=cJSON_IsString(cJSON_GetObjectItemCaseSensitive(doc,"cameraIp"))&&(!ip[0]||inet_pton(AF_INET,ip,&parsed)==1);
  uint32_t host=ntohl(parsed.s_addr);
  // Accept private unicast only. Never place arbitrary user HTML/URLs in FieldPage.
  if(ip[0])valid=valid&&((host>>24)==10||(host>>20)==0xac1||(host>>16)==0xc0a8);
  if(!valid||strlen(ip)>15){cJSON_Delete(doc);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected private IPv4 cameraIp, or empty string to disable");}
  nvs_handle_t nvs;esp_err_t err=nvs_open("visual",NVS_READWRITE,&nvs);
  if(err==ESP_OK){err=nvs_set_str(nvs,"cameraIp",ip);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
  if(err==ESP_OK){lock();strlcpy(visualCameraIp,ip,sizeof(visualCameraIp));unlock();}
  cJSON_Delete(doc);if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save camera address");
  return replyJson(req,"{\"saved\":true}");
}
