#pragma once
// C6 relays operator intent; calibration, PWM and safety remain on the S3.
esp_err_t mastHandler(httpd_req_t *req){
  lock();auto robot=copyRobot();auto s=state;unlock();
  auto m=cJSON_DetachItemFromObjectCaseSensitive(robot,"mast");cJSON_Delete(robot);
  if(!m)m=cJSON_CreateObject();
  bool fresh=s.receivedUs&&esp_timer_get_time()-s.receivedUs<250000;
  auto age=cJSON_GetObjectItemCaseSensitive(m,"transportAgeMs");
  auto sourceAge=cJSON_GetObjectItemCaseSensitive(m,"sourceAgeMs");
  fresh=fresh&&cJSON_IsNumber(age)&&age->valuedouble>=0&&age->valuedouble<250&&
    cJSON_IsNumber(sourceAge)&&sourceAge->valuedouble>=0&&sourceAge->valuedouble<250;
  cJSON_AddBoolToObject(m,"fresh",fresh);return replyJson(req,serialize(m));
}
esp_err_t mastCommandHandler(httpd_req_t *req){
  // Key-free operator requests are validated and executed exclusively by S3.
  std::string body;if(!readBody(req,body,256))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid mast command");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());double token=0,seq=0,op=0,pan=0,tilt=0;
  bool valid=cJSON_IsObject(doc)&&integerValue(doc,"session",1,4294967295.0,token)&&
    integerValue(doc,"sequence",1,4294967295.0,seq)&&integerValue(doc,"operation",0,7,op)&&
    integerValue(doc,"pan",-450,450,pan)&&integerValue(doc,"tilt",-200,200,tilt);
  cJSON_Delete(doc);
  if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected session, sequence, operation 0-7, pan/tilt in tenths of head degrees");
  mast::Command c;c.session=uint32_t(token);c.sequence=uint32_t(seq);c.operation=mast::Operation(uint8_t(op));c.pan=int16_t(pan);c.tilt=int16_t(tilt);
  return queueRequest(req,Type::MAST_CONTROL,c.encode());
}
esp_err_t mastConfigHandler(httpd_req_t *req){
  std::string body;if(!readBody(req,body,768))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid mast commissioning body");
  auto doc=cJSON_ParseWithLength(body.data(),body.size());const char *a=textValue(doc,"action");
  bool valid=cJSON_IsObject(doc)&&(!strcmp(a,"calibrate")||!strcmp(a,"test")||!strcmp(a,"jog-test")||!strcmp(a,"stop"));
  bool pinRequired=!strcmp(a,"jog-test");auto pin=cJSON_GetObjectItemCaseSensitive(doc,"pin");
  bool pinValid=cJSON_IsNumber(pin)&&(pin->valuedouble==10||pin->valuedouble==13);
  cJSON_Delete(doc);if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected calibrate/test/jog-test/stop action");
  if(pinRequired&&!pinValid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Jog requires selected GPIO10 or GPIO13; reload the control page");
  robotlink::Writer w;for(char c:body)w.u8(uint8_t(c));return queueRequest(req,Type::MAST_CONFIG,w);
}
