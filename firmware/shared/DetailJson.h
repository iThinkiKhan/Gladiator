#pragma once
#include "DetailCodec.h"
#include "JsonEscape.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
namespace robotlink {
// Stream a validated wire tree without allocating a full native JSON string.
// Optional extraction retains only one root member for inexpensive crowd reads.
template<class Sink> class DetailJsonStream {
  DetailReader &r;Sink sink;std::string *member;std::string_view memberKey;
  static constexpr size_t LIMIT=MAX_PAYLOAD*6+256;
  size_t written=0;bool capturing=false;
  bool append(std::string_view s){
    if(s.size()>LIMIT-written||!sink(s.data(),s.size())){r.valid=false;return false;}
    written+=s.size();if(capturing&&member)member->append(s.data(),s.size());return true;
  }
  std::string_view string(){const size_t n=r.u16();if(!r.valid||n>r.size-r.pos){r.valid=false;return {};}
    const auto p=reinterpret_cast<const char *>(r.data+r.pos);r.pos+=n;return {p,n};}
  bool quote(std::string_view s){return jsonstream::writeString(s,[&](const char *p,size_t n){return append({p,n});});}
  bool value(unsigned depth){
    if(depth>16||!r.valid){r.valid=false;return false;}
    const uint8_t tag=r.u8();if(!r.valid)return false;
    switch(tag){
      case 0:return append("null");case 1:return append("false");case 2:return append("true");
      case 3:{double n=r.number();if(!r.valid)return false;if(!std::isfinite(n))return append("null");char text[32];const int count=snprintf(text,sizeof(text),"%.17g",n);return count>0&&size_t(count)<sizeof(text)&&append({text,size_t(count)});}
      case 4:{auto s=string();return r.valid&&quote(s);}
      case 5:case 6:{const size_t count=r.u16();if(!r.valid||count>r.size-r.pos){r.valid=false;return false;}if(!append(tag==5?"[":"{"))return false;
        for(size_t i=0;i<count;++i){if(i&&!append(","))return false;std::string_view key;
          if(tag==6){key=string();if(!r.valid||!quote(key)||!append(":"))return false;}
          const bool selected=member&&depth==0&&tag==6&&key==memberKey;
          if(selected){member->clear();capturing=true;}
          if(!value(depth+1))return false;
          if(selected)capturing=false;
        }
        return append(tag==5?"]":"}");}
      default:r.valid=false;return false;
    }
  }
public:
  DetailJsonStream(DetailReader &reader,Sink target,std::string *selected=nullptr,std::string_view key={}) :r(reader),sink(target),member(selected),memberKey(key){}
  bool decodeObject(){written=0;capturing=false;if(member)member->clear();
    const bool ok=r.size&&r.data[0]==6&&value(0)&&r.valid&&r.pos==r.size;
    if(!ok){r.valid=false;if(member)member->clear();}return ok;}
};
// Host/other callers can still request a complete string explicitly.
class DetailJson {
  DetailReader &r;std::string &out;std::string *member;std::string_view memberKey;
public:
  DetailJson(DetailReader &reader,std::string &result,std::string *selected=nullptr,std::string_view key={}) :r(reader),out(result),member(selected),memberKey(key){}
  bool decodeObject(){out.clear();auto append=[&](const char *p,size_t n){out.append(p,n);return true;};
    const bool ok=DetailJsonStream(r,append,member,memberKey).decodeObject();if(!ok)out.clear();return ok;}
};
}
