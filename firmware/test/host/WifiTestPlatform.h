#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <deque>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <cstdint>
// Deterministic fragmented socket pair. Cryptography uses Windows CNG HMAC-SHA256;
// target firmware uses its platform crypto library. WifiChannel itself is real.
inline std::deque<uint8_t> incoming[2];
inline bool blocked=false;
inline bool connectingReady=true;
struct sockaddr {};struct sockaddr_storage {uint8_t bytes[128];};using socklen_t=unsigned;
constexpr int F_SETFL=1,O_NONBLOCK=1,IPPROTO_TCP=6,TCP_NODELAY=1,SOL_SOCKET=1,SO_ERROR=4;
inline int getpeername(int,sockaddr *,socklen_t *){return connectingReady?0:-1;}
inline int getsockopt(int,int,int,void *out,socklen_t *){*static_cast<int *>(out)=0;return 0;}
inline int fcntl(int,int,int){return 0;}
inline int setsockopt(int,int,int,const void *,size_t){return 0;}
inline int close(int){return 0;}
inline int send(int fd,const void *p,size_t size,int){
  if(blocked){errno=EAGAIN;return -1;}size=std::min(size,size_t(7));
  const auto *bytes=static_cast<const uint8_t *>(p);for(size_t i=0;i<size;++i)incoming[1-fd].push_back(bytes[i]);return int(size);
}
inline int recv(int fd,void *p,size_t size,int){
  if(incoming[fd].empty()){errno=EAGAIN;return -1;}size=std::min({size,size_t(5),incoming[fd].size()});
  auto *bytes=static_cast<uint8_t *>(p);for(size_t i=0;i<size;++i){bytes[i]=incoming[fd].front();incoming[fd].pop_front();}return int(size);
}
inline void esp_fill_random(void *p,size_t size){static uint32_t seed=123;auto bytes=static_cast<uint8_t *>(p);for(size_t i=0;i<size;++i){seed=seed*1664525+1013904223;bytes[i]=uint8_t(seed>>24);}}
inline bool testHmac(const uint8_t *key,size_t keySize,const uint8_t *a,size_t aSize,const uint8_t *b,size_t bSize,uint8_t *out){
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  NTSTATUS result=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if(result>=0)result=BCryptCreateHash(algorithm,&hash,nullptr,0,const_cast<PUCHAR>(key),ULONG(keySize),0);
  if(result>=0&&aSize)result=BCryptHashData(hash,const_cast<PUCHAR>(a),ULONG(aSize),0);
  if(result>=0&&bSize)result=BCryptHashData(hash,const_cast<PUCHAR>(b),ULONG(bSize),0);
  if(result>=0)result=BCryptFinishHash(hash,out,32,0);
  if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return result>=0;
}
