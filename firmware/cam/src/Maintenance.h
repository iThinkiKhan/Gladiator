#pragma once
// Included within the CAM namespace after initCamera().
struct NetworkSettings {char ssid[33]={},password[64]={},gateway[64]={};};
NetworkSettings network;
char camAdminKey[33]={},recoverySsid[40]={};
bool maintenanceReady=false,httpReady=false;
std::atomic<bool> otaUpdating{false},networkChanged{false},recoveryActive{false},rebootRequested{false},displayPaused{false};
std::atomic<uint32_t> lastNetworkMs{0},lastLoopMs{0};
uint32_t bootId=0,rebootRequestedMs=0,disconnectedSince=0;
std::atomic<bool> pendingVerification{false};
bool validNetwork(const NetworkSettings &n) {
  IPAddress ip;const char *host=n.gateway+7;
  return strlen(n.ssid)>0&&strlen(n.ssid)<=32&&strlen(n.password)>=8&&strlen(n.password)<=63&&
    !strncmp(n.gateway,"http://",7)&&ip.fromString(host)&&
    (ip[0]==10||(ip[0]==172&&ip[1]>=16&&ip[1]<=31)||(ip[0]==192&&ip[1]==168));
}
NetworkSettings networkSnapshot(){xSemaphoreTake(stateMutex,portMAX_DELAY);auto n=network;xSemaphoreGive(stateMutex);return n;}
bool loadCamSettings() {
  nvs_handle_t nvs;if(nvs_open("masthead",NVS_READWRITE,&nvs)!=ESP_OK)return false;
  size_t size=sizeof(camAdminKey);bool saveKey=nvs_get_str(nvs,"admin",camAdminKey,&size)!=ESP_OK||!camota::validHex(camAdminKey,32);
  if(saveKey){
    if(camota::validHex(VISUAL_OTA_KEY,32))strlcpy(camAdminKey,VISUAL_OTA_KEY,sizeof(camAdminKey));
    else {uint8_t random[16];esp_fill_random(random,sizeof(random));for(unsigned i=0;i<16;++i)snprintf(camAdminKey+i*2,3,"%02x",random[i]);}
    if(nvs_set_str(nvs,"admin",camAdminKey)!=ESP_OK||nvs_commit(nvs)!=ESP_OK){nvs_close(nvs);return false;}
  }
  char stored[256]={};size=sizeof(stored);bool loaded=false;
  if(nvs_get_str(nvs,"network",stored,&size)==ESP_OK){
    StaticJsonDocument<512> d;if(!deserializeJson(d,stored)){
      strlcpy(network.ssid,d["ssid"]|"",sizeof(network.ssid));strlcpy(network.password,d["password"]|"",sizeof(network.password));strlcpy(network.gateway,d["gatewayUrl"]|"",sizeof(network.gateway));loaded=validNetwork(network);
    }
  }
  if(!loaded){
    strlcpy(network.ssid,VISUAL_WIFI_SSID,sizeof(network.ssid));strlcpy(network.password,VISUAL_WIFI_PASSWORD,sizeof(network.password));strlcpy(network.gateway,VISUAL_GATEWAY_URL,sizeof(network.gateway));
    if(validNetwork(network)){
      StaticJsonDocument<512> d;d["ssid"]=network.ssid;d["password"]=network.password;d["gatewayUrl"]=network.gateway;serializeJson(d,stored,sizeof(stored));
      if(measureJson(d)>=sizeof(stored)||nvs_set_str(nvs,"network",stored)!=ESP_OK||nvs_commit(nvs)!=ESP_OK){nvs_close(nvs);return false;}
    }
  }
  nvs_close(nvs);
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);snprintf(recoverySsid,sizeof(recoverySsid),"Gladiator-CAM-%02X%02X%02X",mac[3],mac[4],mac[5]);
  esp_ota_img_states_t imageState;pendingVerification=esp_ota_get_state_partition(esp_ota_get_running_partition(),&imageState)==ESP_OK&&imageState==ESP_OTA_IMG_PENDING_VERIFY;
  return true;
}
void startRecovery() {
  if(!maintenanceReady||recoveryActive)return;
  WiFi.disconnect();WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(IPAddress(192,168,9,1),IPAddress(192,168,9,1),IPAddress(255,255,255,0));
  recoveryActive=WiFi.softAP(recoverySsid,camAdminKey,6,false,2);
  if(recoveryActive)Serial.printf("CAM recovery AP: %s at http://192.168.9.1\n",recoverySsid);
}
esp_err_t camReply(httpd_req_t *req,const char *status,const char *body) {
  httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN);
}
bool camAuthorize(httpd_req_t *req) {
  char supplied[40]={};
  if(!maintenanceReady||httpd_req_get_hdr_value_str(req,"X-Gladiator-Key",supplied,sizeof(supplied))!=ESP_OK||!camota::matchesKey(supplied,camAdminKey)){
    camReply(req,"401 Unauthorized","{\"error\":\"CAM maintenance key required\"}");return false;
  }return true;
}
bool camReadExact(httpd_req_t *req,uint8_t *out,size_t size,uint32_t started) {
  size_t got=0;
  while(got<size&&uint32_t(millis()-started)<120000){int n=httpd_req_recv(req,reinterpret_cast<char *>(out)+got,size-got);if(n<=0)return false;got+=n;}
  return got==size;
}
esp_err_t confirmHandler(httpd_req_t *req) {
  if(!camAuthorize(req))return ESP_OK;
  if(millis()<5000||!httpReady||uint32_t(millis()-lastLoopMs.load())>1000||uint32_t(millis()-lastNetworkMs.load())>3000)
    return camReply(req,"409 Conflict","{\"error\":\"Startup checks still running\"}");
  if(pendingVerification){
    if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return camReply(req,"500 Internal Server Error","{\"error\":\"Boot confirmation failed\"}");
    pendingVerification=false;
  }
  return camReply(req,"200 OK","{\"confirmed\":true}");
}
esp_err_t maintenanceHandler(httpd_req_t *req) {
  httpd_resp_set_type(req,"text/html");httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_send(req,CAM_MAINTENANCE_PAGE,HTTPD_RESP_USE_STRLEN);
}
esp_err_t camConfigHandler(httpd_req_t *req) {
  if(!camAuthorize(req))return ESP_OK;
  if(otaUpdating||pendingVerification)return camReply(req,"409 Conflict","{\"error\":\"Confirm firmware before changing Wi-Fi\"}");
  if(req->content_len==0||req->content_len>256)return camReply(req,"400 Bad Request","{\"error\":\"Invalid network settings\"}");
  char body[257]={};if(!camReadExact(req,reinterpret_cast<uint8_t *>(body),req->content_len,millis()))return ESP_FAIL;
  StaticJsonDocument<512> d;NetworkSettings next;
  if(deserializeJson(d,body)||!d["ssid"].is<const char *>()||!d["password"].is<const char *>()||!d["gatewayUrl"].is<const char *>())
    return camReply(req,"400 Bad Request","{\"error\":\"Expected ssid, password, gatewayUrl\"}");
  if(strlen(d["ssid"])>32||strlen(d["password"])>63||strlen(d["gatewayUrl"])>=sizeof(next.gateway))
    return camReply(req,"400 Bad Request","{\"error\":\"Network field too long\"}");
  strlcpy(next.ssid,d["ssid"],sizeof(next.ssid));strlcpy(next.password,d["password"],sizeof(next.password));strlcpy(next.gateway,d["gatewayUrl"],sizeof(next.gateway));
  if(!validNetwork(next))return camReply(req,"400 Bad Request","{\"error\":\"SSID required; WPA2 password 8-63 characters; gatewayUrl must be http://private-IPv4\"}");
  char stored[256];if(measureJson(d)>=sizeof(stored))return camReply(req,"400 Bad Request","{\"error\":\"Encoded settings too long\"}");
  serializeJson(d,stored,sizeof(stored));nvs_handle_t nvs;esp_err_t err=nvs_open("masthead",NVS_READWRITE,&nvs);
  if(err==ESP_OK){err=nvs_set_str(nvs,"network",stored);if(err==ESP_OK)err=nvs_commit(nvs);nvs_close(nvs);}
  if(err!=ESP_OK)return camReply(req,"500 Internal Server Error","{\"error\":\"Could not save settings\"}");
  xSemaphoreTake(stateMutex,portMAX_DELAY);network=next;xSemaphoreGive(stateMutex);networkChanged=true;
  return camReply(req,"200 OK","{\"saved\":true,\"message\":\"Reconnecting; recovery AP available if connection fails\"}");
}
esp_err_t camOtaHandler(httpd_req_t *req) {
  if(!camAuthorize(req))return ESP_OK;
  const auto *partition=esp_ota_get_next_update_partition(nullptr);
  if(pendingVerification)return camReply(req,"409 Conflict","{\"error\":\"Confirm the running firmware first\"}");
  if(!partition||req->content_len<512||req->content_len>partition->size)
    return camReply(req,"400 Bad Request","{\"error\":\"Expected CAM application firmware.bin within OTA slot size\"}");
  char expectedDigest[65]={};bool checkDigest=httpd_req_get_hdr_value_len(req,"X-Gladiator-SHA256")>0;
  if(checkDigest&&(httpd_req_get_hdr_value_str(req,"X-Gladiator-SHA256",expectedDigest,sizeof(expectedDigest))!=ESP_OK||!camota::validHex(expectedDigest,64)))
    return camReply(req,"400 Bad Request","{\"error\":\"Invalid SHA256 header\"}");
  uint8_t bytes[2048];const uint32_t started=millis();
  if(!camReadExact(req,bytes,512,started))return ESP_FAIL;
  if(!camota::validPrefix(bytes,512))return camReply(req,"400 Bad Request","{\"error\":\"Wrong application, chip, or partition layout; build the CAM OTA target\"}");
  bool expected=false;if(!otaUpdating.compare_exchange_strong(expected,true))return camReply(req,"409 Conflict","{\"error\":\"Update already running\"}");
  uint32_t drain=millis();while((streaming||!displayPaused)&&millis()-drain<5000)vTaskDelay(1);
  if(streaming||!displayPaused){otaUpdating=false;return camReply(req,"503 Service Unavailable","{\"error\":\"Video/display did not pause; retry\"}");}
  const bool hadCamera=cameraReady.exchange(false);if(hadCamera)esp_camera_deinit();
  esp_ota_handle_t handle=0;bool begun=false;esp_err_t err=esp_ota_begin(partition,req->content_len,&handle);begun=err==ESP_OK;
  Serial.printf("CAM OTA begin: %s, %u bytes, %s\n",partition->label,unsigned(req->content_len),esp_err_to_name(err));
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);
  size_t remaining=req->content_len-512;
  if(err==ESP_OK){mbedtls_sha256_update_ret(&hash,bytes,512);err=esp_ota_write(handle,bytes,512);}
  while(err==ESP_OK&&remaining){size_t n=std::min(remaining,sizeof(bytes));if(!camReadExact(req,bytes,n,started)){err=ESP_FAIL;break;}
    mbedtls_sha256_update_ret(&hash,bytes,n);err=esp_ota_write(handle,bytes,n);remaining-=n;vTaskDelay(1);}
  uint8_t digest[32];char actualDigest[65];mbedtls_sha256_finish_ret(&hash,digest);mbedtls_sha256_free(&hash);camota::digestHex(digest,actualDigest);
  if(err==ESP_OK&&checkDigest&&strcmp(expectedDigest,actualDigest))err=ESP_ERR_INVALID_CRC;
  if(err==ESP_OK){err=esp_ota_end(handle);begun=false;}
  if(err==ESP_OK)err=esp_ota_set_boot_partition(partition);
  Serial.printf("CAM OTA finish: %u bytes remaining, %s\n",unsigned(remaining),esp_err_to_name(err));
  if(err!=ESP_OK){
    if(begun)esp_ota_abort(handle);
    if(hadCamera)cameraReady=initCamera();
    otaUpdating=false;
    return camReply(req,"400 Bad Request","{\"error\":\"Update rejected or interrupted; running firmware retained\"}");
  }
  char reply[160];snprintf(reply,sizeof(reply),"{\"updated\":true,\"sha256\":\"%s\",\"confirmationRequired\":true}",actualDigest);
  camReply(req,"200 OK",reply);rebootRequestedMs=millis();rebootRequested=true;return ESP_OK;
}
void maintenanceLoop() {
  lastLoopMs=millis();
  if(rebootRequested&&uint32_t(millis()-rebootRequestedMs)>=750)esp_restart();
  if(pendingVerification&&millis()>config::OTA_CONFIRM_MS){
    Serial.println("OTA not confirmed; rolling back.");esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  static char command[32]={};static size_t used=0;
  for(unsigned budget=0;budget<64&&Serial.available();++budget){char c=Serial.read();
    if(c=='\n'||c=='\r'){command[used]=0;if(!strcmp(command,"credentials"))Serial.printf("{\"camProvisioning\":true,\"adminKey\":\"%s\",\"recoverySsid\":\"%s\"}\n",camAdminKey,recoverySsid);used=0;}
    else if(used<sizeof(command)-1)command[used++]=c;else used=0;
  }
}
