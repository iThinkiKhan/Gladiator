#pragma once
// Operator-only sensor settings; firmware/network maintenance auth is unchanged.
SemaphoreHandle_t cameraMutex;
esp_err_t cameraUnavailable(httpd_req_t *req,const char *message){httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_set_type(req,"text/plain");return httpd_resp_send(req,message,HTTPD_RESP_USE_STRLEN);}
bool cameraValue(JsonVariantConst v,int low,int high,int &out){if(!v.is<int>())return false;out=v.as<int>();return out>=low&&out<=high;}
esp_err_t cameraSettingsHandler(httpd_req_t *req){
  if(!cameraReady||otaUpdating)return cameraUnavailable(req,"Camera unavailable");
  StaticJsonDocument<768> input;
  if(req->method==HTTP_POST){
    if(!req->content_len||req->content_len>384)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Camera settings body too large");
    char body[385]={};size_t got=0;while(got<req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0)return ESP_FAIL;got+=n;}
    if(deserializeJson(input,body)||!input.is<JsonObject>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid camera JSON object");
    int v;for(auto name:{"brightness","aec","aecValue","vflip","hmirror","quality"})if(!input[name].isNull()){
      int low=!strcmp(name,"brightness")?-2:!strcmp(name,"quality")?10:0;
      int high=!strcmp(name,"brightness")?2:!strcmp(name,"aecValue")?1200:!strcmp(name,"quality")?40:1;
      if(!cameraValue(input[name],low,high,v))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Camera setting outside supported range");
    }
    if(!input["resolution"].isNull()){
      const char *r=input["resolution"]|"";
      if(strcmp(r,"QQVGA")&&strcmp(r,"QVGA")&&strcmp(r,"VGA")&&strcmp(r,"SVGA"))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Resolution QQVGA/QVGA/VGA/SVGA");
    }
  }
  if(xSemaphoreTake(cameraMutex,pdMS_TO_TICKS(300))!=pdTRUE)return cameraUnavailable(req,"Camera busy");
  sensor_t *s=esp_camera_sensor_get();int failed=0;
  if(s&&req->method==HTTP_POST){
    if(!input["resolution"].isNull()){const char *r=input["resolution"]|"QVGA";framesize_t f=!strcmp(r,"QQVGA")?FRAMESIZE_QQVGA:!strcmp(r,"VGA")?FRAMESIZE_VGA:!strcmp(r,"SVGA")?FRAMESIZE_SVGA:FRAMESIZE_QVGA;failed|=s->set_framesize(s,f);}
    if(!input["brightness"].isNull())failed|=s->set_brightness(s,input["brightness"].as<int>());
    if(!input["aec"].isNull())failed|=s->set_exposure_ctrl(s,input["aec"].as<int>());
    if(!input["aecValue"].isNull())failed|=s->set_aec_value(s,input["aecValue"].as<int>());
    if(!input["vflip"].isNull())failed|=s->set_vflip(s,input["vflip"].as<int>());
    if(!input["hmirror"].isNull())failed|=s->set_hmirror(s,input["hmirror"].as<int>());
    // Keep the mount orientation across camera restarts.
    if(!failed&&(!input["vflip"].isNull()||!input["hmirror"].isNull())){nvs_handle_t nvs;
      if(nvs_open("masthead",NVS_READWRITE,&nvs)==ESP_OK){nvs_set_u8(nvs,"vflip",s->status.vflip?1:0);nvs_set_u8(nvs,"hmirror",s->status.hmirror?1:0);nvs_commit(nvs);nvs_close(nvs);}}
    if(!input["quality"].isNull())failed|=s->set_quality(s,input["quality"].as<int>());
  }
  StaticJsonDocument<512> d;
  if(s){const auto &v=s->status;d["resolution"]=v.framesize==FRAMESIZE_QQVGA?"QQVGA":v.framesize==FRAMESIZE_VGA?"VGA":v.framesize==FRAMESIZE_SVGA?"SVGA":"QVGA";
    d["brightness"]=v.brightness;d["aec"]=v.aec;d["aecValue"]=v.aec_value;d["vflip"]=v.vflip;d["hmirror"]=v.hmirror;d["quality"]=v.quality;}
  xSemaphoreGive(cameraMutex);
  if(!s||failed)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Camera rejected settings; read current values");
  String out;serializeJson(d,out);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_send(req,out.c_str(),out.length());
}
