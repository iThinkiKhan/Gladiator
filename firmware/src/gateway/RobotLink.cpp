#include "../../shared/RangeGrid.h"
#include "RobotLink.h"
#include "../BoardConfig.h"
#include "../blackbox/BlackBox.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <new>

namespace gateway {
using namespace robotlink;
namespace {
struct Reply {uint32_t client,sequence,epoch;uint8_t result;};
template<size_t Capacity> void tree(TreeWriter<Capacity> &w,JsonVariantConst v,unsigned depth=0){
  if(depth>16){w.valid=false;return;}
  if(v.isNull())w.u8(0);
  else if(v.is<bool>())w.u8(v.as<bool>()?2:1);
  else if(v.is<JsonObjectConst>()){auto obj=v.as<JsonObjectConst>();w.u8(6);w.u16(obj.size());for(auto item:obj){w.string(item.key().c_str());tree(w,item.value(),depth+1);}}
  else if(v.is<JsonArrayConst>()){auto a=v.as<JsonArrayConst>();w.u8(5);w.u16(a.size());for(auto item:a)tree(w,item,depth+1);}
  else if(v.is<const char *>()){w.u8(4);w.string(v.as<const char *>());}
  else {w.u8(3);w.number(v.as<double>());}
}
void meta(Writer &w,const sensors::SensorMeta &m){
  w.u8(uint8_t(m.health));w.u8((m.online?1:0)|(m.hasSample?2:0));w.u32(m.lastUpdateMs);
  w.u32(m.updateCount);w.u32(m.errorCount);w.f32(m.rateHz);
}
}
bool RobotLink::begin(sensors::SensorManager &manager){
  void *storage=heap_caps_malloc(sizeof(Buffers),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!storage)storage=heap_caps_malloc(sizeof(Buffers),MALLOC_CAP_8BIT);
  if(!storage)return false;
  buffers_=new(storage) Buffers();
  manager_=&manager;mutex_=xSemaphoreCreateMutex();commands_=xQueueCreate(6,sizeof(PendingControl));results_=xQueueCreate(6,sizeof(Reply));
  mastCommands_=xQueueCreate(4,sizeof(PendingMast));mastConfigs_=xQueueCreate(2,sizeof(PendingMastConfig));
  if(!mutex_||!commands_||!results_||!mastCommands_||!mastConfigs_)return false;bootId_=esp_random();
  serial_.setRxBufferSize(2048);serial_.setTxBufferSize(2048);
  serial_.begin(board::GATEWAY_BAUD,SERIAL_8N1,board::GATEWAY_RX,board::GATEWAY_TX);
  return xTaskCreatePinnedToCore(taskEntry,"gladiator-link",16384,this,1,nullptr,0)==pdPASS;
}
void RobotLink::setController(const ControllerState &s){if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){controller_=s;xSemaphoreGive(mutex_);}}
void RobotLink::setSystem(const String &s,const String &log){if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){system_=s;if(wantsDetails_)log_=log;xSemaphoreGive(mutex_);}}
void RobotLink::setMast(const String &s){if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){mast_=s;xSemaphoreGive(mutex_);}}
void RobotLink::result(const Control &c,Result r,uint32_t epoch){Reply reply{c.client,c.sequence,epoch,uint8_t(r)};if(results_)xQueueSend(results_,&reply,0);}
String RobotLink::statusJson(){
  if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){visible_=published_;xSemaphoreGive(mutex_);}
  JsonDocument doc;doc["role"]="authoritative-controller";doc["protocolVersion"]=VERSION;doc["interface"]="Gladiator Link";
  doc["started"]=visible_.started;doc["connected"]=online();doc["transport"]=name(visible_.active);
  doc["uartAlive"]=visible_.uartAlive;doc["uartStable"]=visible_.uartStable;doc["wifiAlive"]=visible_.wifiAlive;
  doc["wifiRequested"]=needsWifi();doc["switches"]=visible_.switches;doc["generation"]=generation();
  doc["txPin"]=board::GATEWAY_TX;doc["rxPin"]=board::GATEWAY_RX;doc["baud"]=board::GATEWAY_BAUD;
  doc["rxFrames"]=visible_.rxFrames;doc["txFrames"]=visible_.txFrames;doc["errors"]=visible_.errors;
  doc["rejected"]=visible_.rejected;doc["drops"]=visible_.drops;doc["peerBootId"]=visible_.peerBootId;
  doc["capabilities"]="typed-telemetry,detail-subscriptions,validated-control,wifi-failover,wifi-lease";
  String result;serializeJson(doc,result);return result;
}
bool RobotLink::send(Type type,const void *payload,size_t length,Transport transport){
  if(transport==Transport::NONE)return false;
  size_t n=encode(type,sequence_[uint8_t(transport)]++,bootId_,static_cast<const char *>(payload),length,buffers_->raw,buffers_->tx,sizeof(buffers_->tx));
  bool ok=false;
  if(n&&transport==Transport::UART&&serial_.availableForWrite()>=int(n))ok=serial_.write(buffers_->tx,n)==n;
  if(n&&transport==Transport::WIFI)ok=buffers_->wifi.send(buffers_->tx,n,millis());
  if(ok)++status_.txFrames;else ++status_.drops;return ok;
}
void RobotLink::receive(const Frame &frame,Transport transport){
  auto &health=transport==Transport::UART?uartHealth_:wifiHealth_;
  if(frame.type==Type::HEARTBEAT||frame.type==Type::HELLO){
    Peer p;if(!p.decode(frame)||p.role!=2||!health.accept(frame)){++status_.rejected;return;}
    if(status_.peerBootId&&status_.peerBootId!=frame.bootId)++generation_;
    status_.peerBootId=frame.bootId;
    {const uint32_t before=health.lastGood;health.beat(p,bootId_,millis());
     if(transport==Transport::UART){++status_.hbTotal;status_.echoLag=uint32_t(health.probe-p.echoProbe);
       if(health.lastGood==before)++status_.hbNoEcho;}}
  }else{
    if(frame.bootId!=health.boot||!health.alive(millis())||!health.accept(frame)){++status_.rejected;return;}
    Reader r(frame);
    switch(frame.type){
      case Type::PING:if(frame.length==0){Writer w;send(Type::PONG,w,transport);}break;
      case Type::GET_STATE:if(frame.length==0)detailRequested_=true;break;
      case Type::SUBSCRIBE:{uint32_t interval=r.u32(),lease=r.u32();if(r.done()&&interval>=100&&interval<=10000&&lease<=60000){detailInterval_=interval;detailLease_=lease;detailStart_=millis();detailRequested_=lease!=0;}else ++status_.rejected;break;}
      case Type::WIFI_LEASE:{uint32_t ms=r.u32();if(r.done()&&ms<=60000)requestWifi(ms);else ++status_.rejected;break;}
      case Type::CONTROL:{PendingControl c;c.receivedMs=millis();c.generation=generation();
        if(transport!=selector_.active||!c.command.decode(frame)||xQueueSend(commands_,&c,0)!=pdTRUE)++status_.rejected;break;}
      case Type::MAST_CONTROL:{PendingMast c;c.receivedMs=millis();c.generation=generation();
        if(transport!=selector_.active||!c.command.decode(frame)||xQueueSend(mastCommands_,&c,0)!=pdTRUE)++status_.rejected;break;}
      case Type::MAST_CONFIG:{PendingMastConfig c;c.receivedMs=millis();c.generation=generation();
        if(transport!=selector_.active||!frame.length||frame.length>768){++status_.rejected;break;}
        memcpy(c.json,frame.payload,frame.length);c.json[frame.length]=0;
        if(xQueueSend(mastConfigs_,&c,0)!=pdTRUE)++status_.rejected;break;}
      default:++status_.rejected;return;
    }
  }
  status_.lastRxMs=millis();++status_.rxFrames;
}
void RobotLink::prepareDetail(){
  sensors::RobotSensors snapshot;if(!manager_->snapshot(snapshot))return;
  JsonDocument doc;if(deserializeJson(doc,sensors::sensorJson(snapshot)))return;
  buffers_->detail.size=0;buffers_->detail.valid=true;tree(buffers_->detail,doc.as<JsonVariantConst>());
  if(!buffers_->detail.valid){buffers_->detail.size=0;++status_.errors;return;}detailOffset_=0;++detailId_;
}
RobotLink::Diagnostics RobotLink::takeDiagnostics(){
  Diagnostics d;
  if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){visible_=published_;xSemaphoreGive(mutex_);}
  d.status=visible_;d.goodAgeMaxMs=goodAgeMax_.exchange(0);d.taskGapMaxMs=taskGapMax_.exchange(0);
  d.online=online();d.wantsWifi=needsWifi();return d;
}
// One row per link change, for the flight recorder: exactly what the health state was.
void RobotLink::logLink(const char *why,Transport before,uint32_t now){
  static uint32_t count=0;++count;
  if(count>200&&count%25)return;  // a flapping link must not flood the log
  blackbox::linef("L,%lu,%s,%s>%s,sw=%lu,alive=%d,stable=%d,confirmed=%d,rxAge=%lu,goodAge=%lu,stableFor=%lu,hb=%lu,noEcho=%lu,echoLag=%lu",
    (unsigned long)now,why,name(before),name(selector_.active),(unsigned long)selector_.switches,
    uartHealth_.alive(now),uartHealth_.stable(now),uartHealth_.confirmed,
    (unsigned long)(now-uartHealth_.lastRx),(unsigned long)(now-uartHealth_.lastGood),(unsigned long)(now-uartHealth_.stableSince),
    (unsigned long)status_.hbTotal,(unsigned long)status_.hbNoEcho,(unsigned long)status_.echoLag);
}
void RobotLink::taskEntry(void *p){static_cast<RobotLink *>(p)->run();}
void RobotLink::run(){
  sensors::RobotSensors snapshot;ControllerState c;uint32_t lastBeat=0,lastState=0,lastPower=0,lastImu=0,lastRange=0,lastPresence=0,lastChunk=0,lastSystem=0,lastMast=0;
  status_.started=true;
  String logTransfer;size_t logOffset=0;uint32_t logId=0,lastLog=0,lastLogChunk=0;Transport previousDetails=Transport::NONE;
  for(;;){
    uint32_t now=millis();Frame frame;
    {static uint32_t previous=0;if(previous){uint32_t gap=now-previous;if(gap>taskGapMax_.load())taskGapMax_=gap;}previous=now;
     if(uartHealth_.confirmed){uint32_t age=now-uartHealth_.lastGood;if(age>goodAgeMax_.load())goodAgeMax_=age;}}
    for(unsigned budget=0;budget<1024&&serial_.available();++budget)if(buffers_->decoder.feed(serial_.read(),now,frame))receive(frame,Transport::UART);
    buffers_->wifi.poll(now,[&](const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(buffers_->wifiDecoder.feed(p[i],now,frame))receive(frame,Transport::WIFI);});
    // receive() stamps frames with millis(); the loop's 'now' was read before that. Using the
    // stale value made now-lastGood wrap to ~4e9 for one pass after every heartbeat, so
    // alive() flickered false (2 transport switches per heartbeat, a generation bump that
    // revokes the field session, a false wantsWifi/online, and a red status-LED blip).
    now=millis();
    uint32_t request=wifiRequest_.exchange(0);if(request){wifiLeaseStart_=now;wifiLeaseMs_=request==UINT32_MAX?0:request;}
    if(wifiLeaseMs_&&uint32_t(now-wifiLeaseStart_)>=wifiLeaseMs_)wifiLeaseMs_=0;
    {const Transport before=selector_.active;
     if(selector_.update(uartHealth_,wifiHealth_,now)){++generation_;buffers_->detail.size=0;logLink("switch",before,now);}}
    {static bool wasAlive=false;const bool alive=uartHealth_.alive(now);
     if(alive!=wasAlive){wasAlive=alive;logLink(alive?"uart-alive":"uart-lost",selector_.active,now);}}
    online_=selector_.active!=Transport::NONE;
    wantsWifi_=(!uartHealth_.alive(now)&&now>=LOSS_MS)||selector_.active==Transport::WIFI||wifiLeaseMs_!=0;
    if(!wantsWifi_&&buffers_->wifi.fd>=0){buffers_->wifi.close();wifiHealth_=Health{};}
    if(buffers_->wifi.fd>=0&&now-lastConnect_>LOSS_MS&&!wifiHealth_.alive(now)){buffers_->wifi.close();wifiHealth_=Health{};}
    if(wantsWifi_&&WiFi.status()==WL_CONNECTED&&buffers_->wifi.fd<0&&now-lastConnect_>=3000){
      lastConnect_=now;int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
      if(fd>=0){fcntl(fd,F_SETFL,O_NONBLOCK);sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(WIFI_PORT);inet_pton(AF_INET,FIELD_IP,&address.sin_addr);
        int result=connect(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address));
        if(result==0||errno==EINPROGRESS){buffers_->wifiDecoder.reset();wifiHealth_=Health{};buffers_->wifi.attach(fd,false,now);}else ::close(fd);}
    }
    if(xSemaphoreTake(mutex_,0)==pdTRUE){c=controller_;xSemaphoreGive(mutex_);}
    if(now-lastBeat>=HEARTBEAT_MS){lastBeat=now;
      for(Transport t:{Transport::UART,Transport::WIFI}){auto &h=t==Transport::UART?uartHealth_:wifiHealth_;Peer p;
        p.role=1;p.active=selector_.active;p.probe=++h.probe;p.echoBoot=h.boot;p.echoProbe=h.peerProbe;p.epoch=c.epoch;
        p.wifiLeaseMs=wifiLeaseMs_?wifiLeaseMs_-uint32_t(now-wifiLeaseStart_):0;
        if(t==Transport::UART||buffers_->wifi.ready)send(Type::HEARTBEAT,p.encode(),t);}
    }
    Reply reply;for(unsigned budget=0;budget<3&&xQueueReceive(results_,&reply,0)==pdTRUE;++budget){Writer w;w.u32(reply.client);w.u32(reply.sequence);w.u32(reply.epoch);w.u8(reply.result);send(Type::CONTROL_RESULT,w,selector_.active);}
    if(now-lastState>=100){lastState=now;Writer w;w.u32(now);w.u32(c.epoch);w.u32(c.client);w.u8(c.armed);w.u8(c.deadman);w.u8(c.watchdogStopped);w.u8(c.owner);w.u8(uint8_t(c.leftPercent));w.u8(uint8_t(c.rightPercent));w.u8(c.power);w.u8(c.sensorTaskStarted);w.u8(c.batteryLockout);w.u8(c.radiosShed);w.u8(c.benchPower);w.u32(c.loopMaxUs);w.u32(c.bootCount);w.u8(uint8_t(c.state));w.u8(uint8_t(c.battery));send(Type::CONTROLLER,w,selector_.active);}
    if(manager_->snapshot(snapshot)){
      if(now-lastPower>=200){lastPower=now;Writer w;w.u32(now);meta(w,snapshot.power.meta);auto &p=snapshot.power.derived;
        for(float v:{p.busVolts,p.currentAmps,p.watts,p.minimumVolts,p.peakAbsAmps})w.f32(v);send(Type::POWER,w,selector_.active);}
      if(now-lastImu>=100){lastImu=now;Writer w;w.u32(now);meta(w,snapshot.imu.meta);auto &i=snapshot.imu;
        w.u8(i.derived.valid);w.u8(i.gameDerived.valid);for(float v:{i.derived.rollDeg,i.derived.pitchDeg,i.derived.yawDeg,i.gameDerived.rollDeg,i.gameDerived.pitchDeg,i.gameDerived.yawDeg})w.f32(v);w.u8(sensors::headingValid(i,now));send(Type::ORIENTATION,w,selector_.active);}
      // The compact range + 64-zone packet is under 200 bytes, once per LiDAR sample
      // (~15 Hz, ~2 KB/s). Full native diagnostics keep their slower cadence.
      if(now-lastRange>=rangegrid::PERIOD_MS){lastRange=now;Writer w;w.u32(millis());meta(w,snapshot.tofFront.meta);auto &d=snapshot.tofFront.derived;w.u8(d.valid);w.u32(d.nearestMm);w.u8(d.usableZones);rangegrid::write(w,snapshot.tofFront.raw.distanceMm);send(Type::RANGE,w,selector_.active);}
      if(now-lastPresence>=500){lastPresence=now;Writer w;w.u32(now);meta(w,snapshot.presence.meta);auto &d=snapshot.presence.derived;w.u8(d.presenceValid);w.u8(d.present);w.u8(d.targetValid);w.f32(d.rangeMeters);w.f32(d.speedMetersPerSecond);send(Type::PRESENCE,w,selector_.active);}
    }
    if(now-lastMast>=100){lastMast=now;String s;
      if(xSemaphoreTake(mutex_,0)==pdTRUE){s=mast_;xSemaphoreGive(mutex_);}
      // A live link task must not make a stalled control loop's cache fresh.
      JsonDocument doc;if(s.length()&&!deserializeJson(doc,s)){
        doc["sourceAgeMs"]=uint32_t(millis()-(doc["s3Ms"]|0U));
        // Saved profile is infrequent; preserve UART capacity for live control.
        static uint32_t lastProfile=0;
        if(uint32_t(now-lastProfile)<2000)doc.remove("profile");else lastProfile=now;
        s="";serializeJson(doc,s);
        if(s.length()<=1536)send(Type::MAST_STATE,s.c_str(),s.length(),selector_.active);
      }
    }
    const bool bulk=wifiLeaseMs_&&wifiHealth_.alive(now);Transport details=bulk?Transport::WIFI:selector_.active;
    if(details!=previousDetails){buffers_->detail.size=0;logTransfer="";previousDetails=details;}
    uint32_t interval=detailInterval_<(bulk?100U:1000U)?(bulk?100U:1000U):detailInterval_;
    if(detailLease_&&now-detailStart_>=detailLease_)detailLease_=0;
    wantsDetails_=detailLease_||detailRequested_||buffers_->detail.size;
    if(!buffers_->detail.size&&(detailRequested_||detailLease_)&&now-lastDetail_>=interval){lastDetail_=now;detailRequested_=false;prepareDetail();}
    if(buffers_->detail.size&&now-lastChunk>=(details==Transport::UART?10U:2U)){
      lastChunk=now;Writer w;w.u32(detailId_);w.u32(buffers_->detail.size);w.u32(detailOffset_);size_t n=std::min(size_t(384),buffers_->detail.size-detailOffset_);
      for(size_t i=0;i<n;++i)w.u8(buffers_->detail.data[detailOffset_+i]);
      if(send(Type::DETAIL,w.data,w.size,details)){detailOffset_+=n;if(detailOffset_==buffers_->detail.size)buffers_->detail.size=0;}
    }
    if((detailLease_||buffers_->detail.size)&&now-lastSystem>=2000){lastSystem=now;String s;
      if(xSemaphoreTake(mutex_,0)==pdTRUE){s=system_;xSemaphoreGive(mutex_);}
      JsonDocument doc;TreeWriter<900> w;
      if(s.length()&&!deserializeJson(doc,s)){tree(w,doc.as<JsonVariantConst>());if(w.valid)send(Type::SYSTEM,w.data,w.size,details);}
    }
    if(wantsDetails_&&logTransfer.isEmpty()&&now-lastLog>=5000){lastLog=now;
      if(xSemaphoreTake(mutex_,0)==pdTRUE){logTransfer=log_;xSemaphoreGive(mutex_);}logOffset=0;++logId;
    }
    if(logTransfer.length()&&now-lastLogChunk>=(details==Transport::UART?10U:2U)){
      lastLogChunk=now;Writer w;w.u32(logId);w.u32(logTransfer.length());w.u32(logOffset);
      size_t n=std::min(size_t(384),size_t(logTransfer.length())-logOffset);for(size_t i=0;i<n;++i)w.u8(logTransfer[logOffset+i]);
      if(send(Type::LOG,w.data,w.size,details)){logOffset+=n;if(logOffset==logTransfer.length())logTransfer="";}
    }
    status_.active=selector_.active;status_.uartAlive=uartHealth_.alive(now);status_.uartStable=uartHealth_.stable(now);status_.wifiAlive=wifiHealth_.alive(now);status_.switches=selector_.switches;
    status_.errors=buffers_->decoder.errors+buffers_->decoder.oversize+buffers_->decoder.timeouts+buffers_->wifiDecoder.errors+buffers_->wifi.errors;
    if(xSemaphoreTake(mutex_,0)==pdTRUE){published_=status_;xSemaphoreGive(mutex_);}vTaskDelay(pdMS_TO_TICKS(2));
  }
}
}
