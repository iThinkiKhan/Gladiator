#include "../src/mast/MastHardware.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
MockGPIO GPIO;static uint32_t hz;static bool failChannel=false;
// Model the SDK behavior: pin mode and LEDC stop do not clear matrix routing.
void pinMode(uint8_t pin,int){GPIO.enable&=~(uint32_t(1)<<pin);}
void ledcDetachPin(uint8_t pin){GPIO.func_out_sel_cfg[pin].func_sel=256;}
int ledc_timer_config(const ledc_timer_config_t*t){hz=t->freq_hz;return ESP_OK;}
int ledc_channel_config(const ledc_channel_config_t*c){
  GPIO.enable|=uint32_t(1)<<c->gpio_num;GPIO.func_out_sel_cfg[c->gpio_num].func_sel=73+c->channel;
  return failChannel?-1:ESP_OK;
}
int ledc_stop(int,int,int){return ESP_OK;}
int ledc_set_duty(int,int,uint32_t){return ESP_OK;}
int ledc_update_duty(int,int){return ESP_OK;}
uint32_t ledc_get_freq(int,int){return hz;}
void off(const mast::Hardware&h,int pin){assert(!h.outputEnabled(pin));assert(h.outputSignal(pin)==256);}
int main(){
  mast::Hardware h;GPIO.enable=(1<<10)|(1<<13);GPIO.func_out_sel_cfg[10].func_sel=77;GPIO.func_out_sel_cfg[13].func_sel=78;
  h.begin();off(h,10);off(h,13);
  for(int pin:{13,10,13,10}){
    assert(h.startTest(pin,1500,50));int other=pin==10?13:10;
    assert(h.outputEnabled(pin));assert(h.outputSignal(pin)==(pin==10?77:78));off(h,other);
    assert(h.frequencyHz()==50);assert(h.writeTest(1600));
  }
  assert(!h.startTest(12,1500,50));off(h,10);off(h,13);assert(!h.writeTest(1500));
  failChannel=true;assert(!h.startTest(13,1500,50));off(h,10);off(h,13);
  failChannel=false;assert(h.startTest(10,1500,50));h.stop();off(h,10);off(h,13);assert(h.frequencyHz()==0);
  puts("Mast hardware passed: GPIO13/10 handover, matrix detachment, invalid pin and partial attachment failure.");
}
