#include <cassert>
#include <cstdio>
#include <cstring>
#include "RangeGrid.h"
int main(){
 uint16_t mm[64];for(unsigned i=0;i<64;++i)mm[i]=uint16_t(i*1000);
 robotlink::Writer w;rangegrid::write(w,mm);assert(w.size==128);
 auto frame=robotlink::Frame{robotlink::Type::RANGE,1,2,reinterpret_cast<const char *>(w.data),w.size};
 robotlink::Reader r(frame);rangegrid::Sample s;assert(rangegrid::read(r,s));assert(!memcmp(mm,s.mm,sizeof(mm)));
 for(size_t n=0;n<128;++n){frame.length=n;robotlink::Reader shortRead(frame);rangegrid::Sample before;before.mm[0]=999;assert(!rangegrid::read(shortRead,before));assert(before.mm[0]==999);}
 frame.length=129;robotlink::Reader longRead(frame);assert(!rangegrid::read(longRead,s));
 frame.length=128;robotlink::Reader invalid(frame);invalid.valid=false;assert(!rangegrid::read(invalid,s));
 robotlink::Reader offset(frame);offset.pos=129;assert(!rangegrid::read(offset,s));
 s.seen=s.usable=true;s.receivedMs=100;s.sourceAgeMs=25;
 assert(s.fresh(100)&&s.fresh(2574)&&!s.fresh(2575));
 s.receivedMs=0xfffffff0u;s.sourceAgeMs=20;assert(s.fresh(100));
 s.sourceAgeMs=UINT32_MAX;assert(!s.fresh(100));s.sourceAgeMs=2500;assert(!s.fresh(100));
 s.sourceAgeMs=0;s.usable=false;assert(!s.fresh(100));s.usable=true;s.seen=false;assert(!s.fresh(100));
 puts("PASS compact grid wire format, exact bounds, round trip, freshness and rollover");
}
