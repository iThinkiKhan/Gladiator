#pragma once
#include <cstdint>
using ledc_channel_t=int;
constexpr int ESP_OK=0, LEDC_LOW_SPEED_MODE=0, LEDC_TIMER_2=2, LEDC_TIMER_14_BIT=14, LEDC_USE_APB_CLK=1, LEDC_CHANNEL_4=4, LEDC_CHANNEL_5=5;
struct ledc_timer_config_t {int speed_mode,timer_num,duty_resolution;uint32_t freq_hz;int clk_cfg;};
struct ledc_channel_config_t {int gpio_num,speed_mode,channel,timer_sel;uint32_t duty;};
int ledc_timer_config(const ledc_timer_config_t*);
int ledc_channel_config(const ledc_channel_config_t*);
int ledc_stop(int,int,int);
int ledc_set_duty(int,int,uint32_t);
int ledc_update_duty(int,int);
uint32_t ledc_get_freq(int,int);
