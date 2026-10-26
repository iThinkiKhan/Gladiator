#pragma once
#include "OperatorStatus.h"
#include <stdio.h>
namespace opstatus {
constexpr uint16_t WHITE=0xffff,GRAY=0x9cf3,GREEN=0x8f39,AMBER=0xfdc0,RED=0xfa49,LINE=0x2945;
template<class G>void line(G &g,int x,int y,int size,const char *value,uint16_t color,int width){
 char text[65];unsigned count=width>0?unsigned(width/(6*size)):0;if(count>64)count=64;
 unsigned n=0;while(n<count&&value[n]){text[n]=value[n];++n;}if(value[n]&&n>=3)text[n-1]=text[n-2]=text[n-3]='.';text[n]=0;
 g.setTextWrap(false);g.setTextColor(color);g.setTextSize(size);g.setCursor(x,y);g.print(text);
}
inline uint16_t healthColor(Health h){return h==Health::OK?GREEN:h==Health::UNKNOWN?GRAY:AMBER;}
template<class G>void chip(G &g,int x,int y,int width,const char *label,Health health){char text[48];snprintf(text,sizeof(text),"%s: %s",label,shortName(health));line(g,x,y,1,text,healthColor(health),width);}
template<class G>void statusPanel(G &g,const Facts &f,const Local &l,const Message &m,int rssi){
 const bool live=l.wifi&&l.gateway&&l.controller;
 const int w=g.width(),col=w/2;g.fillScreen(0);line(g,8,4,2,m.title,m.level==2?RED:m.level==1?AMBER:WHITE,w-16);
 line(g,8,28,1,m.action,GRAY,w-16);g.drawFastHLine(8,44,w-16,LINE);
 chip(g,8,54,col-12,"S3",live?Health::OK:Health::UNKNOWN);chip(g,col+4,54,col-12,"POWER",live?f.power:Health::UNKNOWN);
 chip(g,8,72,col-12,"IMU",live?f.imu:Health::UNKNOWN);chip(g,col+4,72,col-12,"LIDAR",live?f.lidar:Health::UNKNOWN);
 chip(g,8,90,col-12,"CAMERA",l.camera?Health::OK:Health::OFFLINE);chip(g,col+4,90,col-12,"RADAR",live?f.radar:Health::UNKNOWN);
 char text[72];if(live&&strcmp(f.stop,"NONE"))snprintf(text,sizeof(text),"Mast %s | Last stop: %s | %s",f.mast,f.stop,f.link);
 else snprintf(text,sizeof(text),"Mast %s | %s | Wi-Fi %d dBm",live?f.mast:"UNKNOWN",l.gateway?f.link:"NONE",rssi);
 line(g,8,108,1,text,GRAY,w-16);
}
}
