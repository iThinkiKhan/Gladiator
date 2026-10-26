// Eye/display mode rules (shared/VisualState.h): a parked robot must not flash
// ALERT just because someone or something is close to the LiDAR.
#include "../shared/VisualState.h"
#include <cassert>
#include <cstdio>
using visual::Mode;
static visual::Input in(const char *state,const char *battery,bool range,uint32_t mm){
  visual::Input s;s.controllerFresh=true;strcpy(s.state,state);strcpy(s.battery,battery);s.rangeValid=range;s.nearestMm=mm;return s;}
int main(){
  assert(visual::automatic(in("SAFE","OK",true,318))==Mode::READY);     // 2026-10-23 flashing case
  assert(visual::automatic(in("SAFE","OK",true,100))==Mode::READY);
  assert(visual::automatic(in("ARMED","OK",true,318))==Mode::ALERT);
  assert(visual::automatic(in("DRIVE","OK",true,499))==Mode::ALERT);
  assert(visual::automatic(in("DRIVE","OK",true,500))==Mode::ACTIVE);
  assert(visual::automatic(in("DRIVE","OK",false,100))==Mode::ACTIVE);   // no valid range: no alert
  assert(visual::automatic(in("SAFE","LOCKOUT",true,2000))==Mode::ALERT);
  assert(visual::automatic(in("SAFE","CRITICAL",false,0))==Mode::ALERT);
  assert(visual::automatic(in("SAFE","WARN",false,0))==Mode::READY);
  auto stale=in("SAFE","OK",true,318);stale.controllerFresh=false;assert(visual::automatic(stale)==Mode::OFFLINE);
  // A temporary expression never hides an ALERT or OFFLINE.
  assert(visual::effective(Mode::ALERT,Mode::GREETING,1000,900,30000)==Mode::ALERT);
  assert(visual::effective(Mode::READY,Mode::GREETING,1000,900,30000)==Mode::GREETING);
  assert(visual::effective(Mode::READY,Mode::GREETING,40000,900,30000)==Mode::READY);
  std::puts("Visual state passed: no proximity alert while SAFE; alert while armed/driving <500 mm; battery alerts always.");
}
