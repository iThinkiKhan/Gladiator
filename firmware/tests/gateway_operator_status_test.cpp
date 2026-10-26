// Exercise the production endpoint handlers against deterministic HTTP/NVS/time doubles.
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include <string>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cassert>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include "cJSON.h"
#include "../shared/VisualState.h"
#include "../shared/MastPolicy.h"
using robotlink::Type;
using esp_err_t=int;using nvs_handle_t=int;
constexpr int ESP_OK=0,NVS_READONLY=0,NVS_READWRITE=1,HTTPD_400_BAD_REQUEST=400,HTTPD_500_INTERNAL_SERVER_ERROR=500;
struct httpd_req_t {const char *uri="/api/visual";std::string body,response;bool authorized=true;int status=200;};
using namespace robotlink;
struct State {uint64_t receivedUs=0;uint32_t peerBootId=1;Transport active=Transport::UART;} state;
uint32_t clockMs=1000;
cJSON *cache=nullptr;
uint32_t nowMs(){return clockMs;}
uint64_t esp_timer_get_time(){return uint64_t(clockMs)*1000;}
void lock(){}void unlock(){}
size_t strlcpy(char *dst,const char *src,size_t n){size_t size=strlen(src);if(n){size_t copy=size<n?size:n-1;memcpy(dst,src,copy);dst[copy]=0;}return size;}
const char *textValue(const cJSON *o,const char *key){auto p=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(p)?p->valuestring:"";}
void number(cJSON *o,const char *k,double v){cJSON_AddNumberToObject(o,k,v);}
cJSON *copyRobot(){return cJSON_Duplicate(cache,true);}
std::string serialize(cJSON *d){char *p=cJSON_PrintUnformatted(d);std::string s=p;cJSON_free(p);cJSON_Delete(d);return s;}
esp_err_t replyJson(httpd_req_t *r,const std::string &s){r->response=s;return 0;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *s){r->status=status;r->response=s;return 0;}
bool authorize(httpd_req_t *r){if(!r->authorized)r->status=401;return r->authorized;}
bool readBody(httpd_req_t *r,std::string &s,size_t maximum){s=r->body;return !s.empty()&&s.size()<=maximum;}
bool integerValue(cJSON *d,const char *key,double low,double high,double &out){auto v=cJSON_GetObjectItemCaseSensitive(d,key);if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||std::floor(v->valuedouble)!=v->valuedouble||v->valuedouble<low||v->valuedouble>high)return false;out=v->valuedouble;return true;}
int nvs_open(const char *,int,nvs_handle_t *n){*n=1;return 0;}
int nvs_get_str(nvs_handle_t,const char *,char *,size_t *){return -1;}
int nvs_set_str(nvs_handle_t,const char *,const char *){return 0;}
int nvs_commit(nvs_handle_t){return 0;}void nvs_close(nvs_handle_t){}
void addCrowdGrid(cJSON*,bool){}
#include "../gateway/src/VisualApplication.h"

void fixture(){cJSON_Delete(cache);cache=cJSON_Parse(R"({"controller":{"state":"SAFE","battery":"OK"},"power":{"meta":{"health":"OK","ageMs":5},"derived":{"busVolts":15.3}},"imu":{"meta":{"health":"DEGRADED","ageMs":5},"gameDerived":{"valid":false}},"tofFront":{"meta":{"health":"OK","ageMs":5},"derived":{"valid":true,"nearestMm":900}},"presence":{"meta":{"health":"OFFLINE","ageMs":null}},"mast":{"calibrated":false,"enabled":false,"testActive":false,"testReason":"operator lease expired"}})");state.receivedUs=esp_timer_get_time();}
cJSON *get(const char *uri="/api/visual"){httpd_req_t r;r.uri=uri;visualHandler(&r);auto d=cJSON_Parse(r.response.c_str());assert(d);assert(r.response.size()<1536);return d;}
int main(){fixture();auto d=get();auto f=cJSON_GetObjectItemCaseSensitive(d,"diagnostics");assert(!strcmp(textValue(f,"power"),"OK"));assert(!strcmp(textValue(f,"imu"),"NO_POSE"));assert(!strcmp(textValue(f,"radar"),"OFFLINE"));assert(!strcmp(textValue(f,"mast"),"MANUAL"));assert(!strcmp(textValue(f,"stop"),"TIMEOUT"));cJSON_Delete(d);
 auto p=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cache,"power"),"meta");cJSON_ReplaceItemInObject(p,"health",cJSON_CreateString("OFFLINE"));d=get();f=cJSON_GetObjectItemCaseSensitive(d,"diagnostics");assert(!strcmp(textValue(f,"power"),"OFFLINE"));cJSON_Delete(d);
 clockMs+=50;state.peerBootId=2;d=get();cJSON_Delete(d);clockMs+=50;state.peerBootId=3;d=get();f=cJSON_GetObjectItemCaseSensitive(d,"diagnostics");assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(f,"restarting")));cJSON_Delete(d);
 state.receivedUs=esp_timer_get_time()-600000;d=get();f=cJSON_GetObjectItemCaseSensitive(d,"diagnostics");assert(!strcmp(textValue(f,"power"),"UNKNOWN"));assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(d,"volts")));cJSON_Delete(d);
 d=get("/api/crowd");assert(!cJSON_GetObjectItemCaseSensitive(d,"diagnostics"));cJSON_Delete(d);
 puts("Gateway display feed passed: actual handler, partial IMU, missing power/radar, manual mast, stopped-test reason, restart loop, stale data, unchanged crowd schema and bounded payload.");}
