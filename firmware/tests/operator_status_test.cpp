#include "../shared/OperatorStatus.h"
#include "../shared/OperatorRender.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <fstream>
#include <vector>
using namespace opstatus;
struct Graphics {
 int size=1,x=0,y=0;unsigned color=0xffff;std::string body;
 int width(){return 320;}
 void fillScreen(int){body="<rect width='320' height='122' fill='#000'/>";}
 void setTextWrap(bool){}void setTextColor(unsigned c){color=c;}void setTextSize(int s){size=s;}void setCursor(int a,int b){x=a;y=b;}
 static std::string rgb(unsigned c){char s[16];snprintf(s,sizeof(s),"#%02x%02x%02x",((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);return s;}
 void print(const char *v){assert(x>=0&&y>=0&&x+int(strlen(v))*6*size<=320&&y+8*size<=122);std::string s;for(;*v;++v){if(*v=='&')s+="&amp;";else if(*v=='<')s+="&lt;";else s+=*v;}
 body+="<text x='"+std::to_string(x)+"' y='"+std::to_string(y+7*size)+"' fill='"+rgb(color)+"' font-family='monospace' font-size='"+std::to_string(8*size)+"' textLength='"+std::to_string(s.size()*6*size)+"' lengthAdjust='spacingAndGlyphs'>"+s+"</text>";}
 void drawFastHLine(int a,int b,int w,unsigned c){body+="<rect x='"+std::to_string(a)+"' y='"+std::to_string(b)+"' width='"+std::to_string(w)+"' height='1' fill='"+rgb(c)+"'/>";}
};
int main(int argc,char **argv){
 Facts f;f.available=true;f.brain=f.power=f.imu=f.lidar=Health::OK;f.radar=Health::OFFLINE;strcpy(f.mast,"MANUAL");strcpy(f.link,"UART");
 Local l;l.wifi=l.gateway=l.controller=l.camera=true;l.battery="OK";
 assert(!strcmp(select(f,l,0).code,"READY"));assert(!dashboard(select(f,l,0),0)&&dashboard(select(f,l,9000),9000));
 f.available=false;assert(!strcmp(select(f,l,0).code,"BASIC"));f.available=true;
 l.camera=false;l.cameraError=0x101;assert(!strcmp(select(f,l,0).code,"CAMERA_MEMORY"));l.cameraError=0;assert(!strcmp(select(f,l,0).code,"CAMERA"));l.battery="CRITICAL";assert(!strcmp(select(f,l,0).code,"BATTERY_CRITICAL"));l.battery="OK";
 f.power=Health::OFFLINE;assert(!strcmp(select(f,l,0).code,"POWER"));f.power=Health::OK;
 f.imu=Health::NO_POSE;assert(!strcmp(select(f,l,6000).code,"IMU_POSE"));assert(!strcmp(select(f,l,0).code,"CAMERA"));l.camera=true;
 assert(!strcmp(select(f,l,0).code,"IMU_POSE"));f.imu=Health::OK;
 l.controller=false;f.restarting=true;assert(!strcmp(select(f,l,0).code,"RESTARTING"));f.restarting=false;assert(!strcmp(select(f,l,0).code,"BRAIN"));l.controller=true;
 l.gateway=false;assert(!strcmp(select(f,l,0).code,"GATEWAY"));l.starting=true;assert(!strcmp(select(f,l,0).code,"CONNECTING"));l.gateway=true;
 l.wifi=false;assert(!strcmp(select(f,l,0).code,"WIFI"));l.wifi=true;
 for(unsigned i=0;i<=unsigned(Health::NO_POSE);++i)assert(parse(name(Health(i)))==Health(i));assert(parse("made-up")==Health::UNKNOWN);
 std::vector<std::string> cases;std::vector<std::string> labels,voltages,networks;
 auto capture=[&](const char *name){Graphics g;statusPanel(g,f,l,select(f,l,6000),-38);cases.push_back(g.body);labels.push_back(name);voltages.push_back(l.wifi&&l.gateway&&l.controller?"15.3V OK":"Battery unknown");networks.push_back(l.wifi?"Gateway 192.168.8.1 | Wi-Fi OK":"Gateway 192.168.8.1 | Wi-Fi lost");};
 capture("Healthy status page / simulated");l.camera=false;capture("Camera missing / simulated");l.camera=true;f.power=Health::OFFLINE;capture("Power monitor missing / simulated");f.power=Health::OK;f.imu=Health::NO_POSE;capture("Orientation missing / simulated");f.imu=Health::OK;l.controller=false;f.restarting=true;capture("S3 reboot loop / simulated");l.controller=true;l.wifi=false;capture("Wi-Fi lost / simulated");
 if(argc>1){std::ofstream file(argv[1]);file<<"<svg xmlns='http://www.w3.org/2000/svg' width='1024' height='434' viewBox='0 0 1024 434'><rect width='1024' height='434' fill='#151d24'/>";
 for(unsigned i=0;i<cases.size();++i){int x=16+(i%3)*336,y=16+(i/3)*210;file<<"<g transform='translate("<<x<<","<<y<<")'><text y='12' fill='white' font-family='sans-serif' font-size='11'>"<<labels[i]<<"</text><g transform='translate(0,24)'><rect width='320' height='170' fill='black'/><text x='4' y='17' fill='#87e0c0' font-family='monospace' font-size='16'>GLADIATOR / STATUS</text><g transform='translate(0,24)'>"<<cases[i]<<"</g><text x='4' y='160' fill='white' font-family='monospace' font-size='16'>"<<voltages[i]<<"</text><text x='4' y='169' fill='#a0b5c0' font-family='monospace' font-size='8'>"<<networks[i]<<"</text></g></g>";}
 file<<"</svg>";}
 puts("Operator status passed: fault priority, local versus remote failures, optional radar, legacy feed, face/status cycling, and all render text bounds.");
}
