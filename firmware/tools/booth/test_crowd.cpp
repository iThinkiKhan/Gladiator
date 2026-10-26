#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdint>
#include "cJSON.h"
#include "RobotProtocol.h"
#include "RangeGrid.h"
using robotlink::Type;
struct Writer {uint8_t data[8];size_t size=0;void u32(uint32_t n){for(int i=0;i<4;++i)data[size++]=uint8_t(n>>(i*8));}};
struct Request{Type type;uint32_t queuedMs;size_t size;uint8_t data[768];};
int requests=0,spaces=16,sent=0;Request last;
#define pdTRUE 1
int uxQueueSpacesAvailable(int){return spaces;}
int xQueueSend(int,const Request *r,int){++sent;last=*r;return 1;}
uint32_t clockMs=1000,detailReceived=900,nativeStartedMs=850;std::string rangeJson;rangegrid::Sample rangeGrid;
uint32_t nowMs(){return clockMs;}void lock(){}void unlock(){}
void number(cJSON *o,const char *key,double v){cJSON_AddNumberToObject(o,key,v);}
const char *textValue(const cJSON *o,const char *k){auto v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";}
#include "CrowdApplication.h"
std::string fixture(int count=64,int age=50,const char *health="OK"){
 auto d=cJSON_CreateObject(),r=d,m=cJSON_AddObjectToObject(r,"meta"),raw=cJSON_AddObjectToObject(r,"raw");
 cJSON_AddBoolToObject(m,"hasSample",true);cJSON_AddStringToObject(m,"health",health);number(m,"ageMs",age);auto a=cJSON_AddArrayToObject(raw,"distanceMm");
 for(int i=0;i<count;++i)cJSON_AddItemToArray(a,cJSON_CreateNumber(i==0?10:i==1?4000:i==2?99999:800+i));
 char *s=cJSON_PrintUnformatted(d);std::string out=s;cJSON_free(s);cJSON_Delete(d);return out;
}
cJSON *get(bool fresh=true){auto d=cJSON_CreateObject();addCrowdGrid(d,fresh);auto g=cJSON_DetachItemFromObjectCaseSensitive(d,"grid");cJSON_Delete(d);return g;}
int main(){
 rangeJson=fixture();auto g=get();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(g,"fresh")));auto a=cJSON_GetObjectItemCaseSensitive(g,"mm");assert(cJSON_GetArraySize(a)==64);assert(cJSON_IsNull(cJSON_GetArrayItem(a,0)));assert(cJSON_IsNull(cJSON_GetArrayItem(a,1)));assert(cJSON_IsNull(cJSON_GetArrayItem(a,2)));assert(cJSON_GetArrayItem(a,3)->valuedouble==803);assert(cJSON_GetObjectItemCaseSensitive(g,"ageMs")->valuedouble==200);assert(sent==1&&last.type==Type::SUBSCRIBE&&robotlink::read32(last.data)==1000&&robotlink::read32(last.data+4)==6000);cJSON_Delete(g);
 g=get();assert(sent==1);cJSON_Delete(g); // Requests from multiple viewers share rate limit.
 clockMs=4000;g=get();assert(sent==2);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(g,"mm")));cJSON_Delete(g);
 clockMs=7000;spaces=4;g=get();assert(sent==2);cJSON_Delete(g);spaces=16;
 clockMs=1000;for(auto v:{fixture(63),fixture(65),fixture(64,2400),fixture(64,50,"OFFLINE"),std::string("{}")}){rangeJson=v;g=get();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(g,"mm")));cJSON_Delete(g);}
 rangeJson=fixture();g=get(false);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));cJSON_Delete(g);
 clockMs=100;nativeStartedMs=0xfffffff0u;detailReceived=32;rangeJson=fixture(64,20);g=get();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(cJSON_GetObjectItemCaseSensitive(g,"ageMs")->valuedouble==136);cJSON_Delete(g);
 rangeGrid.seen=true;rangeGrid.usable=true;rangeGrid.receivedMs=clockMs;rangeGrid.sourceAgeMs=25;rangeGrid.sampleCount=123;
 for(unsigned i=0;i<64;++i)rangeGrid.mm[i]=i==0?4000:1000+i;
 const int beforeCompact=sent;g=get();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(sent==beforeCompact);
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(g,"source")->valuestring,"range-telemetry"));assert(cJSON_GetObjectItemCaseSensitive(g,"sample")->valuedouble==123);
 assert(cJSON_IsNull(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(g,"mm"),0)));assert(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(g,"mm"),63)->valuedouble==1063);cJSON_Delete(g);
 clockMs+=2500;g=get();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(g,"mm")));assert(sent==beforeCompact);cJSON_Delete(g);
 rangeGrid.receivedMs=clockMs;rangeGrid.sourceAgeMs=0;g=get(false);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));cJSON_Delete(g);
 rangeGrid.usable=false;g=get();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(g,"fresh")));cJSON_Delete(g);
 rangeGrid.usable=true;rangeGrid.receivedMs=0xfffffff0u;rangeGrid.sourceAgeMs=20;clockMs=100;g=get();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(g,"fresh")));assert(cJSON_GetObjectItemCaseSensitive(g,"ageMs")->valuedouble==136);cJSON_Delete(g);
 std::puts("PASS compact and legacy production crowd grid: validation, invalid zones, ages, stale controller, rollover, finite rate-limited subscription and reserved control queue capacity");
}
