'use strict';
const $=id=>document.getElementById(id);
const nativeHost=document.documentElement.dataset.hosted==='c6';
const client=crypto.getRandomValues(new Uint32Array(1))[0]||1;
let generation='', connected=false, m=null, field=null, visual=null, lastFresh=0, polling=false;
let test=null, enabling=0, active=false, token=0, sequence=0, commandEpoch=0, queue=[], writing=false, pendingProfile=null, lastPulse=null;
const logs=[],pulseHistory={};
function log(message){logs.unshift(new Date().toLocaleTimeString()+' · '+message);logs.length=Math.min(logs.length,60);$('log').replaceChildren(...logs.map(s=>{const li=document.createElement('li');li.textContent=s;return li;}));}
function notice(message){$('notice').textContent=message;$('notice').classList.remove('error');log(message);}
function numeric(id,min=-Infinity,max=Infinity){const v=$(id).value;if(!v.trim())throw Error('Enter '+($(id).closest('label')?.childNodes[0]?.textContent?.trim()||id)+' first.');const n=Number(v);if(!Number.isFinite(n)||n<min||n>max)throw Error(id+' must be between '+min+' and '+max);return n;}
function integer(id,min,max){const n=numeric(id,min,max);if(!Number.isInteger(n))throw Error(id+' must be a whole number');return n;}
function fresh(){return connected&&m?.fresh&&performance.now()-lastFresh<650;}
function owns(){return fresh()&&active&&m.enabled&&m.owner===client&&m.session===token&&m.safe;}
function idle(){if(!connected)throw Error('Connect to the robot first.');if(m?.enabled||test||enabling)throw Error('Stop the active mast output before starting another test.');if(m?.testActive&&m.testPin!==Number($('pin').value))throw Error('GPIO'+m.testPin+' is being tested. Select that GPIO to resume, or stop it first.');}
function resumePulseTest(){
 if(!fresh()||!m.testActive||m.testPin!==Number($('pin').value))return false;
 test={pin:m.testPin,firstUs:Math.round(m.testPulseUs),minUs:m.testMinUs,maxUs:m.testMaxUs,frequencyHz:Number($('hz').value),target:Math.round(m.testTargetUs??m.testPulseUs),confirmed:true,sent:true,started:performance.now(),lastRenew:performance.now()};
 $('batteryOverride').checked=!!m.testBatteryOverride;$('pulseTarget').value=test.target;render();notice('Resumed GPIO'+test.pin+' test at S3 command '+Math.round(m.testPulseUs)+' µs. No new starting pulse sent.');return true;
}
async function api(path,body,gen=generation){if(!gen)throw Error('Connect first');let r,text;try{r=await fetch((nativeHost?'':'/robot')+path,{method:body===undefined?'GET':'POST',headers:{'Content-Type':'application/json',...(nativeHost?{}:{'X-Hardware-Generation':gen})},...(body===undefined?{}:{body:JSON.stringify(body)}),signal:AbortSignal.timeout(2200),cache:'no-store'});text=await r.text();}catch(e){if(e.name==='TimeoutError'||e.name==='AbortError')throw Error('Gateway HTTP request timed out. Check Gladiator Wi-Fi.'+(body===undefined?'':' Command not confirmed.'));throw e;}let data;try{data=JSON.parse(text);}catch{throw Error('Gateway did not return JSON');}if(!r.ok){const e=Error(data.error||text);e.status=r.status;throw e;}return data;}
function write(path,body,{heartbeat=false,stop=false,label='Command'}={}){
 if(heartbeat&&(writing||queue.length))return Promise.resolve(null);
 const epoch=commandEpoch,gen=generation;
 return new Promise((resolve,reject)=>{const job={path,body,epoch,gen,resolve,reject,stop,label,heartbeat};if(stop)queue.unshift(job);else queue.push(job);pump();});
}
async function pump(){if(writing)return;writing=true;while(queue.length){const job=queue.shift();if(job.epoch!==commandEpoch||job.gen!==generation){job.reject(Error('Command cancelled'));continue;}try{const r=await api(job.path,job.body,job.gen);if(!job.heartbeat)log(job.label+' queued; S3 status confirms execution');job.resolve(r);}catch(e){job.reject(e);}}writing=false;}
function clearOwnership(){active=false;enabling=0;test=null;token=0;$('enableConfirm').checked=false;commandEpoch++;for(const job of queue.splice(0))job.reject(Error('Command cancelled'));render();}
function mastBody(operation,pan=0,tilt=0){return {session:operation===7?client:(token||client),sequence:++sequence,operation,pan,tilt};}
async function allOff(disarm=false){if(!generation)return;clearOwnership();$('batteryOverride').checked=false;stopVideo();const jobs=[write('/api/mast',mastBody(0),{stop:true,label:'Mast OFF'}),write('/api/mast/config',{action:'stop'},{stop:true,label:'Pulse test OFF'})];let c=field?.controller;if(disarm&&!c){try{field=await api('/api/field');c=field.controller;}catch{}}if(disarm&&c)jobs.push(write('/api/control',{epoch:c.epoch,client,sequence:++sequence,observedMs:c.s3Ms,operation:0,deadman:0,overdrive:0,power:c.power||25,left:0,right:0},{stop:true,label:'Tracks DISARM'}));const results=await Promise.allSettled(jobs);if(disarm&&!c)notice('Mast OFF requested, but track DISARM could not be sent without gateway controller status. Check the field controls.');else if(results.some(r=>r.status==='rejected'))notice('Stop could not be confirmed. S3 session leases will expire; inspect the mechanism.');else notice('Outputs OFF requested. Check the live S3 status for confirmation.');}
function guard(fn){return async(...args)=>{try{await fn(...args);}catch(e){notice(e.message);$('notice').classList.add('error');}};}
for(const axis of ['pan','tilt']){
 const f=document.createElement('section');f.className='axis-card';
 const heading=axis==='pan'?'Pan · left & right':'Tilt · up & down',limit=axis==='pan'?5:3;
 const field=(key,label,value='',step='0.1')=>'<label>'+label+'<input id="'+axis+key+'" type="number" step="'+step+'" value="'+value+'"></label>';
 f.innerHTML='<h2>'+heading+'</h2><label>Servo connection<select id="'+axis+'Pin"><option value="10">GPIO10</option><option value="13" '+(axis==='tilt'?'selected':'')+'>GPIO13</option></select></label><p id="'+axis+'Summary" class="axis-summary">No positions recorded</p><div class="grid">'+field('Min','Allowed negative head angle · °',-limit)+field('Max','Allowed positive head angle · °',limit)+'</div><p class="hint">'+(axis==='pan'?'Negative = left, positive = right.':'Negative = down, positive = up.')+' These limits must fit inside your measured travel.</p><details id="'+axis+'Pulses"><summary>Review recorded pulse values</summary><div class="grid">'+field('Low','Lower pulse · µs','','1')+field('Neutral','Center pulse · µs','','1')+field('High','Higher pulse · µs','','1')+'</div></details><details id="'+axis+'Angles"><summary>Enter measured shaft angles <span>Required before applying aiming</span></summary><p class="hint">Measure the servo shaft from your chosen center at each recorded endpoint. '+(axis==='pan'?'Pan head motion is 1.5× the shaft motion.':'Tilt head and shaft angles are equal.')+' Use 0° on an endpoint side only if that endpoint is also your center.</p><div class="grid">'+field('ShaftMin','Negative shaft angle · °')+field('ShaftMax','Positive shaft angle · °')+'</div></details><label class="check"><input id="'+axis+'Reverse" type="checkbox">Higher pulses move '+(axis==='pan'?'left':'down')+' (reverse direction)</label>';
 $('axisForms').append(f);
}
function axisProfile(axis){const ceiling=axis==='pan'?45:20;const a={pin:integer(axis+'Pin',10,13),lowUs:integer(axis+'Low',500,2575),neutralUs:integer(axis+'Neutral',500,2575),highUs:integer(axis+'High',500,2575),shaftMinDeciDeg:Math.round(numeric(axis+'ShaftMin',-180,0)*10),shaftMaxDeciDeg:Math.round(numeric(axis+'ShaftMax',0,180)*10),minDeciDeg:Math.round(numeric(axis+'Min',-ceiling,0)*10),maxDeciDeg:Math.round(numeric(axis+'Max',0,ceiling)*10),reversed:$(axis+'Reverse').checked};validateAxis(a,axis);return a;}
function validateAxis(a,axis){const ratio=axis==='pan'?1.5:1,ceiling=axis==='pan'?450:200;if(![10,13].includes(a.pin)||typeof a.reversed!=='boolean')throw Error('Invalid '+axis+' GPIO or direction');for(const key of ['lowUs','neutralUs','highUs','shaftMinDeciDeg','shaftMaxDeciDeg','minDeciDeg','maxDeciDeg'])if(!Number.isInteger(a[key]))throw Error('Invalid '+axis+' '+key);if(a.lowUs<500||a.highUs>2575||a.lowUs>=a.highUs||a.neutralUs<a.lowUs||a.neutralUs>a.highUs)throw Error(axis+': low ≤ neutral ≤ high; pulse envelope 500–2575 µs is a parser bound, not a servo specification.');const sm=a.shaftMinDeciDeg,sx=a.shaftMaxDeciDeg,mn=a.minDeciDeg,mx=a.maxDeciDeg,neg=a.reversed?a.highUs:a.lowUs,pos=a.reversed?a.lowUs:a.highUs;if(sm< -1800||sx>1800||sm>0||sx<0||sm>=sx||mn>0||mx<0||mn>=mx||mn< -ceiling||mx>ceiling||mn<sm*ratio||mx>sx*ratio||(sm===0?neg!==a.neutralUs:neg===a.neutralUs)||(sx===0?pos!==a.neutralUs:pos===a.neutralUs))throw Error(axis+': measured shaft angles, neutral pulses and allowed head travel do not agree.');}
function profile(){const p={action:'calibrate',panModel:'SG90',tiltModel:'SG90',positionalVerified:true,clearanceVerified:true,frequencyHz:integer('hz',50,100),speedDeciDegPerSec:Math.round(numeric('speed',0.1,30)*10),pan:axisProfile('pan'),tilt:axisProfile('tilt')};if(p.pan.pin===p.tilt.pin)throw Error('Pan and tilt must use different GPIOs');return p;}
function fillProfile(p){for(const axis of ['pan','tilt'])validateAxis(p[axis],axis);if(p.pan.pin===p.tilt.pin||!Number.isInteger(p.frequencyHz)||p.frequencyHz<50||p.frequencyHz>100||!Number.isInteger(p.speedDeciDegPerSec)||p.speedDeciDegPerSec<1||p.speedDeciDegPerSec>300)throw Error('Invalid profile');for(const axis of ['pan','tilt']){const a=p[axis];for(const [key,value] of Object.entries({Pin:a.pin,Low:a.lowUs,Neutral:a.neutralUs,High:a.highUs,ShaftMin:a.shaftMinDeciDeg/10,ShaftMax:a.shaftMaxDeciDeg/10,Min:a.minDeciDeg/10,Max:a.maxDeciDeg/10}))$(axis+key).value=value;$(axis+'Reverse').checked=a.reversed;}$('hz').value=p.frequencyHz;$('speed').value=p.speedDeciDegPerSec/10;$('aimSpeed').value=p.speedDeciDegPerSec/10;notice('Robot calibration loaded. The S3 reports and enforces its saved travel limits.');friendlyRender();}
function download(name,data){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);}
$('connect').onclick=guard(async()=>{if(active||enabling)await allOff();clearOwnership();connected=false;generation='';m=null;field=null;robotSetup=null;setupDirty={};loadedRobotProfile=false;clearTimeout(setupTimer);$('cameraApply').disabled=true;render();let c;if(nativeHost)c={generation:'c6-'+client,target:location.origin};else{const r=await fetch('/connection',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:$('target').value}),signal:AbortSignal.timeout(2000)});c=await r.json();if(!r.ok)throw Error(c.error);}generation=c.generation;connected=true;$('target').value=c.target;$('fieldLink').href=c.target+'/';$('fieldLink').hidden=false;$('connection').textContent='Connected bridge to '+c.target+' · waiting for S3';notice('Connected. No output enable command was sent.');poll();await loadRobotSetup();});
$('off').onclick=guard(()=>allOff(true));$('stopTest').onclick=guard(()=>allOff());$('disable').onclick=guard(()=>allOff());
$('startTest').onclick=guard(async()=>{idle();if(resumePulseTest())return;const overrideBatteryLockout=$('batteryOverride').checked;if(overrideBatteryLockout&&!m?.testOverrideSupported)throw Error('The S3 needs the manual calibration update first.');if(!m?.safe&&!overrideBatteryLockout)throw Error('Battery or power interlock active. Select temporary manual calibration for a servo-only test.');const body={action:'test',pin:integer('pin',10,13),firstUs:integer('first',500,2575),minUs:500,maxUs:2575,frequencyHz:integer('hz',50,100),...(overrideBatteryLockout?{overrideBatteryLockout:true}:{})};const epoch=commandEpoch;test={...body,target:body.firstUs,confirmed:false,started:performance.now(),lastRenew:performance.now()};render();try{await write('/api/mast/config',body,{label:'Start GPIO '+body.pin});if(epoch!==commandEpoch)return;test.sent=true;$('pulseTarget').value=body.firstUs;}catch(e){if(epoch===commandEpoch)await allOff();throw e;}});
async function step(sign){await movePulse((test?.target||0)+sign*integer('step',1,100));}
$('less').onclick=guard(()=>step(-1));$('more').onclick=guard(()=>step(1));
for(const axis of ['pan','tilt'])$('record'+axis[0].toUpperCase()+axis.slice(1)).onclick=guard(()=>capturePosition(axis,'Neutral'));
$('saveProfile').onclick=guard(async()=>{const p=profile();persistDraft();if(!connected)throw Error('Connect to save calibration on the robot.');markSetup(draftFields());await flushSetup();if(test||active||enabling||m?.testActive||m?.enabled)await allOff();const rejected=m?.rejected||0;await write('/api/mast/config',p,{label:'Save calibration'});pendingProfile={profile:p,rejected,started:performance.now()};$('profileStatus').textContent='Setup queued; waiting for the S3 save result. Outputs stay off.';});
$('loadProfile').onclick=guard(()=>{if(!fresh()||!m.profile)throw Error('No saved S3 profile available');fillProfile(m.profile);});
$('exportProfile').onclick=()=>download('gladiator-mast-draft.json',draftData());
$('importProfile').onchange=guard(async()=>{const file=$('importProfile').files[0];if(!file)return;if(file.size>8192)throw Error('Profile file is too large');const data=JSON.parse(await file.text());if(data.kind==='partial-mast-draft')restoreDraft(data);else fillProfile(data.profile||data);persistDraft();notice('Setup draft restored into fields only. Outputs remain unchanged.');$('importProfile').value='';});
$('enableConfirm').onchange=render;
$('enable').onclick=guard(async()=>{if(!fresh()||!m.safe||!m.calibrated||m.enabled||m.testActive||test||!$('enableConfirm').checked)throw Error('Load a calibrated, safe, disabled S3 and confirm neutral positioning.');enabling=performance.now();render();try{await write('/api/mast',mastBody(7),{label:'Enable calibrated mast'});}catch(e){await allOff();throw e;}});
async function aim(op,pan=0,tilt=0){if(!owns())throw Error('Enable this page’s calibrated mast session first.');try{await write('/api/mast',mastBody(op,pan,tilt),{label:['OFF','Hold','Center','Bounded scan'][op]||'Mast command'});}catch(e){await allOff();throw e;}}
$('hold').onclick=guard(()=>aim(1));$('center').onclick=guard(()=>aim(2));$('scan').onclick=guard(()=>aim(3));$('rate').onclick=guard(()=>aim(5,Math.round(numeric('speed',0.1,30)*10)));
for(const b of document.querySelectorAll('[data-pan]')){b.onpointerdown=guard(async e=>{e.preventDefault();if(b.disabled)return;b.setPointerCapture(e.pointerId);await aim(6,Number(b.dataset.pan),Number(b.dataset.tilt));});const release=()=>{if(owns())guard(()=>aim(1))();};b.onpointerup=release;b.onpointercancel=release;b.onlostpointercapture=release;}
function render(){const f=fresh(),own=owns();$('startTest').disabled=!f||!!test||!!enabling||!!m?.enabled||!!(m?.testActive&&m.testPin!==Number($('pin').value));$('startTest').textContent=!test&&m?.testActive&&m.testPin===Number($('pin').value)?'Resume servo test':'Start servo test';for(const id of ['less','more','pulseGo','pulseSlider'])$(id).disabled=!f||!test?.confirmed;$('enable').disabled=!f||!m?.safe||!m?.calibrated||m?.enabled||m?.testActive||!!test||!!enabling||!$('enableConfirm').checked;for(const id of ['hold','center','scan','rate'])$(id).disabled=!own;document.querySelectorAll('[data-pan]').forEach(b=>b.disabled=!own);$('pulseStatus').textContent=test?'GPIO '+test.pin+' · target '+test.target+' µs · '+(test.confirmed?(f?'S3 commanded '+m.testPulseUs+' µs':'waiting for live S3 status'):'waiting for S3 confirmation'):m?.testActive?'GPIO '+m.testPin+' active from another session · S3 commanded '+m.testPulseUs+' µs':'Test stopped. Choose a servo to begin.';$('aimStatus').textContent=own?'Enabled here · commanded pan '+m.commandedPanDeg+'° / tilt '+m.commandedTiltDeg+'° · '+(m.scanning?'scanning':'manual'):'Mast off here · enable when ready';friendlyRender();}
function update(){const c=field?.controller;$('connection').textContent=fresh()?'Gladiator connected':connected?'Waiting for Gladiator · check network and S3 status':'Connect Gladiator to begin';$('mastStatus').textContent=fresh()?(m.testActive?'Testing GPIO'+m.testPin:m.enabled?'Mast enabled':'Servos off'):'Servos: unknown';$('powerStatus').textContent='Power '+(Number.isFinite(field?.volts)?field.volts.toFixed(2)+' V':'unknown')+(c?.batteryLockout?' · LOCKOUT':'');$('trackStatus').textContent=c?'Tracks '+(c.armed?'ARMED':'disarmed')+' · '+(c.leftPercent??'?')+' / '+(c.rightPercent??'?')+'%':'Tracks unknown';$('rawMast').textContent=JSON.stringify(m,null,2);$('sensorData').textContent=JSON.stringify(field?.sensors||{status:'No sensor telemetry'},null,2);sensorCards();$('sensorSummary').textContent=field?.connected?'Live sensor metadata below. Use sample validity and health before interpreting ranges.':'Sensor link unknown. Unknown readings are not obstacle detections.';render();}
async function poll(){if(polling||!connected)return;polling=true;const gen=generation;try{const next=await api('/api/mast',undefined,gen);if(gen!==generation)return;m=next;rememberPulse(m);if(!loadedRobotProfile&&m.profile&&m.calibrated){loadedRobotProfile=true;fillProfile(m.profile);if(robotSetup)applyRobotFields(robotSetup.fields);}if(m.fresh)lastFresh=performance.now();else if(active||enabling){await allOff();notice('S3 telemetry became stale. Session ended; enable deliberately after recovery.');}
if(enabling&&m.fresh&&m.enabled&&m.owner===client){active=true;enabling=0;token=m.session;sequence=Math.max(sequence,m.sequence||0);log('S3 confirmed this page’s enabled session');}
if(enabling&&performance.now()-enabling>1800){await allOff();notice('Enable was not confirmed; inspect S3 action and rejection count.');}
if(active&&(!m.enabled||m.owner!==client||m.session!==token||!m.safe)){await allOff();notice('Mast session ended. Automatic resume is disabled.');}
if(test&&m.fresh&&test.sent){const match=m.testActive&&m.testPin===test.pin&&m.testMinUs===test.minUs&&m.testMaxUs===test.maxUs;if(match){test.confirmed=true;}else if(test.confirmed||performance.now()-test.started>1800){clearOwnership();$('batteryOverride').checked=false;notice('Pulse test stopped on the S3: '+(m.action||m.reason||'test ended')+'. You can start another test.');}}
if(pendingProfile){const expected=pendingProfile.profile;
 if(m.rejected>pendingProfile.rejected){$('profileStatus').textContent='S3 did not save: '+(m.action||m.reason||'request rejected');pendingProfile=null;}
 else if(m.profile&&['frequencyHz','speedDeciDegPerSec','pan','tilt'].every(key=>typeof expected[key]==='object'?Object.entries(expected[key]).every(([k,v])=>m.profile[key]?.[k]===v):m.profile[key]===expected[key])){$('profileStatus').textContent='S3 profile readback matches the saved calibration. Outputs remain off.';pendingProfile=null;log('Calibration confirmed by S3 readback');}
 else if(performance.now()-pendingProfile.started>5000){$('profileStatus').textContent='Save not confirmed: '+(m.action||'check the S3 link');pendingProfile=null;}
}
appliedLimits();update();}catch(e){if(gen===generation){m=null;if(active||enabling)await allOff();$('connection').textContent='Connection interrupted · '+e.message;render();}}finally{polling=false;if(connected)setTimeout(poll,200);}}
let slowBusy=false;setInterval(async()=>{if(!connected||slowBusy)return;slowBusy=true;const gen=generation;const result=await Promise.allSettled([api('/api/field',undefined,gen),api('/api/visual',undefined,gen)]);if(gen===generation){field=result[0].status==='fulfilled'?result[0].value:null;visual=result[1].status==='fulfilled'?result[1].value:null;update();}slowBusy=false;},1200);
setInterval(()=>{if(!connected||document.hidden)return;if(!fresh()){if(active||enabling)guard(()=>allOff())();return;}if(owns())write('/api/mast',mastBody(4),{heartbeat:true}).catch(()=>guard(()=>allOff())());else if(test?.confirmed&&$('maintain').checked&&performance.now()-test.lastRenew>1000){test.lastRenew=performance.now();write('/api/mast/config',{action:'jog-test',pin:test.pin,pulseUs:test.target},{heartbeat:true}).catch(()=>notice('Test connection interrupted; waiting for live S3 status.'));}},150);
function stopVideo(){$('video').removeAttribute('src');$('video').hidden=true;$('videoStatus').textContent='Video off';$('videoPlaceholder').hidden=false;}
$('videoStop').onclick=stopVideo;
$('videoStart').onclick=guard(async()=>{if(!generation)throw Error('Connect first');visual=await api('/api/visual');const u=new URL(visual.streamUrl);const parts=u.hostname.split('.').map(Number);if(u.protocol!=='http:'||u.username||u.password||parts.length!==4||parts.some(n=>!Number.isInteger(n)||n<0||n>255)||!(parts[0]===10||parts[0]===192&&parts[1]===168||parts[0]===172&&parts[1]>=16&&parts[1]<=31))throw Error('Camera must advertise a private LAN HTTP stream');$('video').src=u.href;$('video').hidden=false;$('videoPlaceholder').hidden=true;$('videoStatus').textContent='Live stream · one viewer at a time · browser needs a route to '+u.hostname;});
$('video').onerror=()=>{stopVideo();$('videoStatus').textContent='Stream unavailable. Check ESP32-CAM power, Wi-Fi and address.';};
let cameraBusy=false;
async function camera(body){if(cameraBusy)return;cameraBusy=true;const ids=['resolution','brightness','exposure','aec','flip','mirror','cameraRead','cameraApply'];ids.forEach(id=>$(id).disabled=true);let success=false;try{const c=await (body?write('/api/camera',body,{label:'Camera settings'}):api('/api/camera'));$('resolution').value=c.resolution;$('brightness').value=c.brightness;$('exposure').value=c.aecValue;$('aec').checked=!!c.aec;$('flip').checked=!!c.vflip;$('mirror').checked=!!c.hmirror;success=true;notice('Camera settings read back. They reset on camera reboot.');}finally{cameraBusy=false;ids.forEach(id=>$(id).disabled=false);$('cameraApply').disabled=!success;}}
$('cameraRead').onclick=guard(()=>camera());$('cameraApply').onclick=guard(()=>camera({resolution:$('resolution').value,brightness:integer('brightness',-2,2),aec:$('aec').checked?1:0,aecValue:integer('exposure',0,1200),vflip:$('flip').checked?1:0,hmirror:$('mirror').checked?1:0}));
$('face').onclick=guard(()=>write('/api/visual',{mode:$('expression').value,leaseMs:30000},{label:'Temporary face '+$('expression').value}));
$('diagnostics').onclick=()=>download('gladiator-hardware-diagnostics.json',{captured:new Date().toISOString(),target:$('target').value,mast:m,field,activity:logs});

let currentView='identify';
function showView(name){if(!['identify','calibrate','operate','sensors'].includes(name))return;currentView=name;for(const panel of document.querySelectorAll('.view'))panel.hidden=panel.id!==name;for(const tab of document.querySelectorAll('[data-view]')){const selected=tab.dataset.view===name;tab.setAttribute('aria-selected',String(selected));tab.tabIndex=selected?0:-1;}window.scrollTo({top:0,behavior:'instant'});}
for(const tab of document.querySelectorAll('[data-view]')){tab.onclick=()=>showView(tab.dataset.view);tab.onkeydown=e=>{const tabs=[...document.querySelectorAll('[data-view]')],i=tabs.indexOf(tab);let n;if(e.key==='ArrowRight')n=(i+1)%tabs.length;else if(e.key==='ArrowLeft')n=(i+tabs.length-1)%tabs.length;else if(e.key==='Home')n=0;else if(e.key==='End')n=tabs.length-1;else return;e.preventDefault();showView(tabs[n].dataset.view);tabs[n].focus();};}
document.querySelectorAll('[data-go]').forEach(b=>b.onclick=()=>showView(b.dataset.go));
$('first').addEventListener('input',friendlyRender);
function captureCandidate(){
 // Recording persists robot configuration only; it never sends a pulse.
 const selected=Number($('pin').value);
 if(pulseHistory[selected])return pulseHistory[selected];if(lastPulse?.pin===selected)return lastPulse;
 
 return null;
}
function rememberPulse(state){
 if(![10,13].includes(state?.testPin)||!Number.isFinite(state.testPulseUs)||state.testPulseUs<500||state.testPulseUs>2575)return;
 const pulse=Math.round(state.testPulseUs);if(pulseHistory[state.testPin]?.pulse===pulse&&pulseHistory[state.testPin]?.reported)return;lastPulse={pin:state.testPin,pulse,source:'last S3 command',reported:true};pulseHistory[state.testPin]=lastPulse;
 persistDraft();
}
async function capturePosition(axis,key){
 const sample=captureCandidate();if(!sample)throw Error('No recorded command for this GPIO yet. Run a test, or enter a pulse in Save setup.');
 $(axis+'Pin').value=sample.pin;$(axis+key).value=sample.pulse;
 const other=axis==='pan'?'tilt':'pan';if(Number($(other+'Pin').value)===sample.pin)$(other+'Pin').value=sample.pin===10?13:10;
 $('captureAxis').value=axis;
 markSetup([axis+'Pin',axis+key,other+'Pin']);friendlyRender();await flushSetup();
 notice('Saved on robot: '+axis+' '+(key==='Neutral'?'center':key==='Low'?'lower endpoint':'higher endpoint')+': '+sample.pulse+' µs on GPIO'+sample.pin+' ('+sample.source+'). No movement command sent.');
}
for(const [id,key] of [['captureLow','Low'],['captureHigh','High']])$(id).onclick=guard(()=>capturePosition($('captureAxis').value,key));
$('nextServo').onclick=guard(async()=>{const previous=test?.pin||Number($('pin').value);await allOff();$('pin').value=previous===10?13:10;$('first').value='';notice('Now testing GPIO'+$('pin').value+'. Choose its starting pulse, then start when ready.');friendlyRender();window.scrollTo({top:0,behavior:'instant'});});
$('reviewSetup').onclick=guard(async()=>{if(test||active||enabling||m?.testActive||m?.enabled)await allOff();showView('calibrate');friendlyRender();});
function friendlyRender(){
 const live=fresh(),c=field?.controller,confirmed=!!(live&&test?.confirmed&&m?.testActive),pulse=confirmed?m.testPulseUs:captureCandidate()?.pulse;
 $('connectionDot').classList.toggle('live',!!live);
 $('connect').textContent=connected?'Reconnect':'Connect to robot';
 $('fieldLink').hidden=!connected;
 $('pulseNumber').textContent=Number.isFinite(pulse)?Math.round(pulse):'—';
 $('rangeLow').textContent='500 µs';
 $('rangeHigh').textContent='2575 µs';
 const lo=test?.minUs,hi=test?.maxUs;if(test){$('pulseSlider').min=lo;$('pulseSlider').max=hi;if(document.activeElement!==$('pulseSlider'))$('pulseSlider').value=test.target;}$('pulseMarker').style.left=(confirmed?Math.max(0,Math.min(100,(pulse-lo)/(hi-lo)*100)):50)+'%';
 for(const id of ['pin','first','hz'])$(id).disabled=!!test;
 $('batteryOverride').disabled=!!test||!m?.testOverrideSupported;
 $('overrideStatus').textContent=m?.testBatteryOverride?'Manual calibration override ACTIVE · tracks locked out · Stop clears it.':!m?.testOverrideSupported?'Connect to an S3 with manual calibration support to use the override.':'Tracks are locked out during an override test. Stop or S3 restart clears the override.';
 const sample=captureCandidate();for(const id of ['recordPan','recordTilt','captureLow','captureHigh'])$(id).disabled=!sample;$('captureValue').textContent=sample?'Ready to record GPIO'+sample.pin+' · '+sample.pulse+' µs ('+sample.source+')':'No pulse command recorded for this GPIO yet.';
 $('stopTest').disabled=!connected;
 let help=!connected?'Connect to the robot first.':!live?'Waiting for fresh S3 status. Check power, Wi-Fi and installed firmware.':test?'Test active. Use + / −, then record the positions you want.':m.enabled?'Another mast output is active. Stop it before starting a test.':m.testActive?(m.testPin===Number($('pin').value)?'This GPIO test is still active. Click Resume servo test to continue.':'Another GPIO is being tested. Select it to resume, or stop it first.'):!c?'Waiting for track status.':c.armed||c.deadman||Number(c.leftPercent)||Number(c.rightPercent)?'Disarm the tracks before testing.':!m.safe&&!$('batteryOverride').checked?'S3 reports motion unavailable. Check power, or select temporary manual calibration for servo testing.':!$('first').value?'Enter a starting pulse. Manual test range: 500–2575 µs.':'Ready. Starting the test will send the chosen first pulse.';
 $('testHelp').textContent=help;
 let centers=0;for(const axis of ['pan','tilt']){const v=key=>$(axis+key).value||'—';$(''+axis+'Summary').textContent='GPIO'+v('Pin')+' · low '+v('Low')+' / center '+v('Neutral')+' / high '+v('High')+' µs';if($(axis+'Neutral').value)centers++;}
 $('captureSummary').textContent=centers+' of 2 centers entered. Lower/higher refer to pulse values; set the physical direction in Save setup.';
 $('aimHelp').textContent=!live?'Connect and wait for live S3 status.':!m.calibrated?'Finish Save setup before enabling the mast.':m.testActive?'Stop the single-servo test before enabling aiming.':m.enabled&&!owns()?'The mast is controlled by another session. Stop it before taking control.':!m.safe?'S3 has inhibited movement. Check system status.':owns()?'Mast enabled here. Hold an arrow to aim.':'Ready to enable using the robot’s saved center and travel limits.';
 $('videoPlaceholder').hidden=!$('video').hidden;
}
document.addEventListener('input',e=>{if(e.target.closest('#axisForms')){persistDraft();friendlyRender();}});
document.addEventListener('change',e=>{if(e.target.closest('#axisForms')){persistDraft();markSetup([e.target.id]);friendlyRender();}});
$('pin').addEventListener('change',friendlyRender);
$('speed').addEventListener('input',()=>{$('aimSpeed').value=$('speed').value;persistDraft();});
$('aimSpeed').addEventListener('input',()=>{$('speed').value=$('aimSpeed').value;});
function sensorCards(){
 const sensors=field?.sensors||{};$('sensorCards').replaceChildren(...Object.entries(sensors).map(([name,data])=>{const card=document.createElement('div');card.className='sensor-card';const title=document.createElement('strong');title.textContent=({power:'Power monitor',imu:'Motion sensor',tofFront:'Front range',presence:'Presence'})[name]||name;const status=document.createElement('span');const meta=data?.meta||data||{};status.textContent='Health: '+(meta.health??'unknown')+(typeof data?.valid==='boolean'?' · '+(data.valid?'valid sample':'no valid sample'):'');card.append(title,status);return card;}));
}
if(nativeHost){$('target').value=location.origin;$('target').readOnly=true;document.querySelector('.connection-options').hidden=true;$('gatewayHint').textContent='This gateway: '+location.host;}
const draftKey='gladiator-hardware-draft-v1';
function draftFields(){return ['hz','speed',...['pan','tilt'].flatMap(axis=>['Pin','Low','Neutral','High','ShaftMin','ShaftMax','Min','Max','Reverse'].map(key=>axis+key))];}
function draftData(){return {schema:1,kind:'partial-mast-draft',fields:Object.fromEntries(draftFields().map(id=>[id,$(id).type==='checkbox'?$(id).checked:$(id).value])),lastPulse,pulseHistory};}
function persistDraft(){try{localStorage.setItem(draftKey,JSON.stringify(draftData()));}catch{}}
function restoreDraft(data){
 if(data?.schema!==1||data.kind!=='partial-mast-draft'||!data.fields||typeof data.fields!=='object')throw Error('Not a hardware setup draft');
 for(const id of draftFields()){const value=data.fields[id];if($(id).type==='checkbox'){if(typeof value==='boolean')$(id).checked=value;}else if(typeof value==='string'&&value.length<40)$(id).value=value;}
 for(const pin of [10,13]){const saved=data.pulseHistory?.[pin];if(saved?.pin===pin&&Number.isFinite(saved.pulse)&&saved.pulse>=500&&saved.pulse<=2575)pulseHistory[pin]={pin,pulse:Math.round(saved.pulse),source:'saved command draft',reported:!!saved.reported};}const sample=data.lastPulse;if([10,13].includes(sample?.pin)&&Number.isFinite(sample.pulse)&&sample.pulse>=500&&sample.pulse<=2575){lastPulse={pin:sample.pin,pulse:Math.round(sample.pulse),source:'saved command draft',reported:!!sample.reported};$('pin').value=sample.pin;}
 $('aimSpeed').value=$('speed').value;friendlyRender();
}

try{const saved=localStorage.getItem(draftKey);if(saved){restoreDraft(JSON.parse(saved));notice('Your saved setup draft is restored. Nothing is enabled.');}}catch{}

const emptySetup=Object.fromEntries(draftFields().map(id=>[id,$(id).type==='checkbox'?false:['hz','speed','panPin','tiltPin','panMin','panMax','tiltMin','tiltMax'].includes(id)?({hz:'50',speed:'5',panPin:'10',tiltPin:'13',panMin:'-5',panMax:'5',tiltMin:'-3',tiltMax:'3'})[id]:'']));
let legacySetup=null;
try{const prior=localStorage.getItem('gladiator-pre-robot-setup-v1')||localStorage.getItem(draftKey);if(prior){const d=JSON.parse(prior);if(d?.fields&&['panNeutral','tiltNeutral','panLow','panHigh','tiltLow','tiltHigh'].some(id=>d.fields[id])){legacySetup=d;localStorage.setItem('gladiator-pre-robot-setup-v1',prior);$('migrateEntries').hidden=false;}}}catch{}
let robotSetup=null,setupChain=Promise.resolve(),setupDirty={},setupTimer=null,loadedRobotProfile=false;
function setupValues(){return Object.fromEntries(draftFields().map(id=>[id,$(id).type==='checkbox'?$(id).checked:$(id).value]));}
function showSetupStatus(text){$('robotSetupStatus').textContent=text;}
function applyRobotFields(fields){
 for(const id of draftFields()){if(Object.hasOwn(setupDirty,id)||document.activeElement===$(id)||!Object.hasOwn(fields,id))continue;const value=fields[id];if($(id).type==='checkbox')$(id).checked=value;else $(id).value=value;}
 $('aimSpeed').value=$('speed').value;persistDraft();friendlyRender();
}
async function loadRobotSetup(){
 const gen=generation;const saved=await api('/api/mast/setup',undefined,gen);
 if(gen!==generation)return;
 if(saved?.schema!==1||!Number.isInteger(saved.revision)||!saved.fields)throw Error('Gateway does not support robot setup storage. Update the C6 firmware.');
 robotSetup=saved;
 if(Object.keys(saved.fields).length){applyRobotFields({...emptySetup,...saved.fields});if(m?.profile&&m.calibrated&&!loadedRobotProfile){loadedRobotProfile=true;fillProfile(m.profile);applyRobotFields(saved.fields);}showSetupStatus('Saved on robot · revision '+saved.revision+' · shared by every controller.');}
 else showSetupStatus('No positions saved on this robot yet. Record a position or save the current entries.');
}
function markSetup(ids){
 for(const id of ids)setupDirty[id]=$(id).type==='checkbox'?$(id).checked:$(id).value;
 persistDraft();showSetupStatus(connected?'Saving changes to robot…':'Not saved to robot · reconnect, then Save current entries.');
 clearTimeout(setupTimer);if(connected)setupTimer=setTimeout(()=>guard(()=>flushSetup())(),500);
}
function flushSetup(){
 clearTimeout(setupTimer);
 const fields={...setupDirty},gen=generation;
 if(!connected)throw Error('Connect to save these entries on the robot.');
 if(!Object.keys(fields).length)return Promise.resolve();
 const job=async()=>{
  if(gen!==generation)throw Error('Robot connection changed; entries have not been saved.');
  if(!robotSetup)await loadRobotSetup();
  let saved;
  for(let attempt=0;attempt<2;attempt++){
   try{saved=await api('/api/mast/setup',{revision:robotSetup.revision,fields},gen);break;}
   catch(e){if(e.status!==409||attempt)throw e;robotSetup=await api('/api/mast/setup',undefined,gen);}
  }
  if(gen!==generation)throw Error('Robot connection changed before save confirmation.');
  if(!saved?.fields||!Object.entries(fields).every(([k,v])=>saved.fields[k]===v))throw Error('Robot flash readback did not match the saved entries.');
  robotSetup=saved;
  for(const [id,value] of Object.entries(fields))if(setupDirty[id]===value)delete setupDirty[id];
  applyRobotFields(saved.fields);
  showSetupStatus('Saved on robot · revision '+saved.revision+' · survives reconnect and power-off.');
  log('Robot flash confirmed '+Object.keys(fields).join(', '));
 };
 const task=setupChain.catch(()=>{}).then(job);setupChain=task;
 return task.catch(e=>{showSetupStatus('Not saved to robot · '+e.message);throw e;});
}
$('migrateEntries').onclick=guard(async()=>{if(!legacySetup)throw Error('No earlier entries on this device.');if(!connected)throw Error('Connect to copy these entries to the robot.');restoreDraft(legacySetup);markSetup(draftFields());await flushSetup();notice('Earlier entries from this device copied to robot flash. No movement command sent.');});
$('saveEntries').onclick=guard(async()=>{markSetup(draftFields());await flushSetup();notice('Current entries saved on the robot, including incomplete calibration. Movement is unchanged.');});
function appliedLimits(){
 if(!m?.calibrated||!m.profile){$('appliedLimits').textContent='S3 aiming calibration: not applied yet. Saved centers and endpoints remain on the robot while you finish measuring.';return;}
 const p=m.profile;
 $('appliedLimits').textContent=(fresh()?'S3 enforced travel':'Last reported S3 travel')+': pan '+p.pan.minDeciDeg/10+'° to '+p.pan.maxDeciDeg/10+'° (center '+p.pan.neutralUs+' µs, GPIO'+p.pan.pin+'); tilt '+p.tilt.minDeciDeg/10+'° to '+p.tilt.maxDeciDeg/10+'° (center '+p.tilt.neutralUs+' µs, GPIO'+p.tilt.pin+').';
}
async function movePulse(pulse){
 if(!fresh()||!test?.confirmed||!m.testActive)throw Error('Start and confirm a GPIO test first.');
 if(!Number.isInteger(pulse)||pulse<test.minUs||pulse>test.maxUs)throw Error('Choose a pulse between 500 and 2575 µs.');
 test.target=pulse;test.lastRenew=performance.now();$('pulseTarget').value=pulse;
 try{await write('/api/mast/config',{action:'jog-test',pin:test.pin,pulseUs:pulse},{label:'GPIO'+test.pin+' target '+pulse+' µs'});}catch(e){await allOff();throw e;}render();
}
$('pulseGo').onclick=guard(()=>movePulse(integer('pulseTarget',500,2575)));
$('pulseSlider').oninput=()=>{$('pulseTarget').value=$('pulseSlider').value;};
$('pulseSlider').onchange=guard(()=>movePulse(Number($('pulseSlider').value)));
$('batteryOverride').addEventListener('change',friendlyRender);
$('hz').addEventListener('change',()=>markSetup(['hz']));
$('speed').addEventListener('change',()=>markSetup(['speed']));

function backgroundStop(){stopVideo();if(active||test||enabling)guard(()=>allOff())();}
window.addEventListener('blur',()=>{if(active||enabling)backgroundStop();});window.addEventListener('pagehide',backgroundStop);document.addEventListener('visibilitychange',()=>{if(document.hidden)backgroundStop();});render();

// Staff handoff from the booth page; connecting never starts a servo session.
const operatorLink=new URLSearchParams(location.search);
if(operatorLink.get('manual')==='1')$('batteryOverride').checked=true;
if(['identify','calibrate','operate','sensors'].includes(operatorLink.get('view')))showView(operatorLink.get('view'));
if(operatorLink.get('connect')==='1')$('connect').click();
