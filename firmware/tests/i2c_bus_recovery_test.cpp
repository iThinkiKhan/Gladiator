// Host test for I2CBus's stuck-bus guard (src/sensors/I2CBus.h, compiled unchanged).
#include "I2CBus.h"
#include <cassert>
#include <cstdio>
TwoWire Wire;
namespace sim { int sdaPin=board::I2C_SDA, sclPin=board::I2C_SCL; bool sdaDriven=false, sclDriven=false, sclStuck=false; int slaveHoldClocks=0, clocks=0; }
static void reset(int hold, bool sclStuck=false){ sim::slaveHoldClocks=hold; sim::sclStuck=sclStuck; sim::clocks=0; sim::sdaDriven=sim::sclDriven=false; Wire=TwoWire(); }
int main(){
  sensors::I2CBus bus; uint16_t v=0;
  // Idle bus: no clocks, no restart, transaction goes through.
  reset(0); assert(bus.read16(0x40,1,v)); assert(sim::clocks==0 && Wire.ends==0 && Wire.transactions==2);
  // Sensor stuck mid-byte, releases after 5 clocks: cleared, then the read runs.
  reset(5); assert(bus.read16(0x40,1,v)); assert(sim::clocks>=5 && Wire.ends==1 && Wire.begins==1);
  assert(bus.stuckEvents()==1 && bus.recoveries()==1);
  // Stuck at boot is cleared by begin() before the driver starts.
  reset(7); assert(bus.begin() && sim::sda() && sim::clocks>=7);
  // SDA held low for good: bounded clocks, read refused, driver never used.
  reset(-1); assert(!bus.read16(0x40,1,v)); assert(Wire.transactions==0 && sim::clocks<=19);
  assert(!bus.present(0x4A) && !bus.write16(0x40,0,1) && Wire.transactions==0);
  // SCL held low: no clocks possible, refused without touching the driver.
  reset(0,true); assert(!bus.ensureIdle() && Wire.transactions==0);
  // Released later: next call clears and proceeds.
  reset(0); assert(bus.ensureIdle());
  std::puts("I2C bus recovery passed: idle passthrough, mid-byte clear, boot clear, permanent SDA/SCL faults refused without driver calls.");
}
