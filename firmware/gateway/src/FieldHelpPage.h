#pragma once
// Field troubleshooting page (/help). Read-only: it polls the same key-free
// status endpoints as the field page and never sends a robot command.
static const char FIELD_HELP_PAGE[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gladiator troubleshoot</title><style>
:root{--bg:#101418;--card:#1a2027;--line:#2c343d;--text:#e8edf2;--dim:#9aa6b2;--ok:#2fbf71;--warn:#e9b13b;--bad:#ef5350;--off:#6b7785}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}h1{font-size:21px;margin:4px 0 2px}h2{font-size:16px;margin:22px 0 8px}
a{color:#7cc4ff}.sub{color:var(--dim);margin:0 0 12px}.card{background:var(--card);border:1px solid var(--line);border-radius:10px}
.row{display:grid;grid-template-columns:14px 92px 1fr;gap:10px;align-items:start;padding:10px 12px;border-top:1px solid var(--line)}
.row:first-child{border-top:0}.dot{width:12px;height:12px;border-radius:50%;margin-top:4px;background:var(--off)}
.ok .dot{background:var(--ok)}.warn .dot{background:var(--warn)}.bad .dot{background:var(--bad)}
.name{font-weight:600}.val{font-weight:600}.fix{color:var(--dim);font-size:14px;margin-top:2px}.bad .val{color:var(--bad)}.warn .val{color:var(--warn)}
#banner{padding:12px;border-radius:10px;margin:10px 0;font-weight:600;background:#22303c}#banner.bad{background:#4a1f22}#banner.warn{background:#463a1c}#banner.ok{background:#1c3a2a}
details{background:var(--card);border:1px solid var(--line);border-radius:10px;margin:8px 0;padding:0 12px}summary{cursor:pointer;padding:11px 0;font-weight:600}
details ol,details ul{margin:0 0 12px;padding-left:20px}li{margin:3px 0}code{background:#0b0e11;padding:1px 5px;border-radius:4px}
.small{color:var(--dim);font-size:13px}
</style></head><body><main>
<h1>Gladiator troubleshoot</h1><p class="sub"><a href="/">&larr; Drive page</a> &middot; refreshes every 2&nbsp;s &middot; read-only, sends no commands</p>
<div id="banner">Checking&hellip;</div>
<div class="card" id="checks"></div>
<p class="small" id="stamp"></p>

<h2>If something is wrong</h2>
<details open><summary>Emergency: robot moving when it should not</summary><ol>
<li>Press <b>STOP ALL</b> on the drive page (or Escape on a keyboard).</li>
<li>If it does not stop at once: switch the <b>battery off</b>.</li>
<li>Tracks and mast also stop on their own if the controller page is closed, the link goes stale, or the controller restarts. Nothing restarts by itself afterwards.</li></ol></details>

<details><summary>Brain OFFLINE, or keeps restarting (boot loop)</summary><ol>
<li>Wait 20&nbsp;s. Firmware v13 clears a jammed sensor (I2C) bus by itself; one restart is normal after a jam.</li>
<li>Still restarting: <b>full power-off</b>. Battery off <i>and</i> every USB unplugged, wait 10&nbsp;s, power on. A restart on its own does not power the sensors down.</li>
<li>After 3 jams in a row the S3 stays up <b>without sensors</b> (power/IMU/LiDAR show OFFLINE) instead of looping. Driving still works, but the battery is then <b>not monitored</b>. Power-cycle to bring sensors back.</li>
<li>Laptop on the UART cable (CH340, 115200 baud): look for <code>SENSOR WATCHDOG stage=N</code> (1 power, 2 IMU, 3 LiDAR, 4 radar) and <code>I2C bus stuck</code> lines.</li></ol></details>

<details><summary>Eyes flashing / alert face</summary><ul>
<li><b>Battery</b>: pack low. Stop driving and charge. The motors lock out below 13.2&nbsp;V (4S).</li>
<li><b>Proximity</b>: something is within 50&nbsp;cm of the LiDAR <i>while armed or driving</i>. Parked (SAFE), people walking up do not trigger it.</li>
<li><b>Expression</b>: someone picked &ldquo;Alert&rdquo; on the drive page. It ends by itself after 30&nbsp;s, or choose Automatic.</li>
<li>OFFLINE face: the display has lost the brain; see the boot-loop steps above.</li></ul></details>

<details><summary>Sensor shows OFFLINE or STALE</summary><ul>
<li>Power monitor OFFLINE: battery protection is off; check the battery voltage by hand before driving.</li>
<li>IMU / LiDAR OFFLINE: they retry every 5&nbsp;s. If they stay offline, do a full power-off (above).</li>
<li>Radar OFFLINE is expected; it has not been working.</li>
<li>Do not touch the I2C wires with the robot powered.</li></ul></details>

<details><summary>Can't open this page / lost connection</summary><ul>
<li>On the venue router (Opal, <b>SOIT-Field</b>): <a href="http://192.168.8.1/help">http://192.168.8.1/help</a></li>
<li>Router down: join the robot's own Wi-Fi <b>Gladiator-Gateway</b>, then open <a href="http://192.168.8.1/help">http://192.168.8.1/help</a>. Works with no router or internet.</li>
<li>Weak signal (below about &minus;80&nbsp;dBm): move the gateway antenna clear of metal.</li></ul></details>

<details><summary>Camera: no video</summary><ul>
<li>The camera allows <b>one viewer</b>. Close other tabs or phones showing the video, then Start video again.</li>
<li>Camera &ldquo;not detected&rdquo;: check its ribbon cable and power, then restart it.</li>
<li>Unreachable: the camera must be on SOIT-Field (saved address shown above).</li></ul></details>

<details><summary>Robot won't drive</summary><ul>
<li>Battery LOCKOUT: charge the pack.</li>
<li>Only one controller can drive at a time; close other drive pages.</li>
<li>Arm first, and keep the drive page visible and the phone awake.</li>
<li>Brain OFFLINE: see the boot-loop steps above.</li></ul></details>

<p class="small">Pages: <a href="/">Drive</a> &middot; <a href="/booth/">Booth routines</a> &middot; <a href="/hardware-test">Hardware setup</a> &middot; <a href="/dev">Diagnostics</a></p>
</main><script>
const H={OK:'ok',DEGRADED:'warn',WAIT:'warn',STALE:'warn',NO_POSE:'warn',OFFLINE:'bad',UNKNOWN:'off'};
const FIX={power:'Battery protection is off while this is down. Full power-off if it stays offline.',imu:'Retries every 5 s. Full power-off if it stays offline.',
lidar:'Retries every 5 s. Full power-off if it stays offline.',radar:'Expected: not in service.'};
async function j(u){const r=await fetch(u,{cache:'no-store'});if(!r.ok)throw new Error(r.status);return r.json();}
function row(cls,name,val,fix){return `<div class="row ${cls}"><span class="dot"></span><span class="name">${name}</span><div><div class="val">${val}</div>${fix?`<div class="fix">${fix}</div>`:''}</div></div>`;}
function esc(s){return String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
let cam=null,camAt=0,camFails=0;
async function tick(){
 let v=null,s=null;try{v=await j('/api/visual');}catch(e){}try{s=await j('/api/status');}catch(e){}
 if(Date.now()-camAt>8000){camAt=Date.now();j('/api/camera/status').then(x=>{cam=x;camFails=0;}).catch(()=>{if(++camFails>=2||!cam)cam={error:true};});}
 const out=[],d=(v&&v.diagnostics)||{};let worst='ok',msg='All checks OK.';
 const bad=(m)=>{worst='bad';msg=m;},warn=(m)=>{if(worst!=='bad'){worst='warn';msg=m;}};
 if(!v&&!s){document.getElementById('banner').className='bad';document.getElementById('banner').textContent='Gateway not answering. Check you are on SOIT-Field or Gladiator-Gateway Wi-Fi, then reload.';return;}
 const brain=d.brain==='OK';
 if(d.restarting){bad('Brain is restarting repeatedly: follow "keeps restarting" below.');out.push(row('bad','Brain','RESTARTING ('+d.restarts+' restarts)','Wait 20 s, then do a full power-off: battery off and all USB unplugged for 10 s.'));}
 else if(!brain){bad('Brain (S3) offline: follow "Brain OFFLINE" below.');out.push(row('bad','Brain','OFFLINE','Not answering the gateway. Wait 20 s; then do a full power-off.'));}
 else out.push(row('ok','Brain','OK &middot; state '+esc(v.state)+(d.restarts?' &middot; '+d.restarts+' restart(s) since gateway boot':''),''));
 const b=v&&v.battery,volts=v&&v.volts!=null?v.volts.toFixed(2)+' V':'no reading';
 if(brain){const c=b==='OK'||b==='BENCH'?'ok':b==='WARN'||b==='UNKNOWN'?'warn':'bad';
  if(c==='bad')bad('Battery '+b+': stop and charge.');else if(b==='WARN')warn('Battery getting low: plan to charge.');else if(c==='warn')warn('No battery reading: check the pack voltage by hand.');
  out.push(row(c,'Battery',esc(b)+' &middot; '+volts,c==='ok'?'':b==='UNKNOWN'?'Power monitor not reading; battery protection is off.':'Motors lock out at 13.2 V. Charge before driving more.'));}
 for(const k of ['power','imu','lidar','radar']){if(!brain)break;const h=d[k]||'UNKNOWN',c=k==='radar'&&h==='OFFLINE'?'off':(H[h]||'off');
  if(c==='bad')warn((k==='power'?'Power monitor':k.toUpperCase())+' offline.');
  out.push(row(c,{power:'Power mon.',imu:'IMU',lidar:'LiDAR',radar:'Radar'}[k],esc(h)+(k==='lidar'&&v.nearestMm!=null?' &middot; nearest '+v.nearestMm+' mm':''),c==='ok'?'':FIX[k]));}
 if(brain){out.push(row(d.stop&&d.stop!=='NONE'?'warn':'ok','Mast',esc(d.mast)+(d.stop&&d.stop!=='NONE'?' &middot; last stop '+esc(d.stop):''),''));
  out.push(row('ok','Link',esc(d.link),''));}
 if(v){const m=v.mode,c=m==='alert'?'warn':m==='offline'?'bad':'ok';
  const why={BATTERY:'battery low',PROXIMITY:'object within 50 cm while armed',EXPRESSION:'chosen on drive page'}[v.alertReason]||'';
  out.push(row(c,'Eyes',esc(m)+(why?' &middot; '+why:'')+(v.override?' (temporary expression)':''),m==='alert'?'See "Eyes flashing" below.':''));}
 if(s){const r=s.rssi,c=!s.wifiConnected?'warn':r<-80?'warn':'ok';
  out.push(row(c,'Router',s.wifiConnected?esc(s.wifiSsid)+' &middot; '+r+' dBm &middot; '+esc(s.ip):'not joined (robot Wi-Fi still works)',c==='ok'?'':!s.wifiConnected?'Use Gladiator-Gateway Wi-Fi, http://192.168.8.1/help':'Weak: move the gateway antenna clear of metal.'));
  out.push(row('ok','Gateway',esc(s.firmware)+' &middot; up '+Math.round(s.uptimeMs/60000)+' min',''));}
 if(cam){const ok=!cam.error&&cam.cameraReady;
  const at=v&&v.cameraIp?' &middot; '+esc(v.cameraIp):'';
  out.push(row(ok?'ok':'warn','Camera',(cam.error?'not reachable':ok?'ready'+(cam.streaming?' &middot; streaming':' &middot; idle'):cam.cameraError?'camera not detected':'not ready')+at,ok?'':'See "Camera: no video" below.'));}
 const bn=document.getElementById('banner');bn.className=worst;bn.textContent=msg;
 document.getElementById('checks').innerHTML=out.join('');document.getElementById('stamp').textContent='Updated '+new Date().toLocaleTimeString();
}
tick();setInterval(tick,2000);
</script></body></html>)HTML";
