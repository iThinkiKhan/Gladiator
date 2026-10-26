#pragma once
bool parseMastProfile(const String &json,mast::Calibration &c){
  JsonDocument d;int hz,speed,pin,tin;
  if(deserializeJson(d,json)||!d["positionalVerified"].is<bool>()||!d["positionalVerified"].as<bool>()||
     !d["clearanceVerified"].is<bool>()||!d["clearanceVerified"].as<bool>()||
     !d["panModel"].is<const char *>()||!strlen(d["panModel"].as<const char *>())||
     !d["tiltModel"].is<const char *>()||!strlen(d["tiltModel"].as<const char *>())||
     !mastInteger(d["frequencyHz"],50,100,hz)||!mastInteger(d["speedDeciDegPerSec"],1,300,speed)||
     !mastInteger(d["pan"]["pin"],10,13,pin)||!mastInteger(d["tilt"]["pin"],10,13,tin))return false;
  // Legacy USB head-angle pulse maps remain supported. Operator profiles use
  // measured SHAFT angles and apply the installed 60T/40T = 1.5 pan ratio here.
  for(unsigned i=0;i<2;++i){JsonVariantConst obj=d[i?"tilt":"pan"];auto &a=i?c.tilt:c.pan;int lo,n,hi,mn,mx,smn=0,smx=0;
    if(!mastInteger(obj["lowUs"],500,2575,lo)||!mastInteger(obj["neutralUs"],500,2575,n)||!mastInteger(obj["highUs"],500,2575,hi)||
       !mastInteger(obj["minDeciDeg"],i?-200:-450,0,mn)||!mastInteger(obj["maxDeciDeg"],0,i?200:450,mx)||!obj["reversed"].is<bool>())return false;
    if(obj["shaftMinDeciDeg"].is<int>()||obj["shaftMaxDeciDeg"].is<int>()){
      if(!mastInteger(obj["shaftMinDeciDeg"],-1800,0,smn)||!mastInteger(obj["shaftMaxDeciDeg"],0,1800,smx))return false;
    }
    if(d["action"]=="calibrate"&&!(smn||smx))return false;
    a.lowUs=lo;a.neutralUs=n;a.highUs=hi;a.minDeciDeg=mn;a.maxDeciDeg=mx;a.reversed=obj["reversed"].as<bool>();
    a.shaftMinDeciDeg=smn;a.shaftMaxDeciDeg=smx;a.gearRatio=i?1.0F:1.5F;
  }
  c.panPin=pin;c.tiltPin=tin;c.frequencyHz=hz;c.speedDeciDegPerSec=speed;c.modelsVerified=c.clearanceVerified=true;
  return c.valid();
}
void loadMastProfile(){
  Preferences p;if(!p.begin("mast-v1",true))return;
  String json=p.getString("profile","");p.end();mast::Calibration c;
  if(json.length()<=768&&parseMastProfile(json,c)&&mastController.configure(c)){mastProfileJson=json;mastAction="Calibration loaded; motion off";}
}
bool saveMastProfile(const String &json){
  mast::Calibration c;
  if(mastController.enabled||mastTest.enabled||!mastCommissionSafe()){mastAction="Stop mast/test and disarm drive before saving calibration";return false;}
  if(json.length()>768||!parseMastProfile(json,c)){mastAction="Invalid measured pulse/shaft/limit/pin profile";return false;}
  Preferences p;if(!p.begin("mast-v1",false)){mastAction="Calibration storage unavailable";return false;}
  bool saved=p.putString("profile",json)==json.length();p.end();
  if(!saved){mastAction="Calibration storage failed";return false;}
  mastController.configure(c);mastProfileJson=json;mastAction="Calibration saved; enable mast deliberately";return true;
}
void mastConfigureAction(const char *json){
  JsonDocument d;if(deserializeJson(d,json)){++mastRejected;mastAction="Invalid commissioning request";return;}
  const String action=d["action"]|"";
  if(action=="calibrate"){if(!saveMastProfile(String(json)))++mastRejected;return;}
  if(action=="stop"){mastController.stop(mast::Reason::STOPPED);mastTest.stop();mastHardware.stop();mastOwner=0;mastAction="Outputs stopped";return;}
  if(action=="test"){
    const bool overrideBattery=d["overrideBatteryLockout"].is<bool>()&&d["overrideBatteryLockout"].as<bool>();
    const bool validOverride=d["overrideBatteryLockout"].isNull()||d["overrideBatteryLockout"].is<bool>();
    int pin,first,lo,hi,hz;bool ok=validOverride&&!mastController.enabled&&mastInteger(d["pin"],10,13,pin)&&
      mastInteger(d["firstUs"],500,2575,first)&&mastInteger(d["minUs"],500,2575,lo)&&mastInteger(d["maxUs"],500,2575,hi)&&mastInteger(d["frequencyHz"],50,100,hz)&&
      mastTest.start(pin,first,lo,hi,hz,millis(),mastCommissionReady(overrideBattery),overrideBattery);
    if(ok&&!mastHardware.startTest(pin,first,hz)){mastTest.stop();mastHardware.stop();ok=false;}
    mastAction=ok?(overrideBattery?"Manual servo calibration: battery override active; tracks locked out":"Single output test started; other output off"):"Test rejected: stop outputs, use GPIO10/13, 500-2575us and 50-100Hz";
    if(!ok)++mastRejected;return;
  }
  if(action=="jog-test"){
    int pin,us;bool ok=mastInteger(d["pin"],10,13,pin)&&(pin==10||pin==13)&&
      mastInteger(d["pulseUs"],500,2575,us)&&mastTest.jogPin(pin,us,millis(),mastCommissionSafe());
    mastAction=ok?"Test target accepted":"Test GPIO mismatch, target outside range, expired or inhibited";if(!ok)++mastRejected;return;
  }
  ++mastRejected;mastAction="Unknown commissioning action";
}
