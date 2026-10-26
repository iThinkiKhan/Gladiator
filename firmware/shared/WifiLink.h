#pragma once
#include "GladiatorLink.h"
#ifdef GLADIATOR_WIFI_HOST_TEST
#include "WifiTestPlatform.h"
#else
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR >= 6
#include <psa/crypto.h>
#else
#include <mbedtls/md.h>
#endif
#endif

// Override identically in both builds for a particular robot. This development
// key is separate from HTTP credentials and is never returned by an API.
#ifndef GLADIATOR_LINK_KEY
#define GLADIATOR_LINK_KEY "replace-with-unique-shared-link-key"
#endif
namespace robotlink {
inline bool hmac(const uint8_t *key,size_t keySize,const uint8_t *a,size_t aSize,const uint8_t *b,size_t bSize,uint8_t *out){
#ifdef GLADIATOR_WIFI_HOST_TEST
  return testHmac(key,keySize,a,aSize,b,bSize,out);
#elif ESP_IDF_VERSION_MAJOR >= 6
  if(psa_crypto_init()!=PSA_SUCCESS)return false;
  psa_key_attributes_t attr=PSA_KEY_ATTRIBUTES_INIT;psa_set_key_type(&attr,PSA_KEY_TYPE_HMAC);
  psa_set_key_usage_flags(&attr,PSA_KEY_USAGE_SIGN_MESSAGE);psa_set_key_algorithm(&attr,PSA_ALG_HMAC(PSA_ALG_SHA_256));
  mbedtls_svc_key_id_t id=0;psa_status_t status=psa_import_key(&attr,key,keySize,&id);psa_reset_key_attributes(&attr);
  psa_mac_operation_t op=PSA_MAC_OPERATION_INIT;size_t written=0;
  if(status==PSA_SUCCESS)status=psa_mac_sign_setup(&op,id,PSA_ALG_HMAC(PSA_ALG_SHA_256));
  if(status==PSA_SUCCESS&&aSize)status=psa_mac_update(&op,a,aSize);
  if(status==PSA_SUCCESS&&bSize)status=psa_mac_update(&op,b,bSize);
  if(status==PSA_SUCCESS)status=psa_mac_sign_finish(&op,out,32,&written);
  psa_mac_abort(&op);psa_destroy_key(id);return status==PSA_SUCCESS&&written==32;
#else
  mbedtls_md_context_t ctx;mbedtls_md_init(&ctx);
  int status=mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);
  if(!status)status=mbedtls_md_hmac_starts(&ctx,key,keySize);
  if(!status&&aSize)status=mbedtls_md_hmac_update(&ctx,a,aSize);
  if(!status&&bSize)status=mbedtls_md_hmac_update(&ctx,b,bSize);
  if(!status)status=mbedtls_md_hmac_finish(&ctx,out);
  mbedtls_md_free(&ctx);return status==0;
#endif
}
// Nonblocking, bounded Wi-Fi adapter. Fresh 128-bit nonces on EVERY TCP connection
// derive the HMAC key; directional counters reject replay. CRC framing is unchanged.
// WPA2 supplies privacy; HMAC authenticates the coprocessor independently of phones.
class WifiChannel {
public:
  int fd=-1;uint32_t errors=0,drops=0;bool ready=false;
  void close(){if(fd>=0)::close(fd);fd=-1;ready=false;rxUsed_=txSize_=txAt_=0;}
  void attach(int socket,bool server,uint32_t now){
    close();fd=socket;server_=server;connecting_=!server;fcntl(fd,F_SETFL,O_NONBLOCK);
    int one=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
    esp_fill_random(local_,sizeof(local_));memcpy(tx_,local_,16);txSize_=16;txAt_=0;
    rxUsed_=0;rxExpected_=16;sent_=received_=0;started_=pendingSince_=now;
  }
  bool send(const uint8_t *frame,size_t n,uint32_t now){
    if(!ready||n>MAX_FRAME||txSize_+n+38>sizeof(tx_)){++drops;return false;}
    uint8_t *out=tx_+txSize_;out[0]=uint8_t(n);out[1]=uint8_t(n>>8);write32(out+2,++sent_);
    memcpy(out+6,frame,n);if(!mac(out,n+6,out+n+6,server_?1:0)){++errors;close();return false;}
    if(!txSize_)pendingSince_=now;
    txSize_+=n+38;return true;
  }
  template<class Receive> void poll(uint32_t now,Receive receive){
    if(fd<0)return;
    if((!ready&&uint32_t(now-started_)>2000)||(!connecting_&&txSize_&&uint32_t(now-pendingSince_)>500)){++errors;close();return;}
    if(connecting_){
      sockaddr_storage peer={};socklen_t length=sizeof(peer);
      if(getpeername(fd,reinterpret_cast<sockaddr *>(&peer),&length)!=0){
        int error=0;socklen_t size=sizeof(error);
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)!=0||(error&&error!=EINPROGRESS&&error!=EALREADY)){++errors;close();}
        return;
      }
      connecting_=false;pendingSince_=now;
    }
    if(txSize_){int n=::send(fd,tx_+txAt_,txSize_-txAt_,0);if(n>0){txAt_+=n;if(txAt_==txSize_)txSize_=txAt_=0;}else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINPROGRESS){++errors;close();return;}}
    for(unsigned budget=0;budget<8&&fd>=0;++budget){
      int n=recv(fd,rx_+rxUsed_,rxExpected_-rxUsed_,0);
      if(n==0){close();return;}if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK){++errors;close();}return;}
      rxUsed_+=n;if(rxUsed_!=rxExpected_)continue;
      if(!ready){
        uint8_t seed[32];memcpy(seed,server_?local_:rx_,16);memcpy(seed+16,server_?rx_:local_,16);
        const char *key=GLADIATOR_LINK_KEY;
        if(!hmac(reinterpret_cast<const uint8_t *>(key),strlen(key),seed,32,nullptr,0,key_)){++errors;close();return;}
        ready=true;rxUsed_=0;rxExpected_=6;continue;
      }
      if(rxExpected_==6){size_t length=size_t(rx_[0])|(size_t(rx_[1])<<8);if(!length||length>MAX_FRAME){++errors;close();return;}rxExpected_=length+38;continue;}
      uint8_t digest[32];if(!mac(rx_,rxExpected_-32,digest,server_?0:1)){++errors;close();return;}uint8_t diff=0;
      for(unsigned i=0;i<32;++i)diff|=digest[i]^rx_[rxExpected_-32+i];
      if(diff||read32(rx_+2)!=received_+1){++errors;close();return;}
      ++received_;receive(rx_+6,rxExpected_-38);rxUsed_=0;rxExpected_=6;
    }
  }
private:
  bool server_=false,connecting_=false;uint8_t local_[16],key_[32];
  uint8_t rx_[MAX_FRAME+38],tx_[MAX_FRAME+38];
  size_t rxUsed_=0,rxExpected_=16,txSize_=0,txAt_=0;
  uint32_t started_=0,pendingSince_=0,sent_=0,received_=0;
  bool mac(const uint8_t *data,size_t n,uint8_t *out,uint8_t direction){
    return hmac(key_,32,&direction,1,data,n,out);
  }
};
}
