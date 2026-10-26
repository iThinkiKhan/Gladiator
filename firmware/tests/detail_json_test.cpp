#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <new>
#include "cJSON.h"
#include "../shared/DetailJson.h"
using namespace robotlink;
static bool rejectLargeAllocation=false;
void *operator new(std::size_t n){if(rejectLargeAllocation&&n>4096)throw std::bad_alloc();if(void *p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void *p) noexcept{std::free(p);}
void operator delete(void *p,std::size_t) noexcept{std::free(p);}
static void encode(DetailWriter &w,const cJSON *v){
 if(cJSON_IsNull(v))w.u8(0);else if(cJSON_IsFalse(v))w.u8(1);else if(cJSON_IsTrue(v))w.u8(2);
 else if(cJSON_IsNumber(v)){w.u8(3);w.number(v->valuedouble);}else if(cJSON_IsString(v)){w.u8(4);w.string(v->valuestring);}
 else {const bool array=cJSON_IsArray(v);w.u8(array?5:6);w.u16(cJSON_GetArraySize(v));for(auto p=v->child;p;p=p->next){if(!array)w.string(p->string);encode(w,p);}}
}
static void check(const cJSON *v){
 DetailWriter w;encode(w,v);assert(w.valid);std::string out,range;DetailReader r{w.data,w.size};assert(DetailJson(r,out,&range,"tofFront").decodeObject());
 std::string streamed,selected;DetailReader wire{w.data,w.size};auto sink=[&](const char *p,size_t n){streamed.append(p,n);return true;};
 assert(DetailJsonStream(wire,sink,&selected,"tofFront").decodeObject());assert(streamed==out&&selected==range);
 auto decoded=cJSON_Parse(out.c_str());assert(decoded&&cJSON_Compare(v,decoded,true));cJSON_Delete(decoded);
 auto tof=cJSON_GetObjectItemCaseSensitive(v,"tofFront");if(tof){auto selected=cJSON_Parse(range.c_str());assert(selected&&cJSON_Compare(tof,selected,true));cJSON_Delete(selected);}else assert(range.empty());
 for(size_t i=0;i<w.size;++i){DetailReader bad{w.data,i};assert(!DetailJson(bad,out,&range,"tofFront").decodeObject());assert(out.empty()&&range.empty());}
 w.u8(0);DetailReader extra{w.data,w.size};assert(!DetailJson(extra,out).decodeObject());
}
int main(int argc,char **argv){
 auto example=cJSON_Parse(u8R"({"tofFront":{"raw":{"distanceMm":[20,3999,null]},"meta":{"health":"OK","ageMs":20}},"types":[null,false,true,0,-1,4294967295,0.1,1.23456789e-99,"café\n\t\u0000\"\\"],"empty":{}})");assert(example);check(example);cJSON_Delete(example);
 if(argc>1){std::ifstream file(argv[1]);std::stringstream text;text<<file.rdbuf();auto receipt=cJSON_Parse(text.str().c_str());assert(receipt);auto native=cJSON_GetObjectItemCaseSensitive(receipt,"native");assert(native);check(native);cJSON_Delete(receipt);}
 std::string out;for(auto bytes:{std::string("\x06\x01\x00\xff",4),std::string("\x06\x00\x00\x00",4),std::string("\x05\x00\x00",3)}){DetailReader r{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()};assert(!DetailJson(r,out).decodeObject());}
 DetailWriter deep;deep.u8(6);deep.u16(1);deep.string("deep");for(int i=0;i<17;++i){deep.u8(5);deep.u16(1);}deep.u8(0);DetailReader r{deep.data,deep.size};assert(!DetailJson(r,out).decodeObject());
 DetailWriter big;big.u8(6);big.u16(2);big.string("large");big.u8(4);big.string(std::string(3000,'\n').c_str());big.string("tofFront");big.u8(6);big.u16(1);big.string("range");big.u8(3);big.number(1234);
 size_t total=0;std::string selected;DetailReader large{big.data,big.size};auto count=[&](const char *,size_t n){total+=n;return true;};rejectLargeAllocation=true;
 assert(DetailJsonStream(large,count,&selected,"tofFront").decodeObject());rejectLargeAllocation=false;assert(total>18000&&selected=="{\"range\":1234}");
 DetailReader failed{big.data,big.size};unsigned calls=0;auto reject=[&](const char *,size_t){return ++calls<3;};assert(!DetailJsonStream(failed,reject,&selected,"tofFront").decodeObject());assert(!failed.valid&&selected.empty());
 puts("PASS bounded streaming: 18KB JSON with allocations above 4KB prohibited, member-only extraction, sink failure and full/string output parity");
 puts("PASS detail JSON: real native snapshot round trip and range extraction, all tags, precision, UTF-8/escaping, every truncated prefix, trailing bytes, invalid root/tag/count and excessive nesting");
}
