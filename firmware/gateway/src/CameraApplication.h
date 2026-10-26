#pragma once
// Async proxy keeps a slow camera request out of the drive/mast HTTP task.
struct CameraJob {httpd_req_t *request;std::string body;char url[64];bool post;};
std::atomic<bool> cameraBusy{false};
esp_err_t cameraUnavailable(httpd_req_t *req,const char *message){httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_set_type(req,"text/plain");return httpd_resp_send(req,message,HTTPD_RESP_USE_STRLEN);}
void cameraProxyTask(void *data){
  auto *job=static_cast<CameraJob *>(data);esp_http_client_config_t cfg={};cfg.url=job->url;cfg.timeout_ms=1200;cfg.disable_auto_redirect=true;
  esp_http_client_handle_t client=esp_http_client_init(&cfg);std::string body;int status=502;
  if(client){
    esp_http_client_set_method(client,job->post?HTTP_METHOD_POST:HTTP_METHOD_GET);
    esp_http_client_set_header(client,"Content-Type","application/json");
    if(esp_http_client_open(client,job->post?job->body.size():0)==ESP_OK){
      bool written=!job->post||esp_http_client_write(client,job->body.data(),job->body.size())==int(job->body.size());
      if(written&&esp_http_client_fetch_headers(client)>=0){status=esp_http_client_get_status_code(client);char buffer[256];int n;
        while(body.size()<1536&&(n=esp_http_client_read(client,buffer,std::min(sizeof(buffer),size_t(1536-body.size()))))>0)body.append(buffer,n);
      }
    }
    esp_http_client_close(client);esp_http_client_cleanup(client);
  }
  auto parsed=cJSON_ParseWithLength(body.data(),body.size());bool valid=cJSON_IsObject(parsed);cJSON_Delete(parsed);
  if(status!=200||body.empty()||!valid){httpd_resp_set_status(job->request,"502 Bad Gateway");replyJson(job->request,"{\"error\":\"Camera unavailable or settings rejected\"}");}
  else replyJson(job->request,body);
  httpd_req_async_handler_complete(job->request);delete job;cameraBusy=false;vTaskDelete(nullptr);
}
esp_err_t cameraProxyHandler(httpd_req_t *req){
  std::string body;if(req->method==HTTP_POST&&!readBody(req,body,384))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Camera JSON body required");
  bool expected=false;if(!cameraBusy.compare_exchange_strong(expected,true))return cameraUnavailable(req,"Camera request already pending");
  auto *job=new(std::nothrow) CameraJob;if(!job){cameraBusy=false;return ESP_FAIL;}
  char ip[16];lock();strlcpy(ip,visualCameraIp,sizeof(ip));unlock();in_addr parsed={};
  if(inet_pton(AF_INET,ip,&parsed)!=1){delete job;cameraBusy=false;return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Set camera address in maintenance");}
  snprintf(job->url,sizeof(job->url),"http://%s/api/%s",ip,!strcmp(req->uri,"/api/camera/status")?"status":"camera");job->body=std::move(body);job->post=req->method==HTTP_POST;
  if(httpd_req_async_handler_begin(req,&job->request)!=ESP_OK){delete job;cameraBusy=false;return ESP_FAIL;}
  if(xTaskCreate(cameraProxyTask,"camera-proxy",6144,job,3,nullptr)!=pdPASS){cameraUnavailable(job->request,"Camera worker unavailable");httpd_req_async_handler_complete(job->request);delete job;cameraBusy=false;}
  return ESP_OK;
}
