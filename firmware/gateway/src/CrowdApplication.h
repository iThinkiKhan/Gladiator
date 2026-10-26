#pragma once
// Public presentation telemetry only. No field key, raw device access or motion.
uint32_t crowdRequestedMs=0;
bool crowdRequested=false;
void addCrowdGrid(cJSON *doc,bool fresh) {
  const uint32_t now=nowMs();rangegrid::Sample compact;
  lock();compact=rangeGrid;unlock();
  if(compact.seen){
    const bool valid=fresh&&compact.fresh(now);
    auto grid=cJSON_AddObjectToObject(doc,"grid");cJSON_AddBoolToObject(grid,"fresh",valid);cJSON_AddStringToObject(grid,"order","sensor-native");
    cJSON_AddStringToObject(grid,"source","range-telemetry");number(grid,"sample",compact.sampleCount);
    number(grid,"ageMs",double(compact.sourceAgeMs)+uint32_t(now-compact.receivedMs));
    if(valid){auto mm=cJSON_AddArrayToObject(grid,"mm");for(auto v:compact.mm){
      if(v>=20&&v<4000)cJSON_AddItemToArray(mm,cJSON_CreateNumber(v));else cJSON_AddItemToArray(mm,cJSON_CreateNull());}}
    else cJSON_AddNullToObject(grid,"mm");
    return; // Crowd viewers no longer request full native diagnostics.
  }
  // Older S3 firmware: retain the finite diagnostic-snapshot fallback.
  std::string raw;uint32_t received=0,started=0;
  lock();raw=rangeJson;received=detailReceived;started=nativeStartedMs;unlock();
  // A finite, modest detail lease; spare request slots remain for operator input.
  if(fresh&&(!crowdRequested||uint32_t(now-crowdRequestedMs)>=3000)&&uxQueueSpacesAvailable(requests)>4){
    Writer w;w.u32(1000);w.u32(6000);Request r={};r.type=Type::SUBSCRIBE;r.queuedMs=now;r.size=w.size;memcpy(r.data,w.data,w.size);
    if(xQueueSend(requests,&r,0)==pdTRUE){crowdRequested=true;crowdRequestedMs=now;}
  }
  auto snapshot=raw.empty()?nullptr:cJSON_ParseWithLength(raw.data(),raw.size());
  auto range=snapshot,meta=cJSON_GetObjectItemCaseSensitive(range,"meta");
  auto distances=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(range,"raw"),"distanceMm");
  auto age=cJSON_GetObjectItemCaseSensitive(meta,"ageMs");
  const uint32_t transport=uint32_t(now-started);
  bool valid=fresh&&received&&started&&transport<2500&&cJSON_IsNumber(age)&&std::isfinite(age->valuedouble)&&age->valuedouble>=0&&age->valuedouble+transport<2500&&
    cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta,"hasSample"))&&!strcmp(textValue(meta,"health"),"OK")&&cJSON_IsArray(distances)&&cJSON_GetArraySize(distances)==64;
  auto grid=cJSON_AddObjectToObject(doc,"grid");cJSON_AddBoolToObject(grid,"fresh",valid);cJSON_AddStringToObject(grid,"order","sensor-native");
  if(received&&cJSON_IsNumber(age)&&age->valuedouble>=0)number(grid,"ageMs",age->valuedouble+transport);else cJSON_AddNullToObject(grid,"ageMs");
  if(valid){auto mm=cJSON_AddArrayToObject(grid,"mm");for(int i=0;i<64;++i){auto v=cJSON_GetArrayItem(distances,i);
      if(cJSON_IsNumber(v)&&std::isfinite(v->valuedouble)&&std::floor(v->valuedouble)==v->valuedouble&&v->valuedouble>=20&&v->valuedouble<4000)cJSON_AddItemToArray(mm,cJSON_CreateNumber(v->valuedouble));
      else cJSON_AddItemToArray(mm,cJSON_CreateNull());}}
  else {cJSON_AddNullToObject(grid,"mm");}
  cJSON_Delete(snapshot);
}
