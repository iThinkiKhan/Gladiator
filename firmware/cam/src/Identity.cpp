#include "CamOtaPolicy.h"
extern "C" {
extern const camota::Identity gladiator_cam_identity
  __attribute__((section(".rodata_custom_desc"),used))=camota::IDENTITY;
// Arduino normally confirms an OTA image before setup. Defer until an
// authenticated operator verifies that the new application's HTTP path works.
bool verifyRollbackLater(){return true;}
}
