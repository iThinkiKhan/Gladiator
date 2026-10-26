#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_system.h>
#include <nvs.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cmath>
#include "Config.h"
#include "HalloweenDisplay.h"
#include "VisualState.h"
#include "OperatorStatus.h"
#include "OperatorRender.h"
#include "OperatorProtocol.h"
#include "CamOtaPolicy.h"
#include "MaintenancePage.h"

namespace {
SPIClass displaySpi(HSPI);
Adafruit_ST7789 tft(&displaySpi,config::CS,config::DC,config::RESET);
SemaphoreHandle_t stateMutex;
struct VisualState {
  opstatus::Facts diagnostics;
  visual::Mode mode=visual::Mode::OFFLINE;
  char state[12]="UNKNOWN",battery[12]="UNKNOWN",message[49]={};
  float volts=0;int range=-1;bool fresh=false;uint32_t received=0;
} state;
std::atomic<bool> cameraReady{false},streaming{false};
std::atomic<int> cameraError{0};
std::atomic<uint32_t> frames{0},streamErrors{0},pollErrors{0};
httpd_handle_t api=nullptr,video=nullptr;

// The animation buffer lives in PSRAM; Wi-Fi and camera DMA retain internal RAM.
class Canvas : public Adafruit_GFX {
public:
  uint16_t *pixels=nullptr;
  Canvas(int w,int h):Adafruit_GFX(w,h) {
    pixels=static_cast<uint16_t *>(heap_caps_malloc(w*h*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  }
  void drawPixel(int16_t x,int16_t y,uint16_t color) override {
    if(pixels&&x>=0&&y>=0&&x<width()&&y<height())pixels[y*width()+x]=color;
  }
  void fillScreen(uint16_t color) override {if(pixels)for(int i=0;i<width()*height();++i)pixels[i]=color;}
};
Canvas *canvas=nullptr;
bool initCamera() {
  if(!psramFound()){cameraError=ESP_ERR_NO_MEM;Serial.println("PSRAM missing: camera disabled; check board/power.");return false;}
  camera_config_t c={};
  c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
  c.pin_d0=5;c.pin_d1=18;c.pin_d2=19;c.pin_d3=21;
  c.pin_d4=36;c.pin_d5=39;c.pin_d6=34;c.pin_d7=35;
  c.pin_xclk=0;c.pin_pclk=22;c.pin_vsync=25;c.pin_href=23;
  c.pin_sccb_sda=26;c.pin_sccb_scl=27;c.pin_pwdn=32;c.pin_reset=-1;
  c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;
  c.frame_size=FRAMESIZE_SVGA;c.jpeg_quality=14;c.fb_count=2; // Reserve room for live operator resolution changes.
  c.fb_location=CAMERA_FB_IN_PSRAM;c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);
  cameraError=err;
  if(err!=ESP_OK){Serial.printf("Camera init failed: 0x%x\n",err);return false;}
  if(esp_camera_sensor_get()->set_framesize(esp_camera_sensor_get(),FRAMESIZE_QVGA)!=0)return false;
  // Mount orientation (vflip/hmirror) is saved by the operator page; default upright.
  {nvs_handle_t nvs;uint8_t vflip=0,hmirror=0;
   if(nvs_open("masthead",NVS_READONLY,&nvs)==ESP_OK){nvs_get_u8(nvs,"vflip",&vflip);nvs_get_u8(nvs,"hmirror",&hmirror);nvs_close(nvs);}
   sensor_t *s=esp_camera_sensor_get();s->set_vflip(s,vflip?1:0);s->set_hmirror(s,hmirror?1:0);}
  return true;
}
#include "Maintenance.h"
#include "CameraControl.h"
opstatus::Local localStatus(const VisualState &s,uint32_t now){
 opstatus::Local l;l.wifi=WiFi.status()==WL_CONNECTED;l.gateway=l.wifi&&s.received&&visual::fresh(now,s.received,config::STALE_MS);
 l.controller=l.gateway&&s.fresh;l.camera=cameraReady.load();l.cameraError=cameraError.load();l.starting=now<10000;l.battery=l.controller?s.battery:"UNKNOWN";return l;
}
esp_err_t statusHandler(httpd_req_t *req) {
  StaticJsonDocument<2048> doc;
  VisualState s;xSemaphoreTake(stateMutex,portMAX_DELAY);s=state;xSemaphoreGive(stateMutex);
  doc["firmware"]=config::VERSION;doc["cameraReady"]=cameraReady.load();doc["streaming"]=streaming.load();
  doc["frames"]=frames.load();doc["streamErrors"]=streamErrors.load();doc["pollErrors"]=pollErrors.load();
  doc["freeHeap"]=ESP.getFreeHeap();doc["freePsram"]=ESP.getFreePsram();doc["displayBuffered"]=canvas&&canvas->pixels;
  auto local=localStatus(s,millis());auto problem=opstatus::select(s.diagnostics,local,millis());
  doc["cameraError"]=cameraError.load();doc["displayCode"]=problem.code;doc["displayTitle"]=problem.title;doc["displayAction"]=problem.action;doc["displayDashboard"]=opstatus::dashboard(problem,millis());
  auto facts=doc.createNestedObject("displayHealth");facts["power"]=opstatus::name(local.controller?s.diagnostics.power:opstatus::Health::UNKNOWN);facts["imu"]=opstatus::name(local.controller?s.diagnostics.imu:opstatus::Health::UNKNOWN);facts["lidar"]=opstatus::name(local.controller?s.diagnostics.lidar:opstatus::Health::UNKNOWN);facts["radar"]=opstatus::name(local.controller?s.diagnostics.radar:opstatus::Health::UNKNOWN);facts["mast"]=s.diagnostics.mast;facts["link"]=s.diagnostics.link;
  doc["mode"]=visual::name(local.controller?s.mode:visual::Mode::OFFLINE);
  if(config::HALLOWEEN_DISPLAY){
    doc["mode"]="halloween";doc["displayCode"]="HALLOWEEN";doc["displayTitle"]="Halloween booth loop";doc["displayAction"]="Local display only";doc["displayDashboard"]=false;
    doc["displayAutonomous"]=true;doc["displayScene"]=halloween::name(millis());doc["displayCycleMs"]=halloween::CYCLE_MS;
    doc["cameraDisabledForDisplay"]=true;doc["gatewayPolling"]=false;
  }
  doc["ip"]=WiFi.localIP().toString();doc["rssi"]=WiFi.RSSI();
  doc["role"]="masthead-camera";doc["bootId"]=bootId;doc["uptimeMs"]=millis();doc["otaEnabled"]=maintenanceReady;
  doc["otaUpdating"]=otaUpdating.load();doc["otaPendingVerification"]=pendingVerification.load();
  doc["otaSlot"]=esp_ota_get_running_partition()->label;
  const auto *next=esp_ota_get_next_update_partition(nullptr);doc["otaMaxBytes"]=next?next->size:0;
  doc["recoveryAp"]=recoveryActive.load();doc["recoverySsid"]=recoverySsid;doc["recoveryIp"]="192.168.9.1";
  char body[2048];size_t n=serializeJson(doc,body,sizeof(body));
  httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
  return httpd_resp_send(req,body,n);
}
esp_err_t rootHandler(httpd_req_t *req) {
  constexpr char html[]=R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>Gladiator camera</title><style>body{font:18px system-ui;background:#0c131a;color:white;max-width:640px;margin:32px auto}img{width:100%}</style><h1>Gladiator camera</h1><p>Live QVGA camera ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â· display state comes from the C6 gateway.</p><button id="toggle">Start video</button><img id="feed" alt="Live camera" hidden><p><a href="/maintenance">Firmware updates and Wi-Fi settings</a></p><p><a href="/api/status">Subsystem status</a></p><script>let on=false;toggle.onclick=()=>{on=!on;feed.hidden=!on;if(on)feed.src='http://'+location.hostname+':81/stream';else feed.removeAttribute('src');toggle.textContent=on?'Stop video':'Start video'};document.addEventListener('visibilitychange',()=>{if(document.hidden&&on)toggle.click()});</script>)HTML";
  httpd_resp_set_type(req,"text/html");return httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
}
esp_err_t streamHandler(httpd_req_t *req) {
  if(otaUpdating){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_send(req,"Firmware update in progress",HTTPD_RESP_USE_STRLEN);}
  if(!cameraReady){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_send(req,"Camera unavailable",HTTPD_RESP_USE_STRLEN);}
  bool expected=false;if(!streaming.compare_exchange_strong(expected,true)){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_send(req,"One viewer at a time",HTTPD_RESP_USE_STRLEN);}
  httpd_resp_set_type(req,"multipart/x-mixed-replace;boundary=gladiator-frame");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  esp_err_t err=ESP_OK;
  while(err==ESP_OK&&(WiFi.status()==WL_CONNECTED||recoveryActive)&&!otaUpdating) {
    const uint32_t start=millis();xSemaphoreTake(cameraMutex,portMAX_DELAY);camera_fb_t *fb=esp_camera_fb_get();xSemaphoreGive(cameraMutex);
    if(!fb){err=ESP_FAIL;break;}
    char header[128];int n=snprintf(header,sizeof(header),"\r\n--gladiator-frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",unsigned(fb->len));
    err=httpd_resp_send_chunk(req,header,n);
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,reinterpret_cast<const char *>(fb->buf),fb->len);
    esp_camera_fb_return(fb); // Always return the framebuffer, including failed sends.
    if(err==ESP_OK)++frames;
    uint32_t elapsed=millis()-start;vTaskDelay(pdMS_TO_TICKS(elapsed<67?67-elapsed:1));
  }
  if(err!=ESP_OK)++streamErrors;streaming=false;return ESP_FAIL; // Close the old multipart socket.
}
bool startServers() {
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_uri_handlers=10;
  cfg.max_open_sockets=3;cfg.lru_purge_enable=true;cfg.send_wait_timeout=2;cfg.recv_wait_timeout=2;
  if(httpd_start(&api,&cfg)!=ESP_OK)return false;
  httpd_uri_t root={};root.uri="/";root.method=HTTP_GET;root.handler=rootHandler;
  httpd_uri_t status={};status.uri="/api/status";status.method=HTTP_GET;status.handler=statusHandler;
  if(httpd_register_uri_handler(api,&root)!=ESP_OK||httpd_register_uri_handler(api,&status)!=ESP_OK)return false;
  struct Route {const char *uri;httpd_method_t method;esp_err_t(*handler)(httpd_req_t *);};
  const Route routes[]={{"/maintenance",HTTP_GET,maintenanceHandler},{"/api/ota",HTTP_POST,camOtaHandler},{"/api/confirm",HTTP_POST,confirmHandler},{"/api/config",HTTP_POST,camConfigHandler},{"/api/camera",HTTP_GET,cameraSettingsHandler},{"/api/camera",HTTP_POST,cameraSettingsHandler}};
  for(const auto &r:routes){httpd_uri_t route={};route.uri=r.uri;route.method=r.method;route.handler=r.handler;if(httpd_register_uri_handler(api,&route)!=ESP_OK)return false;}
  if(config::HALLOWEEN_DISPLAY)return true; // No video server in the local booth display mode.
  cfg.server_port=81;cfg.ctrl_port+=1;cfg.max_open_sockets=1;cfg.max_uri_handlers=1;
  // The infinite stream has its own HTTP task. Status stays responsive.
  if(httpd_start(&video,&cfg)!=ESP_OK)return false;
  httpd_uri_t stream={};stream.uri="/stream";stream.method=HTTP_GET;stream.handler=streamHandler;
  return httpd_register_uri_handler(video,&stream)==ESP_OK;
}
bool pollGateway() {
  WiFiClient client;HTTPClient http;http.setConnectTimeout(250);http.setTimeout(300);http.useHTTP10(true);
  auto settings=networkSnapshot();
  if(!http.begin(client,String(settings.gateway)+"/api/visual"))return false;
  int code=http.GET(),length=http.getSize();bool ok=false;
  if(code==200&&length>0&&length<=1536) {
    // Arduino 2.x rounds WiFiClient timeout to whole seconds. Collect explicitly
    // with a deadline so fragmented HTTP bodies cannot block or fail immediately.
    char body[1537];size_t used=0;uint32_t start=millis();auto &stream=http.getStream();
    while(used<size_t(length)&&millis()-start<300) {
      int available=stream.available();
      if(available>0){int n=stream.read(reinterpret_cast<uint8_t *>(body)+used,std::min(size_t(available),size_t(length)-used));if(n>0)used+=n;}
      else if(!stream.connected())break;
      vTaskDelay(1);
    }
    if(used!=size_t(length)){http.end();return false;}
    body[used]=0;StaticJsonDocument<2048> doc;auto err=deserializeJson(doc,body,used);
    visual::Mode mode;
    if(!err&&doc["schema"]==1&&doc["fresh"].is<bool>()&&
       visual::parseMode(doc["mode"]|"",mode)) {
      VisualState next;next.mode=mode;next.fresh=doc["fresh"].as<bool>();next.received=millis();
      strlcpy(next.state,doc["state"]|"UNKNOWN",sizeof(next.state));strlcpy(next.battery,doc["battery"]|"UNKNOWN",sizeof(next.battery));
      strlcpy(next.message,doc["message"]|"",sizeof(next.message));
      next.diagnostics=opstatus::readFacts(doc["diagnostics"]);
      next.range=doc["nearestMm"].is<int>()?doc["nearestMm"].as<int>():-1;
      float v=doc["volts"]|0.0f;next.volts=std::isfinite(v)&&v>=0&&v<100?v:0;
      xSemaphoreTake(stateMutex,portMAX_DELAY);state=next;xSemaphoreGive(stateMutex);ok=true;
    }
  }
  http.end();return ok;
}
void networkTask(void *) {
  uint32_t retry=millis()-10000;bool wasConnected=false;
  for(;;) {
    lastNetworkMs=millis();
    if(otaUpdating){vTaskDelay(pdMS_TO_TICKS(100));continue;}
    if(networkChanged.exchange(false)){startRecovery();WiFi.disconnect();retry=millis()-10000;disconnectedSince=millis();}
    if(WiFi.status()!=WL_CONNECTED) {
      // Keep recovery clients on a stable channel while they administer the CAM.
      if(!(recoveryActive&&WiFi.softAPgetStationNum()>0)&&millis()-retry>=(recoveryActive?60000u:10000u)){retry=millis();auto settings=networkSnapshot();if(validNetwork(settings))WiFi.begin(settings.ssid,settings.password);}
      if(millis()-disconnectedSince>=config::RECOVERY_MS)startRecovery();
      wasConnected=false;
    } else {
      disconnectedSince=millis();
      if(!wasConnected){Serial.printf("Camera IP: %s\n",WiFi.localIP().toString().c_str());wasConnected=true;}
      if(recoveryActive&&WiFi.softAPgetStationNum()==0){WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);recoveryActive=false;}
      if(!config::HALLOWEEN_DISPLAY&&!pollGateway())++pollErrors;
    }
    vTaskDelay(pdMS_TO_TICKS(config::POLL_MS));
  }
}
void render(const VisualState &s,uint32_t now) {
  if(config::HALLOWEEN_DISPLAY){
    static uint8_t shown=255;const uint8_t key=halloween::frameKey(now);if(key==shown)return;shown=key;
    if(canvas&&canvas->pixels){halloween::draw(*canvas,now);tft.drawRGBBitmap(0,0,canvas->pixels,canvas->width(),canvas->height());}
    else halloween::draw(tft,now);
    return;
  }

  auto local=localStatus(s,now);bool fresh=local.controller;
  auto mode=fresh?s.mode:visual::Mode::OFFLINE;
 auto problem=opstatus::select(s.diagnostics,local,now);const bool statusView=opstatus::dashboard(problem,now);
  uint16_t color=mode==visual::Mode::ALERT?ST77XX_RED:mode==visual::Mode::GREETING?ST77XX_GREEN:mode==visual::Mode::OFFLINE?0x630c:ST77XX_CYAN;
  if(canvas&&canvas->pixels) {
    canvas->fillScreen(ST77XX_BLACK);int w=canvas->width(),h=canvas->height();
    if(statusView)opstatus::statusPanel(*canvas,s.diagnostics,local,problem,local.wifi?WiFi.RSSI():0);
    else {
    int offset=mode==visual::Mode::SCANNING?int(sinf(now/450.0f)*w/10):int(sinf(now/1200.0f)*5);
    int eyeH=mode==visual::Mode::ALERT?h*2/3:h/2;
    if(now%4200<120&&mode!=visual::Mode::ALERT)eyeH=5;
    if(mode==visual::Mode::GREETING)eyeH=h/3;
    for(int center:{w/3,2*w/3}) {
      canvas->fillRoundRect(center-w/9+offset,h/2-eyeH/2,w*2/9,eyeH,12,color);
      if(eyeH>10)canvas->fillRoundRect(center-w/24+offset,h/2-eyeH/3,w/12,eyeH*2/3,6,ST77XX_BLACK);
    }
    if(mode==visual::Mode::SCANNING)canvas->drawFastVLine((now/8)%w,2,h-4,ST77XX_BLUE);
    }
    tft.drawRGBBitmap(0,24,canvas->pixels,w,h);
  }
  static uint32_t lastStatus=0;
  if(now-lastStatus>=500) {
    lastStatus=now;tft.fillRect(0,0,tft.width(),24,ST77XX_BLACK);tft.setTextColor(color);tft.setTextSize(2);tft.setCursor(4,4);
    tft.print("GLADIATOR ");tft.print(problem.level?"CHECK":visual::name(mode));
    tft.fillRect(0,tft.height()-24,tft.width(),24,ST77XX_BLACK);
    char battery[32];if(fresh&&s.volts>0)snprintf(battery,sizeof(battery),"%.1fV %s",s.volts,s.battery);else snprintf(battery,sizeof(battery),"Battery unknown");
    opstatus::line(tft,4,tft.height()-24,2,battery,opstatus::WHITE,tft.width()-8);
    auto settings=networkSnapshot();const char *gateway=!strncmp(settings.gateway,"http://",7)?settings.gateway+7:settings.gateway;
    char footer[64];if(s.message[0]&&fresh)snprintf(footer,sizeof(footer),"%s",s.message);
    else snprintf(footer,sizeof(footer),"Gateway %s | %s",gateway,local.wifi?"Wi-Fi OK":"Wi-Fi lost");
    opstatus::line(tft,4,tft.height()-8,1,footer,opstatus::GRAY,tft.width()-8);
  }
}
}
void setup() {
  Serial.begin(115200);pinMode(4,OUTPUT);digitalWrite(4,LOW); // Flash LED stays off; no display reset on GPIO4.
  stateMutex=xSemaphoreCreateMutex();configASSERT(stateMutex);
  bootId=esp_random();maintenanceReady=loadCamSettings();
  if(!maintenanceReady)Serial.println("CAM settings unavailable; OTA disabled.");
  displaySpi.begin(config::SCLK,-1,config::MOSI,config::CS);
  tft.init(VISUAL_TFT_WIDTH,VISUAL_TFT_HEIGHT);tft.setRotation(config::DISPLAY_ROTATION);tft.setSPISpeed(20000000);tft.fillScreen(ST77XX_BLACK);
  canvas=new Canvas(tft.width(),config::HALLOWEEN_DISPLAY?tft.height():tft.height()-48);
  if(!canvas->pixels)Serial.println("Animation buffer unavailable; text status remains active.");
  cameraMutex=xSemaphoreCreateMutex();
  if(config::HALLOWEEN_DISPLAY){pinMode(32,OUTPUT);digitalWrite(32,HIGH);cameraReady=false;cameraError=ESP_OK;}
  else cameraReady=cameraMutex&&initCamera();
  WiFi.mode(WIFI_STA);WiFi.setHostname("gladiator-camera");WiFi.setSleep(false);WiFi.setAutoReconnect(false);
  if(!validNetwork(networkSnapshot()))Serial.println("Wi-Fi not configured; recovery AP will start in 30 seconds.");
  httpReady=startServers();if(!httpReady)Serial.println("HTTP server startup failed; display continues.");
  configASSERT(xTaskCreatePinnedToCore(networkTask,"visual-network",8192,nullptr,1,nullptr,0)==pdPASS);
}
void loop() {
  maintenanceLoop();
  if(otaUpdating){displayPaused=true;delay(10);return;}
  displayPaused=false;
  uint32_t start=millis();VisualState s;
  static uint32_t lastCameraRetry=0;
  if(!config::HALLOWEEN_DISPLAY&&!cameraReady&&start-lastCameraRetry>=10000){lastCameraRetry=start;cameraReady=initCamera();}
  xSemaphoreTake(stateMutex,portMAX_DELAY);s=state;xSemaphoreGive(stateMutex);render(s,start);
  uint32_t elapsed=millis()-start;delay(elapsed<config::FRAME_MS?config::FRAME_MS-elapsed:1);
}
