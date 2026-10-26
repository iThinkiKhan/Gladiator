#pragma once
#include <stdint.h>
#include <string.h>
namespace halloween {
constexpr uint16_t ORANGE=0xFD20, BLACK=0;
constexpr uint32_t CYCLE_MS=24000;
enum class Scene:uint8_t { GREETING, EYES, BRAND };
inline Scene scene(uint32_t now){const auto t=now%CYCLE_MS;return t<6000?Scene::GREETING:t<16000?Scene::EYES:Scene::BRAND;}
inline bool closed(uint32_t now){return scene(now)==Scene::EYES&&(now%2800)<180;}
inline uint8_t frameKey(uint32_t now){return uint8_t(scene(now))*2+(closed(now)?1:0);}
inline const char* name(uint32_t now){switch(scene(now)){case Scene::GREETING:return "happy-halloween";case Scene::EYES:return "orange-eyes";default:return "suburban-office-it";}}
template<class G> void text(G& g,const char* value,int y,int size){g.setTextSize(size);g.setTextColor(ORANGE);g.setCursor((g.width()-int(strlen(value))*6*size)/2,y);g.print(value);}
template<class G> void draw(G& g,uint32_t now){
 const int w=g.width(),h=g.height();g.fillScreen(BLACK);g.setTextWrap(false);
 if(scene(now)==Scene::EYES){
  const bool blink=closed(now);const int ew=w*29/100,eh=blink?5:h*45/100;
  for(int cx:{w/3,2*w/3}){
   g.fillRoundRect(cx-ew/2,h/2-eh/2,ew,eh,blink?2:14,ORANGE);
   if(!blink)g.fillRoundRect(cx-w/60,h/2-eh/3,w/30,eh*2/3,4,BLACK);
  }
 }else{
  g.drawRoundRect(8,8,w-16,h-16,14,ORANGE);
  if(scene(now)==Scene::GREETING){text(g,"HAPPY",h/2-42,4);text(g,"HALLOWEEN!",h/2+9,3);}
  else{text(g,"SUBURBAN",h/2-34,3);text(g,"OFFICE IT",h/2+9,3);}
 }
}
}
