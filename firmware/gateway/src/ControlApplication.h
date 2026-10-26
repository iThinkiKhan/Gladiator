#pragma once
esp_err_t controlHandler(httpd_req_t *req){
  // Key-free field relay; only S3 can accept ownership/arm/deadman/power.
  std::string body;if(!readBody(req,body,512))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid control body");
  cJSON *doc=cJSON_ParseWithLength(body.data(),body.size());robotlink::Control c;double v=0;bool valid=cJSON_IsObject(doc);
  const char *keys[]={"epoch","client","sequence","observedMs","operation","deadman","overdrive","power","left","right"};
  double values[10]={};for(unsigned i=0;i<10;++i){valid=integerValue(doc,keys[i],i>=8?-128:0,i<4?4294967295.0:i>=8?127:255,v)&&valid;values[i]=v;}
  cJSON_Delete(doc);if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected integer control fields");
  c.epoch=values[0];c.client=values[1];c.sequence=values[2];c.observedMs=values[3];c.operation=values[4];c.deadman=values[5];c.overdrive=values[6];c.power=values[7];c.left=values[8];c.right=values[9];
  return queueRequest(req,robotlink::Type::CONTROL,c.encode());
}
