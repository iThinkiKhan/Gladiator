#pragma once
// Included in the S3 namespace, after control/power/sensor state declarations.
mast::Controller mastController;
mast::PulseTest mastTest;
mast::Hardware mastHardware;
uint32_t mastGeneration=0,mastRejected=0;
uint32_t mastOwner=0;
String mastProfileJson,mastAction="No measured calibration saved";
bool mastCalibrationInhibitsDrive(){return mastTest.enabled&&mastTest.batteryOverride;}
bool mastPowerSampleFresh(){
  const auto &p=sensorData.power.meta;
  return p.hasSample&&p.health==sensors::SensorHealth::OK&&uint32_t(millis()-p.lastUpdateMs)<250;
}
bool mastPowerSafe(){return mastPowerSampleFresh()&&!batteryInhibitsMotion();}
bool mastSafe(){return mastPowerSafe()&&gatewayLink.online();}
bool mastCommissionReady(bool overrideBattery){
  const bool tracksStopped=!commandArmed&&!commandDeadman&&!leftMotor.currentPercent&&!rightMotor.currentPercent&&!targetLeftPercent&&!targetRightPercent;
  return mast::manualTestReady(mastPowerSampleFresh(),batteryInhibitsMotion(),tracksStopped,overrideBattery);
}
bool mastCommissionSafe(){return mastCommissionReady(mastCalibrationInhibitsDrive());}
String mastJson(){
  JsonDocument d;d["schema"]=1;d["s3Ms"]=millis();d["enabled"]=mastController.enabled;
  d["calibrated"]=mastController.calibrated;d["referenced"]=mastController.referenced;
  d["testActive"]=mastTest.enabled;d["testPin"]=mastTest.pin;
  d["testPulseUs"]=mastTest.pulseUs;d["testTargetUs"]=mastTest.targetUs;
  d["testMinUs"]=mastTest.minUs;d["testMaxUs"]=mastTest.maxUs;
  d["testRangeMinUs"]=500;d["testRangeMaxUs"]=2575;d["testJogRequiresPin"]=true;d["testOverrideSupported"]=true;
  d["testBatteryOverride"]=mastCalibrationInhibitsDrive();
  d["testReason"]=mast::name(mastTest.reason);d["testMaxTickMs"]=mastTest.maxTickMs;
  // Hardware registers, not position feedback or a measured electrical waveform.
  d["gpio10OutputEnabled"]=mastHardware.outputEnabled(10);d["gpio13OutputEnabled"]=mastHardware.outputEnabled(13);
  d["gpio10OutputSignal"]=mastHardware.outputSignal(10);d["gpio13OutputSignal"]=mastHardware.outputSignal(13);
  d["pwmFrequencyHz"]=mastHardware.frequencyHz();
  d["owner"]=mastOwner;d["action"]=mastAction;d["speedDegPerSec"]=mastController.calibration.speedDeciDegPerSec/10.0F;
  d["panRatio"]=1.5F;d["panPin"]=mastController.calibration.panPin;d["tiltPin"]=mastController.calibration.tiltPin;
  if(mastProfileJson.length()){JsonDocument profile;if(!deserializeJson(profile,mastProfileJson))d["profile"]=profile.as<JsonVariantConst>();}
  d["scanning"]=mastController.scanning;d["session"]=mastController.session;
  d["awaitingOperator"]=mastController.awaitingOperator;
  d["sequence"]=mastController.sequence;d["reason"]=mast::name(mastController.reason);
  d["rejected"]=mastRejected;d["safe"]=mastSafe();
  // Positions are commanded estimates; SG90 has no position feedback.
  d["commandedPanDeg"]=mastController.pan/10;d["commandedTiltDeg"]=mastController.tilt/10;
  const auto &c=mastController.calibration;
  d["panMinDeg"]=c.pan.minDeciDeg/10.0F;d["panMaxDeg"]=c.pan.maxDeciDeg/10.0F;
  d["tiltMinDeg"]=c.tilt.minDeciDeg/10.0F;d["tiltMaxDeg"]=c.tilt.maxDeciDeg/10.0F;
  String out;serializeJson(d,out);return out;
}
bool mastInteger(JsonVariantConst v,int low,int high,int &out){
  if(!v.is<int>())return false;out=v.as<int>();return out>=low&&out<=high;
}
bool mastAxis(JsonVariantConst obj,mast::Axis &a,int ceiling){
  int lo,neutral,hi,mn,mx;
  if(!mastInteger(obj["lowUs"],500,2575,lo)||!mastInteger(obj["neutralUs"],500,2575,neutral)||
     !mastInteger(obj["highUs"],500,2575,hi)||!mastInteger(obj["minDeciDeg"],-ceiling,0,mn)||
     !mastInteger(obj["maxDeciDeg"],0,ceiling,mx)||!obj["reversed"].is<bool>())return false;
  a.lowUs=lo;a.neutralUs=neutral;a.highUs=hi;a.minDeciDeg=mn;a.maxDeciDeg=mx;
  a.reversed=obj["reversed"].as<bool>();return a.valid(ceiling);
}
#include "MastProfiles.h"
bool mastSerial(const String &line){
  if(!line.startsWith("mast"))return false;
  ControlGuard guard;
  if(line=="mast-status"){Serial.println(mastJson());return true;}
  if(line=="mast-stop"){mastController.stop(mast::Reason::STOPPED);mastTest.stop();mastHardware.stop();Serial.println(mastJson());return true;}
  if(line.startsWith("mast-test ")){
    int pin,first,low,high,hz;char extra;
    bool ok=!mastController.enabled&&sscanf(line.c_str(),"mast-test %d %d %d %d %d %c",&pin,&first,&low,&high,&hz,&extra)==5&&
      mastTest.start(pin,first,low,high,hz,millis(),mastCommissionSafe());
    if(ok&&!mastHardware.startTest(pin,first,hz)){mastTest.stop();mastHardware.stop();ok=false;}
    Serial.println(ok?"USB pulse test active for 10 seconds after each jog; other output stays off.":"USB test rejected: stop first, disarm tracks, restore power data; pin 10/13 and bounded pulse/frequency required.");
    Serial.println(mastJson());return true;
  }
  if(line.startsWith("mast-jog ")){
    int us;char extra;bool ok=sscanf(line.c_str(),"mast-jog %d %c",&us,&extra)==1&&mastTest.jog(us,millis(),mastCommissionSafe());
    Serial.println(ok?"USB pulse target accepted.":"USB jog rejected: active test, safe power/tracks and target inside test window required.");return true;
  }
  if(line.startsWith("mast-config ")){
    Serial.println(saveMastProfile(line.substring(12))?"Measured calibration saved; outputs remain off.":mastAction);return true;
  }
  if(line=="mast-reference CONFIRM_NEUTRAL"){
    if(mastTest.enabled){Serial.println("Stop USB pulse test before normal mast reference.");return true;}
    mastGeneration=gatewayLink.generation();
    if(!mastController.reference(esp_random()|1,millis(),mastSafe())){Serial.println("Mast reference rejected: verify calibration, S3 SAFE, power and link.");return true;}
    if(!mastHardware.start(mastController.calibration)){mastController.stop(mast::Reason::HARDWARE);mastHardware.stop();}
    Serial.println(mastJson());return true;
  }
  Serial.println("Mast USB: mast-status | mast-test PIN FIRST_US MIN_US MAX_US HZ | mast-jog US | mast-stop | mast-config {measured calibration} | mast-reference CONFIRM_NEUTRAL");return true;
}
void serviceMast(){
  ControlGuard guard;
  uint32_t now=millis();bool safe=mastSafe();
  gateway::PendingMastConfig configRequest;
  for(unsigned budget=0;budget<2&&gatewayLink.takeMastConfig(configRequest);++budget){
    if(configRequest.generation!=gatewayLink.generation()||uint32_t(millis()-configRequest.receivedMs)>1000){++mastRejected;continue;}
    mastConfigureAction(configRequest.json);
  }
  now=millis();safe=mastSafe(); // Configuration/test start may timestamp after entry.
  if(mastTest.enabled){
    mastTest.tick(now,mastCommissionSafe());
    if(!mastTest.enabled){mastHardware.stop();mastAction=String("Pulse test stopped: ")+mast::name(mastTest.reason);}
    else if(!mastHardware.writeTest(uint16_t(lroundf(mastTest.pulseUs)))){mastTest.stop(mast::Reason::HARDWARE);mastHardware.stop();mastAction="Pulse test stopped: PWM failure";}
    gateway::PendingMast ignored;for(unsigned budget=0;budget<4&&gatewayLink.takeMast(ignored);++budget){
      if(ignored.command.operation==mast::Operation::STOP){mastTest.stop();mastHardware.stop();}else ++mastRejected;}
    static uint32_t lastTest=0;if(uint32_t(now-lastTest)>=50){lastTest=now;gatewayLink.setMast(mastJson());}
    return;
  }
  const auto &r=sensorData.tofFront;const auto &p=sensorData.presence;
  bool rangeGood=r.meta.hasSample&&r.meta.health==sensors::SensorHealth::OK&&
    uint32_t(now-r.meta.lastUpdateMs)<500&&r.derived.valid;
  bool nearby=(rangeGood&&r.derived.nearestMm<500)||
    (p.meta.hasSample&&p.meta.health==sensors::SensorHealth::OK&&uint32_t(now-p.meta.lastUpdateMs)<800&&p.derived.presenceValid&&p.derived.present);
  // Evaluate expiry/generation BEFORE queued commands can renew a dead session.
  mastController.tick(now,safe,mastGeneration==gatewayLink.generation(),rangeGood,nearby);
  gateway::PendingMast request;
  for(unsigned budget=0;budget<4&&gatewayLink.takeMast(request);++budget){
    const uint32_t commandNow=millis(); // RX task may timestamp after tick's sample.
    if(request.command.operation==mast::Operation::STOP){mastController.stop(mast::Reason::STOPPED);mastTest.stop();mastHardware.stop();mastOwner=0;continue;}
    if(request.command.operation==mast::Operation::ENABLE){
      if(!mastTest.enabled&&request.generation==gatewayLink.generation()&&uint32_t(commandNow-request.receivedMs)<=150&&
        mastController.reference(esp_random()|1,commandNow,safe)){
        mastGeneration=gatewayLink.generation();mastOwner=request.command.session;mastController.awaitingOperator=false;
        if(!mastHardware.start(mastController.calibration)){mastController.stop(mast::Reason::HARDWARE);mastHardware.stop();}
      }else ++mastRejected;
      continue;
    }
    if(request.generation!=mastGeneration||uint32_t(commandNow-request.receivedMs)>150||
       (request.command.operation==mast::Operation::SCAN&&nearby)||
       !mastController.command(request.command,commandNow,safe))++mastRejected;
  }
  if(!mastHardware.write(mastController,now)){mastController.stop(mast::Reason::HARDWARE);mastHardware.stop();}
  static uint32_t last=0;if(uint32_t(now-last)>=50){last=now;gatewayLink.setMast(mastJson());}
}
