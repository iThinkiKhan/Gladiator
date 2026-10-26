#pragma once
#include "BoothAssets.h"
esp_err_t boothPageHandler(httpd_req_t *req){return hardwareAsset(req,booth::HTML,sizeof(booth::HTML),"text/html; charset=utf-8");}
esp_err_t boothScriptHandler(httpd_req_t *req){return hardwareAsset(req,booth::JS,sizeof(booth::JS),"text/javascript; charset=utf-8");}

esp_err_t mastSettingsScriptHandler(httpd_req_t *req){return hardwareAsset(req,booth::SETTINGS,sizeof(booth::SETTINGS),"text/javascript; charset=utf-8");}
esp_err_t mastWorkbenchScriptHandler(httpd_req_t *req){return hardwareAsset(req,booth::WORKBENCH,sizeof(booth::WORKBENCH),"text/javascript; charset=utf-8");}
