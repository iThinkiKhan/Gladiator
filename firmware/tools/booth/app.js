"use strict";
const faceModes=['auto','ready','scanning','active','greeting','alert'];
const usesTracks=s=>s.type==='drive'||s.type==='cue';
const usesMast=s=>s.type==='aim'||s.type==='cue';
function validateRoutine(value){
 if(!Array.isArray(value)||!value.length||value.length>40)throw Error('Use 1–40 steps.');
 let total=0,drive=0;const allowed={wait:['type','ms'],face:['type','ms','mode'],drive:['type','ms','left','right'],aim:['type','ms','pan','tilt'],cue:['type','ms','left','right','pan','tilt','mode']};
 const result=value.map((raw,i)=>{
  const bad=message=>{throw Error('Step '+(i+1)+': '+message);};
  if(!raw||typeof raw!=='object'||Array.isArray(raw)||!Object.hasOwn(allowed,raw.type))bad('choose wait, expression, tracks, mast or together.');
  if(Object.keys(raw).some(k=>!allowed[raw.type].includes(k)))bad('unknown field.');
  const s={...raw};if(!Number.isInteger(s.ms)||s.ms<100||s.ms>15000)bad('duration must be 100–15000 ms.');total+=s.ms;
  if(usesTracks(s)){if(s.ms>1500)bad('track cue maximum is 1500 ms.');drive+=s.ms;for(const k of ['left','right'])if(!Number.isInteger(s[k])||Math.abs(s[k])>25)bad(k+' must be -25 to 25 percent.');}
  if(usesMast(s)){for(const [k,bound] of [['pan',45],['tilt',20]])if(!Number.isFinite(s[k])||Math.abs(s[k])>bound||Math.abs(s[k]*10-Math.round(s[k]*10))>1e-6)bad(k+' requires tenths of head degrees within +/-'+bound);}
  if((s.type==='face'||Object.hasOwn(s,'mode'))&&!faceModes.includes(s.mode))bad('invalid expression.');
  return s;
 });if(total>90000||drive>5000)throw Error('Per cycle maximum: 90 seconds; track time: 5 seconds.');return result;
}
function validatePlayback({repeats=1,gapMs=5000}={}){
 if(!Number.isInteger(repeats)||repeats<0||repeats>100)throw Error('Repeat count must be 1–100, or choose Until STOP.');
 if(!Number.isInteger(gapMs)||gapMs<1000||gapMs>60000)throw Error('Cycle pause must be 1–60 seconds.');
 return {repeats,gapMs};
}
class BoothRunner {
 constructor(api,clock=()=>performance.now(),pause=ms=>new Promise(r=>setTimeout(r,ms))){this.api=api;this.clock=clock;this.pause=pause;this.generation=0;this.running=false;this.client=crypto.getRandomValues(new Uint32Array(1))[0]||1;this.seq=0;this.mastSeq=0;this.owned=false;this.mastOwned=false;}
 guard(g){if(g!==this.generation||!this.running)throw Error('Stopped.');}
 async post(path,body,g){this.guard(g);const result=await this.api(path,body);this.guard(g);return result;}
 async read(g){this.guard(g);const s=await this.api('/api/field');this.guard(g);const c=s.controller;
  if(!s.connected||!c||s.telemetryAgeMs==null||s.telemetryAgeMs>=250||!s.powerValid||c.batteryLockout||!['SAFE','ARMED','DRIVE'].includes(c.state))throw Error('S3 or power unavailable.');
  if(this.epoch!==undefined&&c.epoch!==this.epoch)throw Error('Robot session changed.');
  if(this.owned&&(!c.armed||c.client!==this.client))throw Error('Track control ownership ended.');
  if(s.lastResult?.client===this.client&&s.lastResult.sequence>this.resultFloor&&s.lastResult.result)throw Error('S3 rejected track command: '+s.lastResult.result);
  this.snapshot=s;this.onSnapshot?.(s);return s;
 }
 driveBody(op,left=0,right=0){const c=this.snapshot.controller;return {epoch:c.epoch,client:this.client,sequence:++this.seq,observedMs:c.s3Ms,operation:op,deadman:op===2&&(left!==0||right!==0)?1:0,overdrive:0,power:25,left,right};}
 async track(op,left,right,g){await this.post('/api/control',this.driveBody(op,left,right),g);}
 async mastPost(op,pan,tilt,g){await this.post('/api/mast',{session:op===7?this.client:this.mastState.session,sequence:++this.mastSeq,operation:op,pan:Math.round(pan*10),tilt:Math.round(tilt*10)},g);}
 async readMast(g,confirm=false,fromField=false){
  // Field telemetry already contains the same mast state. Avoid another radio
  // round trip during every coordinated heartbeat, retaining both age checks.
  let m=fromField?this.snapshot?.sensors?.mast:null;
  if(m)m={...m,fresh:this.snapshot.telemetryAgeMs<250&&Number.isFinite(m.sourceAgeMs)&&m.sourceAgeMs>=0&&m.sourceAgeMs<250&&Number.isFinite(m.transportAgeMs)&&m.transportAgeMs>=0&&m.transportAgeMs<250};
  else m=await this.api('/api/mast');this.guard(g);
  if(!m.fresh||!m.calibrated||m.testActive||!m.safe)throw Error('Mast calibration, power or telemetry unavailable.');
  if(!['panMinDeg','panMaxDeg','tiltMinDeg','tiltMaxDeg'].every(k=>Number.isFinite(m[k]))||m.panMinDeg>m.panMaxDeg||m.tiltMinDeg>m.tiltMaxDeg)throw Error('Mast limits unavailable.');
  if(confirm&&(!m.enabled||m.owner!==this.client))return false;
  if(this.mastOwned&&(!m.enabled||m.owner!==this.client||m.session!==this.mastState.session||m.rejected!==this.mastState.rejected))throw Error('Mast session ended or command rejected.');
  this.mastState=m;return true;
 }
 async stop(message='Stopped.',all=false){++this.generation;this.running=false;this.onState?.();
  const stopTracks=all||this.owned||this.trackPending,stopMast=all||this.mastOwned||this.mastPending;
  this.owned=this.mastOwned=this.trackPending=this.mastPending=false;
  const c=this.snapshot?.controller;const requests=[];
  if(c&&stopTracks)requests.push(this.api('/api/control',this.driveBody(0)));
  if(stopMast)requests.push(this.api('/api/mast',{session:1,sequence:++this.mastSeq,operation:0,pan:0,tilt:0}));
  const results=await Promise.allSettled(requests);this.onStatus?.(message+(results.some(r=>r.status==='rejected')?' Stop delivery unconfirmed; check robot.':''));
 }
 async hold(step,g){
  const start=this.clock();let first=true;
  while(first||this.clock()-start<step.ms){
   this.guard(g);if(this.clock()-this.lastCycle>250)throw Error('Control loop delayed; stopped.');this.lastCycle=this.clock();
   await this.read(g);if(this.clock()-this.lastCycle>250)throw Error('Control request delayed; stopped.');
   if(this.mastOwned)await this.readMast(g,false,true);
   if(this.clock()-this.lastCycle>250)throw Error('Control request delayed; stopped.');
   const active=this.clock()-start<step.ms;
   if(first&&!active&&(usesTracks(step)||usesMast(step)))throw Error('Step expired before its movement could start.');
   const requests=[];
   // Submit the first track and mast targets from the same fresh snapshot.
   // They share a cue, not a promise of hard real-time synchronization.
   if(this.owned)requests.push(this.track(2,active&&usesTracks(step)?step.left:0,active&&usesTracks(step)?step.right:0,g));
   if(this.mastOwned)requests.push(first&&usesMast(step)?this.mastPost(2,step.pan,step.tilt,g):this.mastPost(4,0,0,g));
   if(first&&(step.type==='face'||Object.hasOwn(step,'mode')))requests.push(this.post('/api/visual',step.mode==='auto'?{mode:'auto'}:{mode:step.mode,leaseMs:Math.max(1000,step.ms),message:'Event booth'},g));
   await Promise.all(requests);first=false;await this.pause(40);
  }
  if(this.owned){await this.read(g);await this.track(2,0,0,g);}
 }
 async run(raw,{motion=false,mast=false,repeats=1,gapMs=5000}={}){
  if(this.running)throw Error('Already running.');const steps=validateRoutine(raw),tracks=steps.some(usesTracks),aim=steps.some(usesMast);validatePlayback({repeats,gapMs});
  if((tracks||aim)&&!motion)throw Error('Confirm the supervised rehearsal first.');if(aim&&!mast)throw Error('Select calibrated mast movement first.');
  this.running=true;this.onState?.();const g=++this.generation;this.epoch=undefined;this.resultFloor=this.seq;this.owned=this.mastOwned=this.trackPending=this.mastPending=false;this.cycle=0;
  try {
   let s=await this.read(g);this.epoch=s.controller.epoch;
   if(s.controller.armed||s.controller.deadman||s.controller.leftPercent||s.controller.rightPercent)throw Error('Disarm and stop existing controls first.');
   if(aim){await this.readMast(g);if(this.mastState.enabled)throw Error('Turn mast OFF in other controls first.');
    for(const step of steps.filter(usesMast))if(step.pan<this.mastState.panMinDeg||step.pan>this.mastState.panMaxDeg||step.tilt<this.mastState.tiltMinDeg||step.tilt>this.mastState.tiltMaxDeg)throw Error('Aim exceeds the robot calibrated limits.');
    this.mastPending=true;await this.mastPost(7,0,0,g);let accepted=false;
    for(let i=0;i<5;++i){await this.pause(70);this.guard(g);if(await this.readMast(g,true)){accepted=true;break;}}
    if(!accepted)throw Error('Mast enable not confirmed.');this.mastOwned=true;
   }
   if(tracks){await this.read(g);this.trackPending=true;await this.track(1,0,0,g);let accepted=false;
    for(let i=0;i<5;++i){await this.pause(40);s=await this.read(g);if(s.controller.armed&&s.controller.client===this.client){accepted=true;break;}}
    if(!accepted)throw Error('Track arm not confirmed.');this.owned=true;
   }
   this.lastCycle=this.clock();
   while(!repeats||this.cycle<repeats){
    ++this.cycle;
    for(let i=0;i<steps.length;++i){this.guard(g);this.onStatus?.('Cycle '+this.cycle+(repeats?' / '+repeats:' / until STOP')+' — Step '+(i+1)+' / '+steps.length+' — '+steps[i].type);await this.hold(steps[i],g);}
    if(repeats&&this.cycle>=repeats)break;
    this.onStatus?.('Cycle '+this.cycle+' complete — pause '+(gapMs/1000)+' s; tracks at zero, mast holds its target.');
    await this.hold({type:'wait',ms:gapMs},g);
   }
   this.guard(g);await this.stop('Routine complete after '+this.cycle+' cycle'+(this.cycle===1?'':'s')+'. Outputs OFF requested; check S3 status.');
  }catch(e){if(g===this.generation)await this.stop('Routine ended: '+e.message);else throw e;}
 }
}
if(typeof module!=='undefined')module.exports={validateRoutine,validatePlayback,BoothRunner};
if(typeof document!=='undefined'){
 const $=id=>document.getElementById(id),key='gladiator-booth-routine-v1',playKey='gladiator-booth-playback-v1';let field=null,polling=false,steps=[],workbench=null,runStarting=false,runIntent=0;
 async function api(path,body,timeoutMs=200){const response=await fetch(path,{method:body?'POST':'GET',cache:'no-store',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined,signal:AbortSignal.timeout(timeoutMs)});if(!response.ok)throw Error('HTTP '+response.status);return response.json();}
 const runner=new BoothRunner(api);
 const kinds={wait:'Wait',face:'Expression',drive:'Tracks',aim:'Mast',cue:'Tracks + mast'};
 const initial=[{type:'face',ms:3000,mode:'greeting'},{type:'wait',ms:1000},{type:'face',ms:1000,mode:'auto'}];
 function defaults(type){return type==='drive'?{type,ms:300,left:10,right:10}:type==='aim'?{type,ms:2000,pan:0,tilt:0}:type==='cue'?{type,ms:300,left:10,right:10,pan:2,tilt:0,mode:'greeting'}:type==='face'?{type,ms:1000,mode:'greeting'}:{type:'wait',ms:1000};}
 function playback(){const repeats=$('repeatMode').value==='forever'?0:$('repeatMode').value==='count'?Number($('repeatCount').value):1;return validatePlayback({repeats,gapMs:Math.round(Number($('gap').value)*1000)});}
 function busy(){for(const el of $('sequence-editor').querySelectorAll('button,input,select,textarea'))el.disabled=runner.running||runStarting||el.dataset.boundary==='true';$('run').disabled=runner.running||runStarting||!!workbench?.busy;workbench?.controls();}
 runner.onStatus=text=>{$('reply').textContent=text;busy();};
 function read(){return validateRoutine(JSON.parse($('routine').value));}
 function describe(s,i){return (i+1)+'. '+kinds[s.type]+' / '+(s.ms/1000)+' s'+(usesTracks(s)?' / L '+s.left+'% R '+s.right+'%':'')+(usesMast(s)?' / pan '+s.pan+'° tilt '+s.tilt+'°':'')+(s.mode?' / '+s.mode:'');}
 function preview(){const list=read(),play=playback();const ms=list.reduce((n,x)=>n+x.ms,0),drive=list.filter(usesTracks).reduce((n,x)=>n+x.ms,0);$('preview').textContent=list.map(describe).join('\n');$('summary').textContent='One cycle: '+(ms/1000)+' s / track time: '+(drive/1000)+' s. '+(play.repeats===1?'Run once.':play.repeats?'Repeat '+play.repeats+' cycles; '+(play.gapMs/1000)+' s pause.':'Repeat until STOP; '+(play.gapMs/1000)+' s pause.');return list;}
 function refreshPreview(){try{preview();$('preview').classList.remove('invalid');}catch(e){$('preview').textContent=e.message;$('preview').classList.add('invalid');}}
 function sync(){ $('routine').value=JSON.stringify(steps,null,2);refreshPreview();}
 function action(fn){return async()=>{if(runner.running||runStarting)return;try{await fn();}catch(e){if(e.message!=='Stopped.')$('reply').textContent=e.message;}};}
 function el(tag,text,cls){const o=document.createElement(tag);if(text!==undefined)o.textContent=text;if(cls)o.className=cls;return o;}
 function render(){const list=$('steps');list.replaceChildren();steps.forEach((s,i)=>{
  const card=el('article',undefined,'step'),top=el('div',undefined,'step-top'),name=el('strong','Step '+(i+1));top.append(name);
  const select=el('select');select.setAttribute('aria-label','Step '+(i+1)+' type');for(const [value,label] of Object.entries(kinds)){const o=el('option',label);o.value=value;select.append(o);}select.value=s.type;
  select.onchange=()=>{const next=defaults(select.value);if(usesMast(next)&&usesMast(s)){next.pan=s.pan;next.tilt=s.tilt;}if(usesTracks(next)&&usesTracks(s)){next.left=s.left;next.right=s.right;}steps[i]=next;sync();render();};top.append(select);card.append(top);
  const fields=el('div',undefined,'fields');
  function number(key,label,value,min,max,step=1,scale=1){const l=el('label',label),input=el('input');input.type='number';input.value=value;input.min=min;input.max=max;input.step=step;input.setAttribute('aria-label','Step '+(i+1)+' '+label);input.oninput=()=>{s[key]=Math.round(Number(input.value)*scale*10)/10;sync();};l.append(input);fields.append(l);}
  number('ms','Seconds',s.ms/1000,.1,usesTracks(s)?1.5:15,.1,1000);
  if(usesTracks(s)){number('left','Left track %',s.left,-25,25);number('right','Right track %',s.right,-25,25);}
  if(usesMast(s)){number('pan','Pan °',s.pan,-45,45,.1);number('tilt','Tilt °',s.tilt,-20,20,.1);}
  if(s.type==='face'||s.type==='cue'){const l=el('label','Expression'),input=el('select');if(s.type==='cue'){const none=el('option','Keep current');none.value='';input.append(none);}for(const name of faceModes){const o=el('option',name);o.value=name;input.append(o);}input.value=s.mode||'';input.setAttribute('aria-label','Step '+(i+1)+' expression');input.onchange=()=>{if(input.value)s.mode=input.value;else delete s.mode;sync();};l.append(input);fields.append(l);}
  card.append(fields);const row=el('div',undefined,'row step-actions');
  for(const [label,verb,fn,disabled] of [['↑','Move up',()=>{[steps[i-1],steps[i]]=[steps[i],steps[i-1]];},i===0],['↓','Move down',()=>{[steps[i+1],steps[i]]=[steps[i],steps[i+1]];},i===steps.length-1],['Duplicate','Duplicate',()=>{if(steps.length>=40)throw Error('Maximum 40 steps.');steps.splice(i+1,0,{...s});},false],['Remove','Remove',()=>steps.splice(i,1),false]]){const b=el('button',label);b.type='button';b.disabled=disabled;b.dataset.boundary=String(disabled);b.setAttribute('aria-label',verb+' step '+(i+1));b.onclick=action(()=>{fn();sync();render();});row.append(b);}
  card.append(row);list.append(card);
 });}
 function load(list){steps=validateRoutine(list);sync();render();$('motion').checked=$('mast').checked=false;}
 try{load(JSON.parse(localStorage.getItem(key)||JSON.stringify(initial)));}catch{load(initial);}
 function repeatChanged(){const mode=$('repeatMode').value;$('countLabel').hidden=mode!=='count';$('gapLabel').hidden=mode==='once';$('run').textContent=mode==='once'?'Run once':'Start repeating';refreshPreview();}
 try{const p=JSON.parse(localStorage.getItem(playKey)||'null');if(p){validatePlayback(p);$('repeatMode').value=p.repeats===0?'forever':p.repeats===1?'once':'count';$('repeatCount').value=p.repeats>1?p.repeats:3;$('gap').value=p.gapMs/1000;}}catch{}
 for(const id of ['repeatMode','repeatCount','gap'])$(id).onchange=repeatChanged;repeatChanged();
 for(const b of document.querySelectorAll('[data-add]'))b.onclick=action(()=>{if(steps.length>=40)throw Error('Maximum 40 steps.');steps.push(defaults(b.dataset.add));sync();render();});
 $('check').onclick=action(()=>{load(preview());$('reply').textContent='Preview updated. No robot commands sent.';});
 $('save').onclick=action(()=>{localStorage.setItem(key,JSON.stringify(preview(),null,2));localStorage.setItem(playKey,JSON.stringify(playback()));$('reply').textContent='Saved on this device. Starting always requires Run.';});
 $('export').onclick=action(()=>{const url=URL.createObjectURL(new Blob([JSON.stringify(preview(),null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='gladiator-routine.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
 $('import').onclick=action(()=>$('file').click());$('file').onchange=action(async()=>{const f=$('file').files[0];if(!f)return;if(f.size>12000)throw Error('Routine file too large.');load(JSON.parse(await f.text()));});
 $('example').onclick=action(()=>load([{type:'face',ms:1000,mode:'greeting'},{type:'drive',ms:300,left:10,right:10},{type:'wait',ms:2000},{type:'drive',ms:300,left:-10,right:-10},{type:'face',ms:1000,mode:'auto'}]));
 $('combined').onclick=action(()=>load([{type:'face',ms:1000,mode:'greeting'},{type:'cue',ms:300,left:10,right:10,pan:2,tilt:0},{type:'aim',ms:1500,pan:2,tilt:0},{type:'cue',ms:300,left:-10,right:-10,pan:-2,tilt:0},{type:'aim',ms:1500,pan:0,tilt:0},{type:'face',ms:1000,mode:'auto'}]));
 $('display').onclick=action(()=>load(initial));
 $('run').onclick=action(async()=>{const list=preview(),intent=++runIntent,options={motion:$('motion').checked,mast:$('mast').checked,...playback()};runStarting=true;busy();try{await workbench?.beforeRun(list);if(intent!==runIntent)throw Error('Stopped.');runStarting=false;await runner.run(list,options);}finally{if(intent===runIntent){runStarting=false;$('motion').checked=$('mast').checked=false;}busy();}});
 $('stop').onclick=()=>{++runIntent;runStarting=false;workbench?.cancel();runner.stop('STOP requested.',true);$('motion').checked=$('mast').checked=false;};
 function leave(){++runIntent;runStarting=false;workbench?.cancel();if(runner.running)runner.stop('Page left; outputs OFF requested.');$('motion').checked=$('mast').checked=false;}
 window.addEventListener('blur',leave);window.addEventListener('pagehide',leave);document.addEventListener('visibilitychange',()=>{if(document.hidden)leave();});window.addEventListener('keydown',e=>{if(e.key==='Escape')$('stop').click();});
 function displayField(next){field=next;runner.snapshot=next;$('health').textContent=field.connected?'S3 connected / '+(field.transport||'unknown'):'S3 unavailable';$('power').textContent=field.powerValid?Number(field.volts).toFixed(2)+' V / '+(field.controller?.battery||'unknown'):'Power unavailable';const m=field.sensors?.mast;$('mastHealth').textContent=m?.calibrated?'S3 aiming profile applied / pan '+m.panMinDeg+'° to '+m.panMaxDeg+'° / tilt '+m.tiltMinDeg+'° to '+m.tiltMaxDeg+'°':'S3 aiming profile not applied — saved pulse settings are shown below.';$('uptime').textContent=field.controller?'S3 uptime '+Math.floor(field.controller.s3Ms/60000)+' min / boot '+(field.controller.bootCount??'?'):'';$('diagnostics').textContent=JSON.stringify(field,null,2);workbench?.update(field);}
 workbench=window.createMastWorkbench({api:(path,body)=>api(path,body,1200),isRunning:()=>runner.running||runStarting,onBusy:busy,addStep:step=>{if(steps.length>=40)throw Error('Maximum 40 steps.');steps.push(step);sync();render();}});
 runner.onSnapshot=displayField;runner.onState=busy;
 async function poll(){if(!runner.running&&!polling){polling=true;try{displayField(await api('/api/field'));}catch{$('health').textContent='Connection unavailable';workbench?.update(null);}finally{polling=false;}}setTimeout(poll,500);}
 poll();
}
