#pragma once
#include <cstdint>
struct MockGPIO { uint32_t enable=0;struct {uint16_t func_sel=256;} func_out_sel_cfg[49];};
extern MockGPIO GPIO;
