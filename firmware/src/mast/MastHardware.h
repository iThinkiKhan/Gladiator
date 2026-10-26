#pragma once
#include <Arduino.h>
#include <driver/ledc.h>
#include <soc/gpio_struct.h>
#include "../BoardConfig.h"
#include "../../shared/MastPolicy.h"
namespace mast {
// Existing direct GPIO10/13 wiring. No hardware gate is installed or required.
// PWM is detached on software stops; servo supply/holding torque is not controlled.
class Hardware {
public:
  void begin(){detachOutputs();}
  bool start(const Calibration &c){
    stop();
    ledc_timer_config_t timer={};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_2;
    timer.duty_resolution=LEDC_TIMER_14_BIT;timer.freq_hz=c.frequencyHz;timer.clk_cfg=LEDC_USE_APB_CLK;
    hz_=c.frequencyHz;
    if(ledc_timer_config(&timer)!=ESP_OK)return false;
    for(unsigned i=0;i<2;++i){ledc_channel_config_t ch={};ch.gpio_num=i?c.tiltPin:c.panPin;
      ch.speed_mode=LEDC_LOW_SPEED_MODE;ch.channel=i?LEDC_CHANNEL_5:LEDC_CHANNEL_4;
      ch.timer_sel=LEDC_TIMER_2;ch.duty=duty(i?c.tilt.neutralUs:c.pan.neutralUs);
      if(ledc_channel_config(&ch)!=ESP_OK){stop();return false;}}
    attached_=true;return true;
  }
  bool startTest(uint8_t pin,uint16_t us,uint16_t hz){
    stop();if(pin!=board::MAST_PAN&&pin!=board::MAST_TILT)return false;
    ledc_timer_config_t timer={};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_2;
    timer.duty_resolution=LEDC_TIMER_14_BIT;timer.freq_hz=hz;timer.clk_cfg=LEDC_USE_APB_CLK;hz_=hz;
    if(ledc_timer_config(&timer)!=ESP_OK)return false;
    ledc_channel_config_t ch={};ch.gpio_num=pin;ch.speed_mode=LEDC_LOW_SPEED_MODE;
    ch.channel=pin==board::MAST_PAN?LEDC_CHANNEL_4:LEDC_CHANNEL_5;
    ch.timer_sel=LEDC_TIMER_2;ch.duty=duty(us);
    if(ledc_channel_config(&ch)!=ESP_OK){stop();return false;}
    testChannel_=ch.channel;attached_=true;return true;
  }
  bool writeTest(uint16_t us){return attached_&&ledc_set_duty(LEDC_LOW_SPEED_MODE,testChannel_,duty(us))==ESP_OK&&
    ledc_update_duty(LEDC_LOW_SPEED_MODE,testChannel_)==ESP_OK;}
  void stop(){
    ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_4,0);ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_5,0);
    detachOutputs();attached_=false;}
  bool outputEnabled(uint8_t pin)const{return (GPIO.enable&(uint32_t(1)<<pin))!=0;}
  uint16_t outputSignal(uint8_t pin)const{return GPIO.func_out_sel_cfg[pin].func_sel;}
  uint32_t frequencyHz()const{return attached_?ledc_get_freq(LEDC_LOW_SPEED_MODE,LEDC_TIMER_2):0;}
  bool write(const Controller &c,uint32_t){
    if(!c.enabled){if(attached_)stop();return true;}
    if(!attached_)return false;
    if(ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_4,duty(c.calibration.pan.pulse(c.pan)))!=ESP_OK||
       ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_4)!=ESP_OK||
       ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_5,duty(c.calibration.tilt.pulse(c.tilt)))!=ESP_OK||
       ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_5)!=ESP_OK)return false;
    return true;
  }
private:
  static void detachOutputs(){
    // Disabling the pad alone retains its GPIO-matrix LEDC routing. Clear both
    // routes before selecting a new output, including after a previous test.
    ledcDetachPin(board::MAST_PAN);ledcDetachPin(board::MAST_TILT);
    pinMode(board::MAST_PAN,INPUT_PULLDOWN);pinMode(board::MAST_TILT,INPUT_PULLDOWN);
  }
  bool attached_=false;uint16_t hz_=0;
  ledc_channel_t testChannel_=LEDC_CHANNEL_4;
  uint32_t duty(uint16_t us)const{return uint32_t(uint64_t(us)*hz_*16384/1000000);}
};
}
