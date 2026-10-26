#pragma once
#include <string>
#include <string_view>
#include <cstddef>
namespace jsonstream {
// JSON-quote a UTF-8 log without allocating a second full-size escaped copy.
// Sink failure stops immediately so an HTTP error cannot become valid empty JSON.
template<class Sink> bool writeString(std::string_view value,Sink sink){
  char buffer[512];size_t used=0;
  auto emit=[&](char c){if(used==sizeof(buffer)){if(!sink(buffer,used))return false;used=0;}buffer[used++]=c;return true;};
  if(!emit('"'))return false;
  constexpr char hex[]="0123456789abcdef";
  for(unsigned char c:value){
    if(c=='"'||c=='\\'){if(!emit('\\')||!emit(c))return false;}
    else if(c<0x20){for(char part:{'\\','u','0','0',hex[c>>4],hex[c&15]})if(!emit(part))return false;}
    else if(!emit(c))return false;
  }
  return emit('"')&&(!used||sink(buffer,used));
}
}
