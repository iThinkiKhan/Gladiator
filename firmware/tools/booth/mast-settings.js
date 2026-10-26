"use strict";
(function(root){
 const axes=['pan','tilt'];
 const fields={Pin:'pin',Low:'lowUs',Neutral:'neutralUs',High:'highUs',ShaftMin:'shaftMinDeciDeg',ShaftMax:'shaftMaxDeciDeg',Min:'minDeciDeg',Max:'maxDeciDeg',Reverse:'reversed'};
 const angleKeys=new Set(['ShaftMin','ShaftMax','Min','Max']);
 const labels={Pin:'GPIO',Low:'low pulse',Neutral:'neutral pulse',High:'high pulse',ShaftMin:'negative shaft angle',ShaftMax:'positive shaft angle',Min:'negative head limit',Max:'positive head limit'};
 function validateProfile(value){
  if(!value||!Number.isInteger(value.frequencyHz)||value.frequencyHz<50||value.frequencyHz>100||!Number.isInteger(value.speedDeciDegPerSec)||value.speedDeciDegPerSec<1||value.speedDeciDegPerSec>300)throw Error('Invalid aiming frequency or speed.');
  const p={action:'calibrate',panModel:value.panModel||'SG90',tiltModel:value.tiltModel||'SG90',positionalVerified:true,clearanceVerified:true,frequencyHz:value.frequencyHz,speedDeciDegPerSec:value.speedDeciDegPerSec};
  for(const axis of axes){const a=value[axis],limit=axis==='pan'?450:200,ratio=axis==='pan'?1.5:1;
   if(!a||![10,13].includes(a.pin)||typeof a.reversed!=='boolean')throw Error(axis+': invalid GPIO or direction.');
   const b={};for(const key of ['pin','lowUs','neutralUs','highUs','shaftMinDeciDeg','shaftMaxDeciDeg','minDeciDeg','maxDeciDeg','reversed'])b[key]=a[key]??(key.startsWith('shaft')?0:undefined);
   if(Object.entries(b).some(([k,v])=>k!=='reversed'&&!Number.isInteger(v)))throw Error(axis+': angles must be in tenths of a degree and pulses in whole microseconds.');
   if(b.lowUs<500||b.highUs>2575||b.lowUs>=b.highUs||b.neutralUs<b.lowUs||b.neutralUs>b.highUs)throw Error(axis+': low / neutral / high pulses do not agree.');
   const neg=b.reversed?b.highUs:b.lowUs,pos=b.reversed?b.lowUs:b.highUs,sm=b.shaftMinDeciDeg,sx=b.shaftMaxDeciDeg,mn=b.minDeciDeg,mx=b.maxDeciDeg,shaft=sm!==0||sx!==0;
   if(mn>0||mx<0||mn>=mx||mn< -limit||mx>limit)throw Error(axis+': invalid head travel limits.');
   if(shaft){if(sm>0||sx<0||sm>=sx||sm< -1800||sx>1800||mn<sm*ratio||mx>sx*ratio||(sm===0?neg!==b.neutralUs:neg===b.neutralUs)||(sx===0?pos!==b.neutralUs:pos===b.neutralUs))throw Error(axis+': measured shaft travel, direction, neutral and head limits do not agree.');}
   else if((mn===0?neg!==b.neutralUs:neg===b.neutralUs)||(mx===0?pos!==b.neutralUs:pos===b.neutralUs))throw Error(axis+': head limits do not agree with neutral.');
   p[axis]=b;
  }
  if(p.pan.pin===p.tilt.pin)throw Error('Pan and tilt must use different GPIOs.');return p;
 }
 function savedProfile(record){
  const missing=[],errors=[],f=record?.fields||{};const p={};
  const number=(key,scale=1)=>{const v=f[key];if(v===undefined||v===null||String(v).trim()===''){missing.push(key);return NaN;}const n=Number(v)*scale;if(!Number.isFinite(n)||Math.abs(n-Math.round(n))>1e-6){errors.push(key+': enter '+(scale===10?'tenths of a degree':'a whole number'));return NaN;}return Math.round(n);};
  p.frequencyHz=number('hz');p.speedDeciDegPerSec=number('speed',10);
  for(const axis of axes){p[axis]={};for(const [key,property]of Object.entries(fields)){if(key==='Reverse'){if(f[axis+key]!==undefined&&typeof f[axis+key]!=='boolean')errors.push(axis+' direction must be true or false.');p[axis][property]=f[axis+key]??false;}else p[axis][property]=number(axis+key,angleKeys.has(key)?10:1);}}
  let profile=null;if(!missing.length&&!errors.length){try{profile=validateProfile(p);for(const axis of axes)if(!profile[axis].shaftMinDeciDeg&&!profile[axis].shaftMaxDeciDeg)throw Error(axis+': measured shaft travel is required.');}catch(e){profile=null;errors.push(e.message);}}
  return {profile,missing,errors,missingText:missing.map(k=>{const axis=k.startsWith('pan')?'pan':k.startsWith('tilt')?'tilt':'';return axis?axis+' '+(labels[k.slice(axis.length)]||k):({hz:'PWM frequency',speed:'aiming speed'}[k]||k);})};
 }
 function canonical(p){return JSON.stringify(validateProfile(p));}
 function sameProfile(a,b){try{return canonical(a)===canonical(b);}catch{return false;}}
 function signature(p){const c=validateProfile(p);delete c.speedDeciDegPerSec;return JSON.stringify(c);}
 function entryMatches(record,p){try{p=validateProfile(p);const f=record.fields;for(const axis of axes)for(const [key,property]of Object.entries(fields)){const v=f[axis+key];if(v===undefined||v==='')continue;if((key==='Reverse'?v:Number(v)*(angleKeys.has(key)?10:1))!==p[axis][property])return false;}for(const [k,target,scale]of [['hz','frequencyHz',1],['speed','speedDeciDegPerSec',10]])if(f[k]!==undefined&&f[k]!==''&&Math.abs(Number(f[k])*scale-p[target])>1e-6)return false;return true;}catch{return false;}}
 function recordKey(r){return JSON.stringify([r?.revision,r?.fields]);}
 function idle(field,m){const c=field?.controller;if(!field?.connected||!field.powerValid||!c||field.telemetryAgeMs>=250||!Number.isFinite(field.telemetryAgeMs)||c.batteryLockout||c.state!=='SAFE'||c.armed||c.deadman||c.leftPercent||c.rightPercent)throw Error('Wait for fresh power and disarmed, stationary tracks.');if(!m?.fresh||!m.safe||m.enabled||m.testActive)throw Error('Stop the active mast or calibration test first.');}
 class Installer{
  constructor(api,clock=()=>performance.now(),sleep=ms=>new Promise(r=>setTimeout(r,ms))){this.api=api;this.clock=clock;this.sleep=sleep;this.generation=0;this.busy=false;}
  cancel(){++this.generation;}
  async apply({verified=false,expectedRecord}={}){
   if(this.busy)throw Error('Already applying a profile.');if(!verified)throw Error('Confirm that the measured travel and clearance were checked.');
   const gen=++this.generation;const guard=()=>{if(gen!==this.generation)throw Error('Profile operation cancelled.');};this.busy=true;
   try{
    const record=await this.api('/api/mast/setup');guard();if(expectedRecord&&recordKey(expectedRecord)!==recordKey(record))throw Error('Saved measurements changed. Refresh and review before applying.');
    const built=savedProfile(record);if(!built.profile)throw Error(built.errors.join('; ')||'Still needed: '+built.missingText.join(', '));
    const [field,m]=await Promise.all([this.api('/api/field'),this.api('/api/mast')]);guard();idle(field,m);
    const again=await this.api('/api/mast/setup');guard();if(recordKey(again)!==recordKey(record))throw Error('Saved measurements changed during review. Refresh and review again.');
    await this.api('/api/mast/config',built.profile);guard();const start=this.clock();
    while(this.clock()-start<6500){await this.sleep(150);guard();const current=await this.api('/api/mast');guard();
     if(!current.fresh)continue;if(current.enabled||current.testActive)throw Error('Another output became active; profile save is not confirmed.');
     if(current.rejected>m.rejected)throw Error('S3 rejected the profile: '+(current.action||current.reason||'check robot status'));
     if(current.calibrated&&current.profile&&sameProfile(current.profile,built.profile)){
      const [latest,status]=await Promise.all([this.api('/api/mast/setup'),this.api('/api/field')]);guard();
      if(recordKey(latest)!==recordKey(record))throw Error('New measurements were saved during apply. Review them before applying again.');
      if(status.controller?.epoch!==field.controller.epoch)throw Error('S3 restarted during apply; refresh and verify its saved profile.');
      return {record:latest,profile:built.profile};
     }
    }
    throw Error('S3 profile readback was not confirmed. Refresh before trying again.');
   }finally{this.busy=false;}
  }
 }
 function poseInRange(pose,profile){try{const p=validateProfile(profile);return ['pan','tilt'].every(axis=>Number.isFinite(pose[axis])&&Math.abs(pose[axis]*10-Math.round(pose[axis]*10))<1e-6&&pose[axis]*10>=p[axis].minDeciDeg&&pose[axis]*10<=p[axis].maxDeciDeg);}catch{return false;}}
 function poseFromState(name,m){const label=String(name).trim();if(!label||label.length>40)throw Error('Use a pose name with 1–40 characters.');if(!m?.fresh||!m.calibrated||!m.profile||m.testActive||!Number.isFinite(m.commandedPanDeg)||!Number.isFinite(m.commandedTiltDeg))throw Error('Wait for fresh calibrated mast status and commanded angles.');const pose={name:label,pan:Math.round(m.commandedPanDeg*10)/10,tilt:Math.round(m.commandedTiltDeg*10)/10,signature:signature(m.profile)};if(!poseInRange(pose,m.profile))throw Error('Reported commanded pose is outside the saved limits.');return pose;}
 function poseStep(pose,m,ms=2000){if(!m?.fresh||!m.calibrated||!m.profile)throw Error('Wait for fresh applied mast calibration.');if(pose.signature!==signature(m.profile))throw Error('This pose uses a different calibration. Review it and save it again.');if(!poseInRange(pose,m.profile))throw Error('Pose exceeds the current saved limits.');return {type:'aim',ms,pan:pose.pan,tilt:pose.tilt};}
 const api={validateProfile,savedProfile,sameProfile,signature,entryMatches,recordKey,Installer,poseInRange,poseFromState,poseStep};
 if(typeof module!=='undefined')module.exports=api;else root.MastSettings=api;
})(typeof globalThis!=='undefined'?globalThis:this);
