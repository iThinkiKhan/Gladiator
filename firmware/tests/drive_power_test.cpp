#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/DrivePower.h"
#include "../src/Orientation.h"
int main(){
  unsigned cases=0;
  for(int p=0;p<=100;++p)for(int a=-100;a<=100;++a)for(int b=-100;b<=100;++b){
    int8_t l=a,r=b;drivepower::cap(l,r,p,false);
    assert(abs(l)<=p&&abs(r)<=p);
    assert((a>=0?l>=0:l<=0)&&(b>=0?r>=0:r<=0));
    if(abs(a)<=p&&abs(b)<=p)assert(l==a&&r==b);
    int8_t ul=a,ur=b;drivepower::cap(ul,ur,p,true);assert(ul==a&&ur==b);++cases;
  }
  for(int sign:{-1,1}){
    int8_t l=sign*25,r=sign*25;orientation::straightTrim(l,r,false);
    drivepower::cap(l,r,25,false);assert(abs(l)<=25&&abs(r)<=25&&abs(l)==25);
  }
  // A ramp from a formerly high output must obey a newly lowered ceiling now.
  int8_t l=95,r=-70;drivepower::cap(l,r,25,false);assert(l==25&&r==-18);
  puts("Drive power checks passed: 4,080,501 signed input/power combinations, trim, lowered ceiling, explicit Overdrive.");
  assert(cases==4080501);
}
