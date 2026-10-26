#include <cassert>
#include <cstdio>
#include "../shared/JsonEscape.h"
int main(){
 std::string out;size_t chunks=0;
 auto sink=[&](const char *data,size_t count){assert(count<=512);out.append(data,count);++chunks;return true;};
 assert(jsonstream::writeString(std::string("line\n\"quoted\"\\end\t")+std::string(1,'\0')+u8" / café",sink));
 assert(out==u8"\"line\\u000a\\\"quoted\\\"\\\\end\\u0009\\u0000 / café\"");
 out.clear();chunks=0;assert(jsonstream::writeString(std::string(12000,'x'),sink));assert(out.size()==12002&&out.front()=='"'&&out.back()=='"'&&chunks==24);
 size_t calls=0;assert(!jsonstream::writeString(std::string(12000,'x'),[&](const char*,size_t){return ++calls<3;}));assert(calls==3);
 puts("PASS JSON stream: UTF-8, quotes, backslash, control bytes, large logs, bounded chunks, delivery failure");
}
