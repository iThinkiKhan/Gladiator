#pragma once
#include "HardwareTestAssets.h"
// Assets live in flash; serving the tester adds no robot command or motion path.
esp_err_t hardwareAsset(httpd_req_t *req,const unsigned char *bytes,size_t length,const char *type){
  httpd_resp_set_type(req,type);
  httpd_resp_set_hdr(req,"Content-Encoding","gzip");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
  httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' http: data:; connect-src 'self'; frame-ancestors 'self'; base-uri 'none'");
  return httpd_resp_send(req,reinterpret_cast<const char *>(bytes),length);
}
esp_err_t hardwarePageHandler(httpd_req_t *req){return hardwareAsset(req,hardware_test::HTML,sizeof(hardware_test::HTML),"text/html; charset=utf-8");}
esp_err_t hardwareScriptHandler(httpd_req_t *req){return hardwareAsset(req,hardware_test::JS,sizeof(hardware_test::JS),"text/javascript; charset=utf-8");}
esp_err_t hardwareStyleHandler(httpd_req_t *req){return hardwareAsset(req,hardware_test::CSS,sizeof(hardware_test::CSS),"text/css; charset=utf-8");}
