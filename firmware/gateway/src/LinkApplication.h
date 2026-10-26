#pragma once
// Included within the gateway namespace after HTTP/state utilities.
using namespace robotlink;
// This target runs at 100 Hz. pdMS_TO_TICKS(2) truncates to zero at that
// tick rate, so use an explicit tick to guarantee that the idle task runs.
constexpr TickType_t LINK_LOOP_DELAY_TICKS=1;
constexpr UBaseType_t LINK_TASK_PRIORITY=4;
Health uartHealth,wifiHealth;
WifiChannel wifiChannel;
Decoder wifiDecoder;
Transport primary=Transport::NONE;
uint32_t sequences[3]={},topicReceived[6]={},detailReceived=0,systemReceived=0;
cJSON *robotCache=nullptr;
std::shared_ptr<const std::string> systemJson;
uint8_t nativeWire[MAX_PAYLOAD];size_t nativeWireSize=0;
std::string rangeJson;
rangegrid::Sample rangeGrid;
std::shared_ptr<const std::string> logCache;std::string logPending;uint32_t logReceived=0,logId=0;size_t logUsed=0;
uint8_t detailBytes[MAX_PAYLOAD];size_t detailUsed=0,detailTotal=0;uint32_t detailTransfer=0,detailStartedMs=0,nativeStartedMs=0;
struct Request {Type type;uint32_t queuedMs;size_t size;uint8_t data[768];};
QueueHandle_t requests;

bool sendTyped(Type type,const void *data,size_t size,Transport transport){
  if(transport==Transport::NONE)return false;
  size_t n=encode(type,sequences[uint8_t(transport)]++,bootId,static_cast<const char *>(data),size,txRaw,txFrame,sizeof(txFrame));
  bool ok=n&&(transport==Transport::UART?uart_write_bytes(config::UART,txFrame,n)==int(n):wifiChannel.send(txFrame,n,nowMs()));
  lock();if(ok)++state.txFrames;else ++state.drops;unlock();return ok;
}
void sendTyped(Type type,const Writer &w,Transport t){sendTyped(type,w.data,w.size,t);}
std::string detailString(DetailReader &r){size_t n=r.u16();if(n>r.size-r.pos){r.valid=false;return {};}
  std::string s(reinterpret_cast<const char *>(r.data+r.pos),n);r.pos+=n;return s;}
cJSON *readTree(DetailReader &r,unsigned depth=0){
  if(depth>16||!r.valid){r.valid=false;return nullptr;}uint8_t tag=r.u8();
  switch(tag){
    case 0:return cJSON_CreateNull();case 1:return cJSON_CreateFalse();case 2:return cJSON_CreateTrue();
    case 3:return cJSON_CreateNumber(r.number());case 4:{auto s=detailString(r);return cJSON_CreateString(s.c_str());}
    case 5:case 6:{size_t count=r.u16();if(count>r.size-r.pos){r.valid=false;return nullptr;}cJSON *out=tag==5?cJSON_CreateArray():cJSON_CreateObject();
      for(size_t i=0;i<count&&r.valid;++i){std::string key;if(tag==6)key=detailString(r);auto v=readTree(r,depth+1);if(!v){r.valid=false;break;}if(tag==5)cJSON_AddItemToArray(out,v);else cJSON_AddItemToObject(out,key.c_str(),v);}
      if(!r.valid){cJSON_Delete(out);return nullptr;}return out;}
    default:r.valid=false;return nullptr;
  }
}
void number(cJSON *o,const char *key,double value){cJSON_AddNumberToObject(o,key,value);}
cJSON *decodeMeta(Reader &r,uint32_t sourceMs){
  auto m=cJSON_CreateObject();uint8_t health=r.u8(),flags=r.u8();uint32_t sampleMs=r.u32();
  cJSON_AddStringToObject(m,"health",interfacev1::healthName(health));cJSON_AddBoolToObject(m,"online",flags&1);cJSON_AddBoolToObject(m,"hasSample",flags&2);
  number(m,"lastUpdateMs",sampleMs);if(flags&2)number(m,"ageMs",uint32_t(sourceMs-sampleMs));else cJSON_AddNullToObject(m,"ageMs");
  number(m,"updateCount",r.u32());number(m,"errorCount",r.u32());number(m,"rateHz",r.f32());return m;
}
cJSON *decodeTelemetry(const Frame &f,int &topic,const char *&key,rangegrid::Sample &grid){
  Reader r(f);auto out=cJSON_CreateObject();uint32_t sourceMs=r.u32();number(out,"s3Ms",sourceMs);
  if(f.type==Type::CONTROLLER){topic=0;key="controller";number(out,"epoch",r.u32());number(out,"client",r.u32());bool armed=r.u8(),deadman=r.u8();
    cJSON_AddBoolToObject(out,"armed",armed);cJSON_AddBoolToObject(out,"deadman",deadman);cJSON_AddBoolToObject(out,"watchdogStopped",r.u8());
    number(out,"owner",r.u8());int8_t left=int8_t(r.u8()),right=int8_t(r.u8());number(out,"leftPercent",left);number(out,"rightPercent",right);
    number(out,"power",r.u8());cJSON_AddBoolToObject(out,"sensorTaskStarted",r.u8());cJSON_AddBoolToObject(out,"batteryLockout",r.u8());cJSON_AddBoolToObject(out,"radiosShed",r.u8());cJSON_AddBoolToObject(out,"benchPower",r.u8());number(out,"loopMaxUs",r.u32());number(out,"bootCount",r.u32());
    // Legacy S3 payloads have no presentation state. Never infer it on C6.
    if(r.size-r.pos!=0&&r.size-r.pos!=2)r.valid=false;
    const auto state=r.pos<r.size?interfacev1::State(r.u8()):interfacev1::State::UNKNOWN;
    const auto battery=r.pos<r.size?interfacev1::Battery(r.u8()):interfacev1::Battery::UNKNOWN;
    cJSON_AddStringToObject(out,"state",interfacev1::name(state));
    cJSON_AddStringToObject(out,"battery",interfacev1::name(battery));
  }else{
    cJSON_AddItemToObject(out,"meta",decodeMeta(r,sourceMs));auto d=cJSON_AddObjectToObject(out,"derived");
    switch(f.type){
      case Type::POWER:topic=1;key="power";for(auto field:{"busVolts","currentAmps","watts","minimumVolts","peakAbsAmps"})number(d,field,r.f32());break;
      case Type::ORIENTATION:{topic=2;key="imu";cJSON_AddBoolToObject(d,"valid",r.u8());auto game=cJSON_AddObjectToObject(out,"gameDerived");cJSON_AddBoolToObject(game,"valid",r.u8());
        for(auto target:{d,game})for(auto axis:{"rollDeg","pitchDeg","yawDeg"})number(target,axis,r.f32());
        cJSON_AddBoolToObject(d,"headingValid",r.pos<r.size?r.u8():false);
        break;}
      case Type::RANGE:{topic=3;key="tofFront";cJSON_AddBoolToObject(d,"valid",r.u8());number(d,"nearestMm",r.u32());number(d,"usableZones",r.u8());
        if(r.pos<r.size){
          if(!rangegrid::read(r,grid)){r.valid=false;break;}
          auto m=cJSON_GetObjectItemCaseSensitive(out,"meta");auto age=cJSON_GetObjectItemCaseSensitive(m,"ageMs");
          grid.seen=true;grid.receivedMs=nowMs();
          grid.usable=cJSON_IsNumber(age)&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(m,"hasSample"))&&!strcmp(textValue(m,"health"),"OK");
          grid.sourceAgeMs=cJSON_IsNumber(age)?uint32_t(age->valuedouble):UINT32_MAX;
          auto count=cJSON_GetObjectItemCaseSensitive(m,"updateCount");grid.sampleCount=uint32_t(count->valuedouble);
        }break;}
      case Type::PRESENCE:topic=4;key="presence";cJSON_AddBoolToObject(d,"presenceValid",r.u8());cJSON_AddBoolToObject(d,"present",r.u8());cJSON_AddBoolToObject(d,"targetValid",r.u8());number(d,"rangeMeters",r.f32());number(d,"speedMetersPerSecond",r.f32());break;
      default:r.valid=false;break;
    }
  }
  if(!r.done()){cJSON_Delete(out);return nullptr;}return out;
}
void receiveTyped(const Frame &f,Transport transport){
  auto &h=transport==Transport::UART?uartHealth:wifiHealth;
  if(f.type==Type::HEARTBEAT||f.type==Type::HELLO){
    Peer p;if(!p.decode(f)||p.role!=1||!h.accept(f))return;
    h.beat(p,bootId,nowMs());primary=p.active;
    lock();if(state.peerBootId&&state.peerBootId!=f.bootId){cJSON_Delete(robotCache);robotCache=nullptr;nativeWireSize=0;rangeJson.clear();rangeGrid={};systemJson.reset();logCache.reset();logPending.clear();logUsed=0;detailReceived=systemReceived=logReceived=0;memset(topicReceived,0,sizeof(topicReceived));detailUsed=detailTotal=0;nativeStartedMs=0;state.receivedUs=0;}
    state.peerBootId=f.bootId;state.active=primary;state.wifiLeaseMs=p.wifiLeaseMs;++state.rxFrames;unlock();return;
  }
  if(f.bootId!=h.boot||!h.alive(nowMs())||!h.accept(f))return;
  if(f.type==Type::PONG&&f.length==0){lock();++state.pongs;unlock();return;}
  if(f.type==Type::CONTROL_RESULT){Reader r(f);uint32_t client=r.u32(),sequence=r.u32(),epoch=r.u32();uint8_t result=r.u8();if(r.done()){lock();state.resultClient=client;state.resultSequence=sequence;state.resultEpoch=epoch;state.resultCode=result;unlock();}return;}
  if(f.type==Type::SYSTEM&&f.length<=900){DetailReader reader{reinterpret_cast<const uint8_t *>(f.payload),f.length};cJSON *doc=readTree(reader);
    if(reader.valid&&reader.pos==reader.size&&cJSON_IsObject(doc)){auto json=serialize(doc);lock();systemJson=std::make_shared<const std::string>(std::move(json));systemReceived=nowMs();unlock();}else cJSON_Delete(doc);return;}
  if(f.type==Type::LOG){Reader r(f);uint32_t id=r.u32(),total=r.u32(),offset=r.u32();size_t chunk=f.length>=12?f.length-12:0;
    if(!r.valid||!total||total>12000||chunk>384||offset>total||chunk>total-offset)return;
    if(offset==0){logPending.resize(total);logUsed=0;logId=id;}
    if(id!=logId||total!=logPending.size()||offset!=logUsed)return;
    memcpy(&logPending[offset],f.payload+12,chunk);logUsed+=chunk;
    if(logUsed==total){lock();logCache=std::make_shared<const std::string>(std::move(logPending));logReceived=nowMs();unlock();logUsed=0;}return;
  }
  if(f.type==Type::MAST_STATE&&f.length<=1536){
    auto value=cJSON_ParseWithLength(f.payload,f.length);
    auto schema=cJSON_GetObjectItemCaseSensitive(value,"schema");
    if(!cJSON_IsObject(value)||!cJSON_IsNumber(schema)||schema->valuedouble!=1){cJSON_Delete(value);return;}
    lock();if(!robotCache)robotCache=cJSON_CreateObject();
    if(!cJSON_GetObjectItemCaseSensitive(value,"profile")){
      auto previous=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(robotCache,"mast"),"profile");
      if(previous)cJSON_AddItemToObject(value,"profile",cJSON_Duplicate(previous,true));
    }
    cJSON_DeleteItemFromObjectCaseSensitive(robotCache,"mast");cJSON_AddItemToObject(robotCache,"mast",value);
    topicReceived[5]=nowMs();++state.rxFrames;unlock();return;
  }
  if(f.type==Type::DETAIL){Reader r(f);uint32_t id=r.u32(),total=r.u32(),offset=r.u32();size_t chunk=f.length>=12?f.length-12:0;
    if(!r.valid||!total||total>MAX_PAYLOAD||chunk>384||offset>total||chunk>total-offset){lock();++state.detailErrors;unlock();return;}
    if(offset==0){detailTransfer=id;detailTotal=total;detailUsed=0;detailStartedMs=nowMs();}
    if(id!=detailTransfer||total!=detailTotal||offset!=detailUsed){lock();++state.detailErrors;unlock();return;}
    memcpy(detailBytes+offset,f.payload+12,chunk);detailUsed+=chunk;
    if(detailUsed==detailTotal){DetailReader reader{detailBytes,detailTotal};std::string range;
      auto discard=[](const char *,size_t){return true;};
      if(DetailJsonStream(reader,discard,&range,"tofFront").decodeObject()){lock();memcpy(nativeWire,detailBytes,detailTotal);nativeWireSize=detailTotal;rangeJson=std::move(range);detailReceived=nowMs();nativeStartedMs=detailStartedMs;unlock();}
      else{lock();++state.detailErrors;unlock();}detailTotal=detailUsed=0;}
    return;
  }
  int topic=-1;const char *key=nullptr;rangegrid::Sample grid;cJSON *value=decodeTelemetry(f,topic,key,grid);
  if(!value){lock();++state.jsonErrors;unlock();return;}
  lock();if(!robotCache)robotCache=cJSON_CreateObject();cJSON_DeleteItemFromObjectCaseSensitive(robotCache,key);cJSON_AddItemToObject(robotCache,key,value);
  if(grid.seen)rangeGrid=grid;
  topicReceived[topic]=nowMs();++state.rxFrames;if(topic==0)state.receivedUs=esp_timer_get_time();unlock();
}
// Caller holds stateMutex. Ages grow even if that individual topic stops.
cJSON *copyRobot(){
  cJSON *out=robotCache?cJSON_Duplicate(robotCache,true):cJSON_CreateObject();
  const char *keys[]={"controller","power","imu","tofFront","presence","mast"};
  for(unsigned i=0;i<6;++i){auto item=cJSON_GetObjectItemCaseSensitive(out,keys[i]);if(!item)continue;
    number(item,"transportAgeMs",uint32_t(nowMs()-topicReceived[i]));
    auto age=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(item,"meta"),"ageMs");
    if(cJSON_IsNumber(age))cJSON_SetNumberValue(age,age->valuedouble+uint32_t(nowMs()-topicReceived[i]));
    auto sourceAge=cJSON_GetObjectItemCaseSensitive(item,"sourceAgeMs");
    if(cJSON_IsNumber(sourceAge))cJSON_SetNumberValue(sourceAge,sourceAge->valuedouble+uint32_t(nowMs()-topicReceived[i]));
  }return out;
}
void uartTask(void *){
  uart_config_t cfg={};cfg.baud_rate=config::BAUD;cfg.data_bits=UART_DATA_8_BITS;cfg.parity=UART_PARITY_DISABLE;
  cfg.stop_bits=UART_STOP_BITS_1;cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(config::UART,4096,2048,0,nullptr,0));ESP_ERROR_CHECK(uart_param_config(config::UART,&cfg));
  ESP_ERROR_CHECK(uart_set_pin(config::UART,config::TX,config::RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
  int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);configASSERT(listener>=0);int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(WIFI_PORT);inet_pton(AF_INET,FIELD_IP,&address.sin_addr);
  configASSERT(bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof(address))==0);configASSERT(listen(listener,2)==0);fcntl(listener,F_SETFL,O_NONBLOCK);
  uint8_t bytes[1024];uint32_t lastHeartbeat=0,lastWifiRx=0;
  for(;;){uint32_t now=nowMs();int n=uart_read_bytes(config::UART,bytes,sizeof(bytes),0);Frame frame;
    for(int i=0;i<n;++i)if(decoder.feed(bytes[i],now,frame))receiveTyped(frame,Transport::UART);
    int client=accept(listener,nullptr,nullptr);if(client>=0){if(wifiChannel.fd>=0)::close(client);else{wifiDecoder.reset();wifiHealth=Health{};wifiChannel.attach(client,true,now);lastWifiRx=now;}}
    wifiChannel.poll(now,[&](const uint8_t *p,size_t count){for(size_t i=0;i<count;++i)if(wifiDecoder.feed(p[i],now,frame)){receiveTyped(frame,Transport::WIFI);lastWifiRx=now;}});
    // Refresh after RX so unsigned age checks cannot precede fresh heartbeats.
    now=nowMs();
    if(wifiChannel.fd>=0&&now-lastWifiRx>LOSS_MS){wifiChannel.close();wifiHealth=Health{};}
    if(now-lastHeartbeat>=HEARTBEAT_MS){lastHeartbeat=now;
      for(Transport t:{Transport::UART,Transport::WIFI}){auto &h=t==Transport::UART?uartHealth:wifiHealth;Peer p;p.role=2;p.active=primary;p.probe=++h.probe;p.echoBoot=h.boot;p.echoProbe=h.peerProbe;
        if(t==Transport::UART||wifiChannel.ready)sendTyped(Type::HEARTBEAT,p.encode(),t);}
    }
    Transport usable=primary;
    if((usable==Transport::UART&&!uartHealth.alive(now))||(usable==Transport::WIFI&&!wifiHealth.alive(now)))usable=Transport::NONE;
    Request request;for(unsigned budget=0;budget<4&&xQueueReceive(requests,&request,0)==pdTRUE;++budget){
      if(now-request.queuedMs<=150)sendTyped(request.type,request.data,request.size,usable);else{lock();++state.drops;unlock();}}
    if(pingRequested.exchange(false)){Writer w;sendTyped(Type::PING,w,usable);sendTyped(Type::GET_STATE,w,usable);}
    lock();state.uartAliveUs=esp_timer_get_time();state.uartAlive=uartHealth.alive(now);state.uartStable=uartHealth.stable(now);state.wifiLinkAlive=wifiHealth.alive(now);
    state.frameErrors=decoder.errors+decoder.oversize+decoder.timeouts+wifiDecoder.errors+wifiChannel.errors;unlock();vTaskDelay(LINK_LOOP_DELAY_TICKS);
  }
}
