#pragma once
// Robot-side partial commissioning record. No PWM, link command or enable path.
// A single NVS value atomically stores fields and revision, independent of clients.
cJSON *mastSetupEmpty(){
  auto d=cJSON_CreateObject();cJSON_AddNumberToObject(d,"schema",1);
  cJSON_AddNumberToObject(d,"revision",0);cJSON_AddObjectToObject(d,"fields");return d;
}
bool mastSetupField(const cJSON *v){
  const char *k=v->string;if(!k)return false;
  if(!strcmp(k,"panReverse")||!strcmp(k,"tiltReverse"))return cJSON_IsBool(v);
  double low=0,high=0;bool whole=false,pin=false;
  if(!strcmp(k,"hz")){low=50;high=100;whole=true;}
  else if(!strcmp(k,"speed")){low=.1;high=30;}
  else {
    const char *s=!strncmp(k,"pan",3)?k+3:!strncmp(k,"tilt",4)?k+4:nullptr;
    if(!s)return false;
    if(!strcmp(s,"Pin")){low=10;high=13;pin=whole=true;}
    else if(!strcmp(s,"Low")||!strcmp(s,"Neutral")||!strcmp(s,"High")){low=500;high=2575;whole=true;}
    else if(!strcmp(s,"ShaftMin")){low=-180;high=0;}
    else if(!strcmp(s,"ShaftMax")){low=0;high=180;}
    else if(!strcmp(s,"Min")){low=k[0]=='p'?-45:-20;high=0;}
    else if(!strcmp(s,"Max")){low=0;high=k[0]=='p'?45:20;}
    else return false;
  }
  if(!cJSON_IsString(v)||strlen(v->valuestring)>24)return false;
  if(!v->valuestring[0])return !pin; // Incomplete measurement fields are intentional.
  char *end=nullptr;double n=strtod(v->valuestring,&end);
  return end!=v->valuestring&&!*end&&std::isfinite(n)&&n>=low&&n<=high&&
    (!whole||floor(n)==n)&&(!pin||n==10||n==13);
}
bool mastSetupValid(const cJSON *d){
  auto schema=cJSON_GetObjectItemCaseSensitive(d,"schema");
  auto rev=cJSON_GetObjectItemCaseSensitive(d,"revision");
  auto fields=cJSON_GetObjectItemCaseSensitive(d,"fields");
  if(!cJSON_IsObject(d)||!cJSON_IsNumber(schema)||schema->valuedouble!=1||
    !cJSON_IsNumber(rev)||rev->valuedouble<0||rev->valuedouble>4294967294.0||
    floor(rev->valuedouble)!=rev->valuedouble||!cJSON_IsObject(fields))return false;
  for(auto v=fields->child;v;v=v->next)if(!mastSetupField(v))return false;
  return true;
}
esp_err_t mastSetupRead(cJSON **doc){
  *doc=nullptr;nvs_handle_t nvs;auto err=nvs_open("mast-setup",NVS_READONLY,&nvs);
  if(err==ESP_ERR_NVS_NOT_FOUND){*doc=mastSetupEmpty();return ESP_OK;}
  if(err!=ESP_OK)return err;
  char text[3072];size_t n=sizeof(text);err=nvs_get_str(nvs,"record",text,&n);nvs_close(nvs);
  if(err==ESP_ERR_NVS_NOT_FOUND){*doc=mastSetupEmpty();return ESP_OK;}
  if(err!=ESP_OK)return err;
  *doc=cJSON_Parse(text);
  if(!mastSetupValid(*doc)){cJSON_Delete(*doc);*doc=nullptr;return ESP_FAIL;}
  return ESP_OK;
}
esp_err_t mastSetupError(httpd_req_t *req,const char *status,const char *error){
  httpd_resp_set_status(req,status);auto d=cJSON_CreateObject();
  cJSON_AddStringToObject(d,"error",error);return replyJson(req,serialize(d));
}
esp_err_t mastSetupHandler(httpd_req_t *req){
  // HTTP server serializes handlers. Revision prevents stale full-record overwrites.
  cJSON *stored=nullptr;
  if(mastSetupRead(&stored)!=ESP_OK)return mastSetupError(req,"500 Internal Server Error","Cannot read robot setup storage");
  if(req->method==HTTP_GET)return replyJson(req,serialize(stored));
  std::string body;
  if(!readBody(req,body,2048)){cJSON_Delete(stored);return mastSetupError(req,"400 Bad Request","Invalid setup patch");}
  auto patch=cJSON_ParseWithLength(body.data(),body.size());
  auto fields=cJSON_GetObjectItemCaseSensitive(patch,"fields");
  double rev=0;
  bool valid=cJSON_IsObject(patch)&&integerValue(patch,"revision",0,4294967294.0,rev)&&cJSON_IsObject(fields);
  if(valid)for(auto v=fields->child;v;v=v->next)if(!mastSetupField(v)){valid=false;break;}
  if(!valid){cJSON_Delete(patch);cJSON_Delete(stored);return mastSetupError(req,"400 Bad Request","Expected revision and known setup fields with valid values");}
  auto revision=cJSON_GetObjectItemCaseSensitive(stored,"revision");
  if(rev!=revision->valuedouble){cJSON_Delete(patch);cJSON_Delete(stored);return mastSetupError(req,"409 Conflict","Robot setup changed; read it and retry the changed fields");}
  auto target=cJSON_GetObjectItemCaseSensitive(stored,"fields");bool changed=false;
  for(auto v=fields->child;v;v=v->next){
    auto old=cJSON_GetObjectItemCaseSensitive(target,v->string);
    if(old&&cJSON_Compare(old,v,true))continue;
    auto value=cJSON_Duplicate(v,true);cJSON_DeleteItemFromObjectCaseSensitive(target,v->string);
    cJSON_AddItemToObject(target,v->string,value);changed=true;
  }
  cJSON_Delete(patch);
  if(!changed)return replyJson(req,serialize(stored)); // Keepalive/polling never wear flash.
  if(revision->valuedouble>=4294967294.0){cJSON_Delete(stored);return mastSetupError(req,"500 Internal Server Error","Setup revision exhausted");}
  cJSON_SetNumberValue(revision,revision->valuedouble+1);
  const auto encoded=serialize(stored);nvs_handle_t nvs;auto err=nvs_open("mast-setup",NVS_READWRITE,&nvs);
  if(err==ESP_OK){err=nvs_set_str(nvs,"record",encoded.c_str());if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
  if(err!=ESP_OK)return mastSetupError(req,"500 Internal Server Error","Robot setup was not saved to flash");
  cJSON *readback=nullptr;
  if(mastSetupRead(&readback)!=ESP_OK)return mastSetupError(req,"500 Internal Server Error","Cannot verify robot setup readback");
  return replyJson(req,serialize(readback));
}
