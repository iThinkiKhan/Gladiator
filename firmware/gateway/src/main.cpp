#include <atomic>
#include <string>
#include <string_view>
#include <memory>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include <new>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "../../shared/JsonEscape.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "RobotProtocol.h"
#include "GladiatorLink.h"
#include "RangeGrid.h"
#include "WifiLink.h"
#include "DetailCodec.h"
#include "DetailJson.h"
#include "FieldPage.h"
#include "InterfaceVocabulary.h"
#include "GatewayConfig.h"
#include "GatewayPage.h"
#include "FieldHelpPage.h"
#include "VisualState.h"
#include "MastPolicy.h"

namespace {
constexpr char TAG[]="gateway";
SemaphoreHandle_t stateMutex;
httpd_handle_t server=nullptr;
esp_netif_t *station=nullptr;
uint32_t bootId=0;
RTC_NOINIT_ATTR uint32_t failedAllocationCookie,failedAllocationBytes;
uint32_t previousFailedAllocation=0;
void allocationFailed(size_t bytes,uint32_t,const char *){failedAllocationBytes=bytes;failedAllocationCookie=0x474C4144;}

char adminKey[33]={},wifiSsid[33]={},wifiPassword[65]={};
struct State {
  robotlink::Transport active=robotlink::Transport::NONE;
  bool uartAlive=false,uartStable=false,wifiLinkAlive=false;
  uint32_t drops=0,detailErrors=0,wifiLeaseMs=0,resultClient=0,resultSequence=0,resultEpoch=0;
  uint8_t resultCode=0;
  bool wifiConnected=false,configured=false;
  char ip[16]={};
  uint32_t rxFrames=0,txFrames=0,frameErrors=0,jsonErrors=0,sequenceGaps=0,peerBootId=0,lastSequence=0,pongs=0;
  bool sequenceKnown=false;
  uint64_t receivedUs=0,uartAliveUs=0;
  int disconnectReason=0;
} state;
std::atomic<bool> reconfigure{false},pingRequested{false},otaUpdating{false};
robotlink::Decoder decoder;
uint8_t txRaw[robotlink::MAX_RAW],txFrame[robotlink::MAX_FRAME];
uint32_t nowMs(){return uint32_t(esp_timer_get_time()/1000);}
void lock(){xSemaphoreTake(stateMutex,portMAX_DELAY);}
void unlock(){xSemaphoreGive(stateMutex);}
const char *textValue(const cJSON *object,const char *key){auto p=cJSON_GetObjectItemCaseSensitive(object,key);return cJSON_IsString(p)?p->valuestring:"";}
std::string serialize(cJSON *doc){char *s=cJSON_PrintUnformatted(doc);std::string out=s?s:"{}";cJSON_free(s);cJSON_Delete(doc);return out;}

void loadSettings(){
  nvs_handle_t nvs;ESP_ERROR_CHECK(nvs_open("gladiator",NVS_READWRITE,&nvs));
  size_t n=sizeof(adminKey);
  if(nvs_get_str(nvs,"admin",adminKey,&n)!=ESP_OK||strlen(adminKey)!=32){
    uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));
    for(size_t i=0;i<sizeof(bytes);++i)snprintf(adminKey+2*i,3,"%02x",bytes[i]);
    ESP_ERROR_CHECK(nvs_set_str(nvs,"admin",adminKey));ESP_ERROR_CHECK(nvs_commit(nvs));
  }
  n=sizeof(wifiSsid);if(nvs_get_str(nvs,"ssid",wifiSsid,&n)!=ESP_OK)wifiSsid[0]=0;
  n=sizeof(wifiPassword);if(nvs_get_str(nvs,"password",wifiPassword,&n)!=ESP_OK)wifiPassword[0]=0;
  nvs_close(nvs);state.configured=wifiSsid[0]!=0;
  // Physical USB maintenance only; never returned by a network endpoint.
  printf("{\"gatewayProvisioning\":true,\"ssid\":\"%s\",\"password\":\"%s\",\"adminKey\":\"%s\"}\n",config::AP_SSID,config::AP_PASSWORD,adminKey);
}
void onWifi(void *,esp_event_base_t base,int32_t id,void *event){
  if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
    auto info=static_cast<wifi_event_sta_disconnected_t *>(event);
    lock();state.wifiConnected=false;state.ip[0]=0;state.disconnectReason=info->reason;unlock();
  }else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
    auto info=static_cast<ip_event_got_ip_t *>(event);char ip[16];
    snprintf(ip,sizeof(ip),IPSTR,IP2STR(&info->ip_info.ip));
    lock();state.wifiConnected=true;state.disconnectReason=0;strlcpy(state.ip,ip,sizeof(state.ip));unlock();
    ESP_LOGI(TAG,"Homelab Wi-Fi connected; gateway at http://%s",ip);
  }
}
void applyStation(){
  wifi_config_t cfg={};
  lock();memcpy(cfg.sta.ssid,wifiSsid,strlen(wifiSsid));
  strlcpy(reinterpret_cast<char *>(cfg.sta.password),wifiPassword,sizeof(cfg.sta.password));unlock();
  if(cfg.sta.ssid[0]){
    cfg.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable=true;cfg.sta.pmf_cfg.required=false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&cfg));esp_wifi_connect();
  }
}
void startWifi(){
  gpio_set_direction(config::RF_ENABLE,GPIO_MODE_OUTPUT);gpio_set_level(config::RF_ENABLE,0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_direction(config::RF_SELECT,GPIO_MODE_OUTPUT);gpio_set_level(config::RF_SELECT,1);
  ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
  station=esp_netif_create_default_wifi_sta();auto ap=esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(station,config::HOSTNAME);
  esp_netif_dhcps_stop(ap);
  esp_netif_ip_info_t ip={};ip.ip.addr=ESP_IP4TOADDR(192,168,8,1);ip.gw=ip.ip;ip.netmask.addr=ESP_IP4TOADDR(255,255,255,0);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(ap,&ip));ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
  wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,onWifi,nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,onWifi,nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  wifi_config_t cfg={};strlcpy(reinterpret_cast<char *>(cfg.ap.ssid),config::AP_SSID,sizeof(cfg.ap.ssid));
  strlcpy(reinterpret_cast<char *>(cfg.ap.password),config::AP_PASSWORD,sizeof(cfg.ap.password));
  cfg.ap.ssid_len=strlen(config::AP_SSID);cfg.ap.channel=6;cfg.ap.max_connection=4;cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&cfg));ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));applyStation();
}
#include "LinkApplication.h"
cJSON *gatewayState(const State *snapshot=nullptr){
  State s;char ssid[33];lock();s=snapshot?*snapshot:state;strlcpy(ssid,wifiSsid,sizeof(ssid));unlock();
  const uint64_t now=esp_timer_get_time();const bool fresh=s.receivedUs&&now-s.receivedUs<config::STALE_MS*1000ULL;
  cJSON *doc=cJSON_CreateObject();
  cJSON_AddStringToObject(doc,"firmware",config::VERSION);cJSON_AddStringToObject(doc,"role","network-gateway");
  cJSON_AddStringToObject(doc,"board","Seeed XIAO ESP32C6");cJSON_AddStringToObject(doc,"antenna","external");
  cJSON_AddNumberToObject(doc,"uptimeMs",nowMs());cJSON_AddNumberToObject(doc,"bootId",bootId);
  number(doc,"resetReasonCode",esp_reset_reason());number(doc,"previousFailedAllocationBytes",previousFailedAllocation);number(doc,"httpStackFreeBytes",uxTaskGetStackHighWaterMark(nullptr));
  cJSON_AddNumberToObject(doc,"freeHeap",esp_get_free_heap_size());
  cJSON_AddNumberToObject(doc,"largestFreeBlock",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));cJSON_AddNumberToObject(doc,"minimumFreeHeap",esp_get_minimum_free_heap_size());
  cJSON_AddStringToObject(doc,"apSsid",config::AP_SSID);cJSON_AddStringToObject(doc,"apIp","192.168.8.1");
  cJSON_AddBoolToObject(doc,"wifiConfigured",s.configured);cJSON_AddBoolToObject(doc,"wifiConnected",s.wifiConnected);
  cJSON_AddStringToObject(doc,"wifiSsid",ssid);cJSON_AddStringToObject(doc,"ip",s.ip);
  cJSON_AddNumberToObject(doc,"disconnectReason",s.disconnectReason);
  wifi_ap_record_t ap={};if(s.wifiConnected&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK)cJSON_AddNumberToObject(doc,"rssi",ap.rssi);
  cJSON_AddBoolToObject(doc,"robotConnected",fresh);cJSON_AddBoolToObject(doc,"telemetryStale",!fresh);
  if(s.receivedUs)cJSON_AddNumberToObject(doc,"telemetryAgeMs",double((now-s.receivedUs)/1000));else cJSON_AddNullToObject(doc,"telemetryAgeMs");
  cJSON_AddNumberToObject(doc,"rxFrames",s.rxFrames);cJSON_AddNumberToObject(doc,"txFrames",s.txFrames);
  cJSON_AddNumberToObject(doc,"frameErrors",s.frameErrors);cJSON_AddNumberToObject(doc,"jsonErrors",s.jsonErrors);
  cJSON_AddNumberToObject(doc,"sequenceGaps",s.sequenceGaps);cJSON_AddNumberToObject(doc,"pongs",s.pongs);
  cJSON_AddNumberToObject(doc,"peerBootId",s.peerBootId);cJSON_AddNumberToObject(doc,"txPin",config::TX);
  cJSON_AddNumberToObject(doc,"rxPin",config::RX);cJSON_AddNumberToObject(doc,"baud",config::BAUD);
  cJSON_AddNumberToObject(doc,"rtosTickHz",configTICK_RATE_HZ);
  cJSON_AddNumberToObject(doc,"linkTaskPriority",LINK_TASK_PRIORITY);
  cJSON_AddNumberToObject(doc,"linkLoopDelayTicks",LINK_LOOP_DELAY_TICKS);
  cJSON_AddNumberToObject(doc,"linkLoopMinimumDelayMs",1000.0*LINK_LOOP_DELAY_TICKS/configTICK_RATE_HZ);
  cJSON_AddNumberToObject(doc,"protocolVersion",robotlink::VERSION);cJSON_AddBoolToObject(doc,"otaUpdating",otaUpdating.load());
  cJSON_AddStringToObject(doc,"interface","Gladiator Link");cJSON_AddStringToObject(doc,"transport",robotlink::name(s.active));
  cJSON_AddBoolToObject(doc,"uartAlive",s.uartAlive);cJSON_AddBoolToObject(doc,"uartStable",s.uartStable);cJSON_AddBoolToObject(doc,"wifiLinkAlive",s.wifiLinkAlive);
  cJSON_AddBoolToObject(doc,"fieldRecovery",!s.uartAlive);number(doc,"wifiLeaseMs",s.wifiLeaseMs);number(doc,"drops",s.drops);number(doc,"detailErrors",s.detailErrors);
  cJSON_AddStringToObject(doc,"otaSlot",esp_ota_get_running_partition()->label);
  cJSON_AddStringToObject(doc,"capabilities","typed-telemetry,detail-subscriptions,control-relay,wifi-failover,wifi-lease");
  cJSON_AddStringToObject(doc,"futureCapabilities","BLE interface,802.15.4 experiments; not enabled in this firmware");return doc;
}
esp_err_t replyJson(httpd_req_t *req,const std::string &body){
  httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,body.data(),body.size());
}
bool authorize(httpd_req_t *req){
  char supplied[40]={};
  if(httpd_req_get_hdr_value_str(req,"X-Gladiator-Key",supplied,sizeof(supplied))!=ESP_OK||strlen(supplied)!=32){
    httpd_resp_send_err(req,HTTPD_401_UNAUTHORIZED,"Gateway maintenance key required");return false;}
  uint8_t diff=0;for(unsigned i=0;i<32;++i)diff|=supplied[i]^adminKey[i];
  if(diff){httpd_resp_send_err(req,HTTPD_401_UNAUTHORIZED,"Invalid maintenance key");return false;}return true;
}
esp_err_t rootHandler(httpd_req_t *req){httpd_resp_set_type(req,"text/html");return httpd_resp_send(req,FIELD_PAGE,HTTPD_RESP_USE_STRLEN);}
esp_err_t devHandler(httpd_req_t *req){httpd_resp_set_type(req,"text/html");return httpd_resp_send(req,GATEWAY_PAGE,HTTPD_RESP_USE_STRLEN);}
esp_err_t statusHandler(httpd_req_t *req){return replyJson(req,serialize(gatewayState()));}
esp_err_t robotHandler(httpd_req_t *req){
  // HTTP handlers run on one server task; its bounded wire copy is never shared.
  static uint8_t nativeSnapshot[robotlink::MAX_PAYLOAD];size_t nativeSize=0;
  State snapshot;lock();snapshot=state;cJSON *robot=copyRobot();nativeSize=nativeWireSize;memcpy(nativeSnapshot,nativeWire,nativeSize);auto system=systemJson,logs=logCache;uint32_t detailMs=detailReceived,systemMs=systemReceived,logMs=logReceived;unlock();
  // Keep small field replies prompt, but allow TCP to coalesce this bulk stream.
  // NODELAY on every tiny HTTP chunk/header can exhaust Wi-Fi TX packet buffers.
  struct BulkTcp {int fd;explicit BulkTcp(int socket):fd(socket){int zero=0;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&zero,sizeof(zero));}
    ~BulkTcp(){int one=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));}} bulk(httpd_req_to_sockfd(req));
  // Hold shared system/log snapshots; native diagnostics retain bounded wire
  // bytes and stream through a 512-byte buffer. Neither update nor HTTP allocates
  // a full native JSON string. Views avoid implicit copies of retained strings.
  // Full native telemetry plus logs can exceed the largest free heap block even
  // while total heap is healthy. Stream the validated snapshot by component;
  // never parse/duplicate the native tree or grow a complete print buffer.
  const auto gateway=serialize(gatewayState(&snapshot)),telemetry=serialize(robot);
  httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
  auto send=[&](const char *data,size_t size){return httpd_resp_send_chunk(req,data,size)==ESP_OK;};
  auto text=[&](std::string_view value){return send(value.data(),value.size());};
  auto native=[&](){
    if(!nativeSize)return text("null");
    char buffer[512];size_t used=0;
    auto append=[&](const char *data,size_t size){
      while(size){const size_t n=std::min(size,sizeof(buffer)-used);memcpy(buffer+used,data,n);used+=n;data+=n;size-=n;
        if(used==sizeof(buffer)){if(!send(buffer,used))return false;used=0;}}
      return true;
    };
    DetailReader reader{nativeSnapshot,nativeSize};
    return DetailJsonStream(reader,append).decodeObject()&&(!used||send(buffer,used));
  };
  const uint32_t current=nowMs();
  if(!text("{\"gateway\":")||!text(gateway)||!text(",\"robot\":")||!text(telemetry)||
     !text(",\"native\":")||!native()||!text(",\"system\":")||!text(system?std::string_view(*system):std::string_view("null"))||
     !text(",\"nativeAgeMs\":")||!text(detailMs?std::to_string(uint32_t(current-detailMs)):"null")||
     !text(",\"systemAgeMs\":")||!text(systemMs?std::to_string(uint32_t(current-systemMs)):"null")||
     !text(",\"logs\":")||!jsonstream::writeString(logs?std::string_view(*logs):std::string_view{},send)||
     !text(",\"logAgeMs\":")||!text(logMs?std::to_string(uint32_t(current-logMs)):"null")||
     !text(",\"schemaVersion\":2}"))return ESP_FAIL;
  return httpd_resp_send_chunk(req,nullptr,0);
}
esp_err_t pingHandler(httpd_req_t *req){
  if(!authorize(req))return ESP_OK;
  pingRequested=true;return replyJson(req,"{\"queued\":true,\"request\":\"ping_and_state\"}");
}
bool readBody(httpd_req_t *req,std::string &out,size_t maximum){
  if(req->content_len<=0||size_t(req->content_len)>maximum)return false;
  out.resize(req->content_len);size_t got=0;
  while(got<out.size()){int n=httpd_req_recv(req,&out[got],out.size()-got);if(n<=0)return false;got+=n;}return true;
}
esp_err_t fieldHandler(httpd_req_t *req){
  lock();cJSON *robot=copyRobot();State s=state;unlock();auto c=cJSON_DetachItemFromObjectCaseSensitive(robot,"controller");
  cJSON *doc=cJSON_CreateObject();cJSON_AddBoolToObject(doc,"requiresKey",false);
  cJSON_AddBoolToObject(doc,"connected",s.receivedUs&&esp_timer_get_time()-s.receivedUs<250000);
  cJSON_AddStringToObject(doc,"interface",s.active==Transport::UART?"C6 field / UART to S3":s.active==Transport::WIFI?"C6 field / Wi-Fi to S3":"C6 field / S3 unavailable");
  cJSON_AddItemToObject(doc,"controller",c?c:cJSON_CreateNull());
  cJSON_AddStringToObject(doc,"transport",s.active==Transport::UART?"UART":s.active==Transport::WIFI?"WIFI":"NONE");
  if(s.receivedUs)number(doc,"telemetryAgeMs",(esp_timer_get_time()-s.receivedUs)/1000);else cJSON_AddNullToObject(doc,"telemetryAgeMs");
  cJSON_AddItemToObject(doc,"sensors",cJSON_Duplicate(robot,true));
  auto p=cJSON_GetObjectItemCaseSensitive(robot,"power"),m=cJSON_GetObjectItemCaseSensitive(p,"meta"),d=cJSON_GetObjectItemCaseSensitive(p,"derived");
  auto age=cJSON_GetObjectItemCaseSensitive(m,"ageMs");
  cJSON_AddBoolToObject(doc,"powerValid",cJSON_IsNumber(age)&&age->valuedouble<500&&strcmp(textValue(m,"health"),"OK")==0);
  for(auto mapping:{std::pair<const char *,const char *>{"volts","busVolts"},{"amps","currentAmps"}}){auto v=cJSON_GetObjectItemCaseSensitive(d,mapping.second);cJSON_AddItemToObject(doc,mapping.first,v?cJSON_Duplicate(v,true):cJSON_CreateNull());}
  auto result=cJSON_AddObjectToObject(doc,"lastResult");number(result,"client",s.resultClient);number(result,"sequence",s.resultSequence);number(result,"epoch",s.resultEpoch);number(result,"result",s.resultCode);
  cJSON_Delete(robot);return replyJson(req,serialize(doc));
}
bool integerValue(cJSON *d,const char *key,double low,double high,double &out){
  auto v=cJSON_GetObjectItemCaseSensitive(d,key);if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||std::floor(v->valuedouble)!=v->valuedouble||v->valuedouble<low||v->valuedouble>high)return false;out=v->valuedouble;return true;
}
#include "CrowdApplication.h"
#include "VisualApplication.h"
esp_err_t queueRequest(httpd_req_t *req,Type type,const Writer &w){
  Request request={};request.type=type;request.queuedMs=nowMs();request.size=w.size;
  if(w.size>sizeof(request.data))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Request too large");
  memcpy(request.data,w.data,w.size);
  if(xQueueSend(requests,&request,0)!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return replyJson(req,"{\"error\":\"Link queue full\"}");}
  httpd_resp_set_status(req,"202 Accepted");return replyJson(req,"{\"queued\":true,\"authority\":\"S3\"}");
}
#include "MastApplication.h"
#include "CameraApplication.h"
#include "ControlApplication.h"
#include "HardwareTestApplication.h"
#include "BoothApplication.h"
#include "MastSetupApplication.h"
esp_err_t subscriptionHandler(httpd_req_t *req){
  if(!authorize(req))return ESP_OK;
  std::string body;if(!readBody(req,body,128))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid subscription");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());double interval=0,lease=0;
  bool valid=integerValue(doc,"intervalMs",100,10000,interval)&&integerValue(doc,"leaseMs",0,60000,lease);cJSON_Delete(doc);
  if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Interval 100-10000 ms, lease 0-60000 ms");
  Writer w;w.u32(interval);w.u32(lease);return queueRequest(req,Type::SUBSCRIBE,w);
}
esp_err_t linkHandler(httpd_req_t *req){
  if(!authorize(req))return ESP_OK;
  std::string body;if(!readBody(req,body,128))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid lease");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());double lease=0;bool valid=integerValue(doc,"leaseMs",0,60000,lease);cJSON_Delete(doc);
  if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Lease 0-60000 ms");
  Writer w;w.u32(lease);return queueRequest(req,Type::WIFI_LEASE,w);
}
esp_err_t configHandler(httpd_req_t *req){
  if(!authorize(req))return ESP_OK;
  std::string body;
  if(!readBody(req,body,512))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid settings body");
  cJSON *doc=cJSON_ParseWithLength(body.data(),body.size());
  const char *ssid=textValue(doc,"ssid"),*password=textValue(doc,"password");
  if(!cJSON_IsObject(doc)||!strlen(ssid)||strlen(ssid)>32||strlen(password)<8||strlen(password)>63){
    cJSON_Delete(doc);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"SSID must be 1-32 bytes and Wi-Fi password 8-63 bytes");}
  nvs_handle_t nvs;esp_err_t err=nvs_open("gladiator",NVS_READWRITE,&nvs);
  if(err==ESP_OK){err=nvs_set_str(nvs,"ssid",ssid);if(err==ESP_OK)err=nvs_set_str(nvs,"password",password);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
  if(err!=ESP_OK){cJSON_Delete(doc);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save settings");}
  lock();strlcpy(wifiSsid,ssid,sizeof(wifiSsid));strlcpy(wifiPassword,password,sizeof(wifiPassword));state.configured=true;unlock();
  cJSON_Delete(doc);reconfigure=true;
  return replyJson(req,"{\"saved\":true,\"message\":\"Connecting to Wi-Fi; gateway setup access point remains available.\"}");
}
TaskHandle_t rebootWorker=nullptr;
void rebootTask(void *){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);vTaskDelay(pdMS_TO_TICKS(750));esp_restart();}
esp_err_t otaHandler(httpd_req_t *req){
  if(!authorize(req))return ESP_OK;
  if(!rebootWorker)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Reboot worker unavailable; update refused");
  const esp_partition_t *partition=esp_ota_get_next_update_partition(nullptr);
  if(!partition||req->content_len<512||size_t(req->content_len)>partition->size)
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected a C6 gateway firmware.bin within OTA slot size");
  bool expected=false;if(!otaUpdating.compare_exchange_strong(expected,true))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update already in progress");
  uint8_t buffer[2048];esp_ota_handle_t handle=0;bool begun=false;esp_err_t error=ESP_OK;
  size_t remaining=req->content_len,headerRead=0;
  // Validate image/project identity before beginning an update.
  while(headerRead<512){int n=httpd_req_recv(req,reinterpret_cast<char *>(buffer)+headerRead,512-headerRead);if(n<=0){error=ESP_FAIL;break;}headerRead+=n;}
  esp_app_desc_t description={};
  if(error==ESP_OK){
    memcpy(&description,buffer+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t),sizeof(description));
    if(buffer[0]!=ESP_IMAGE_HEADER_MAGIC||description.magic_word!=ESP_APP_DESC_MAGIC_WORD||
       strncmp(description.project_name,"gladiator_gateway",sizeof(description.project_name))!=0)error=ESP_ERR_INVALID_ARG;
  }
  if(error==ESP_OK){error=esp_ota_begin(partition,req->content_len,&handle);begun=error==ESP_OK;}
  if(error==ESP_OK){error=esp_ota_write(handle,buffer,headerRead);remaining-=headerRead;}
  while(error==ESP_OK&&remaining){
    int n=httpd_req_recv(req,reinterpret_cast<char *>(buffer),std::min(remaining,sizeof(buffer)));
    if(n<=0){error=ESP_FAIL;break;}error=esp_ota_write(handle,buffer,n);remaining-=n;
  }
  if(error==ESP_OK){error=esp_ota_end(handle);begun=false;}
  if(error==ESP_OK)error=esp_ota_set_boot_partition(partition);
  if(error!=ESP_OK){if(begun)esp_ota_abort(handle);otaUpdating=false;return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update rejected or incomplete; running firmware retained");}
  replyJson(req,"{\"updated\":true,\"message\":\"C6 gateway rebooting. The S3 controller continues running.\"}");
  xTaskNotifyGive(rebootWorker);return ESP_OK;
}
// Small HTTP headers and JSON should not wait for the peer's delayed ACK.
// A failed socket option preserves access to the existing maintenance server.
esp_err_t openHttpSession(httpd_handle_t,int socket){
  const int enabled=1;
  if(setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&enabled,sizeof(enabled))!=0)
    ESP_LOGW(TAG,"TCP_NODELAY unavailable: %d",errno);
  return ESP_OK;
}
esp_err_t helpHandler(httpd_req_t *req){httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_send(req,FIELD_HELP_PAGE,HTTPD_RESP_USE_STRLEN);}
void startHttp(){
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=12288;cfg.max_uri_handlers=40;
  cfg.lru_purge_enable=true;cfg.recv_wait_timeout=5;cfg.send_wait_timeout=5;cfg.open_fn=openHttpSession;
  ESP_ERROR_CHECK(httpd_start(&server,&cfg));
  struct Route {const char *uri;httpd_method_t method;esp_err_t(*handler)(httpd_req_t *);};
  const Route routes[]={{"/",HTTP_GET,rootHandler},{"/api/status",HTTP_GET,statusHandler},
    {"/hardware-test",HTTP_GET,hardwarePageHandler},{"/hardware-test/",HTTP_GET,hardwarePageHandler},{"/hardware-test/app.js",HTTP_GET,hardwareScriptHandler},{"/hardware-test/styles.css",HTTP_GET,hardwareStyleHandler},
    {"/booth",HTTP_GET,boothPageHandler},{"/booth/",HTTP_GET,boothPageHandler},{"/booth/app.js",HTTP_GET,boothScriptHandler},{"/booth/mast-settings.js",HTTP_GET,mastSettingsScriptHandler},{"/booth/mast-workbench.js",HTTP_GET,mastWorkbenchScriptHandler},
    {"/help",HTTP_GET,helpHandler},{"/help/",HTTP_GET,helpHandler},{"/api/camera/status",HTTP_GET,cameraProxyHandler},{"/dev",HTTP_GET,devHandler},{"/api/field",HTTP_GET,fieldHandler},{"/api/control",HTTP_POST,controlHandler},{"/api/mast/config",HTTP_POST,mastConfigHandler},{"/api/camera",HTTP_GET,cameraProxyHandler},{"/api/camera",HTTP_POST,cameraProxyHandler},
    {"/api/subscribe",HTTP_POST,subscriptionHandler},{"/api/link",HTTP_POST,linkHandler},
    {"/api/crowd",HTTP_GET,visualHandler},{"/api/visual",HTTP_GET,visualHandler},{"/api/visual",HTTP_POST,visualCommandHandler},
    {"/api/visual/config",HTTP_POST,visualConfigHandler},
    {"/api/mast/setup",HTTP_GET,mastSetupHandler},{"/api/mast/setup",HTTP_POST,mastSetupHandler},
    {"/api/mast",HTTP_GET,mastHandler},{"/api/mast",HTTP_POST,mastCommandHandler},
    {"/api/robot",HTTP_GET,robotHandler},{"/api/config",HTTP_POST,configHandler},
    {"/api/ping",HTTP_POST,pingHandler},{"/api/ota",HTTP_POST,otaHandler}};
  for(const auto &r:routes){httpd_uri_t uri={};uri.uri=r.uri;uri.method=r.method;uri.handler=r.handler;ESP_ERROR_CHECK(httpd_register_uri_handler(server,&uri));}
}
}
extern "C" void app_main(){
  previousFailedAllocation=failedAllocationCookie==0x474C4144?failedAllocationBytes:0;failedAllocationCookie=0;
  ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(allocationFailed));
  usb_serial_jtag_driver_config_t usb=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
  usb_serial_jtag_vfs_use_driver();
  fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
  stateMutex=xSemaphoreCreateMutex();configASSERT(stateMutex);
  esp_err_t err=nvs_flash_init();
  if(err==ESP_ERR_NVS_NO_FREE_PAGES||err==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());err=nvs_flash_init();}
  ESP_ERROR_CHECK(err);bootId=esp_random();loadSettings();loadVisualSettings();
  requests=xQueueCreate(6,sizeof(Request));configASSERT(requests);
  startWifi();
  configASSERT(xTaskCreate(uartTask,"gladiator-link",12288,nullptr,LINK_TASK_PRIORITY,nullptr)==pdPASS);
  configASSERT(xTaskCreate(rebootTask,"reboot",8192,nullptr,2,&rebootWorker)==pdPASS);
  startHttp();
  fcntl(STDIN_FILENO,F_SETFL,O_NONBLOCK);
  ESP_LOGI(TAG,"Gateway ready: C6 TX16/RX17, external antenna; setup http://192.168.8.1");
  uint32_t lastRetry=0,lastLog=0;bool verified=false;
  char usbCommand[32]={};size_t usbUsed=0;
  for(;;){
    char input;
    for(unsigned budget=0;budget<64&&read(STDIN_FILENO,&input,1)==1;++budget){
      if(input=='\n'||input=='\r'){
        usbCommand[usbUsed]=0;
        if(strcmp(usbCommand,"credentials")==0)
          printf("{\"gatewayProvisioning\":true,\"ssid\":\"%s\",\"password\":\"%s\",\"adminKey\":\"%s\"}\n",config::AP_SSID,config::AP_PASSWORD,adminKey);
        else if(strcmp(usbCommand,"status")==0)printf("%s\n",serialize(gatewayState()).c_str());
        else if(strcmp(usbCommand,"saved")==0){cJSON *record=nullptr;
          if(mastSetupRead(&record)==ESP_OK)printf("%s\n",serialize(record).c_str());
          else printf("{\"error\":\"Saved setup storage could not be read\"}\n");
        }else if(strcmp(usbCommand,"field")==0){lock();auto robot=copyRobot();unlock();printf("%s\n",serialize(robot).c_str());}
        usbUsed=0;
      }else if(usbUsed<sizeof(usbCommand)-1)usbCommand[usbUsed++]=input;
      else usbUsed=0;
    }
    State s;lock();s=state;unlock();
    if(reconfigure.exchange(false)){esp_wifi_disconnect();applyStation();lastRetry=nowMs();}
    else if(s.configured&&!s.wifiConnected&&nowMs()-lastRetry>10000){
      lastRetry=nowMs();wifi_sta_list_t clients={};
      // Opal searches take this shared radio off the setup AP channel. Keep a
      // direct staff session responsive; retry when those clients leave.
      // Startup and an explicit saved-settings reconnect still try immediately.
      if(esp_wifi_ap_get_sta_list(&clients)==ESP_OK&&clients.num==0)esp_wifi_connect();
    }
    if(!verified&&nowMs()>15000&&esp_timer_get_time()-s.uartAliveUs<2000000&&server){
      esp_ota_img_states_t status;
      if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&status)==ESP_OK&&status==ESP_OTA_IMG_PENDING_VERIFY)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
      verified=true;
    }
    if(nowMs()-lastLog>5000){lastLog=nowMs();ESP_LOGI(TAG,"state frames=%lu errors=%lu wifi=%s ip=%s heap=%lu",
      (unsigned long)s.rxFrames,(unsigned long)s.frameErrors,s.wifiConnected?"up":"down",s.ip,(unsigned long)esp_get_free_heap_size());}
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
