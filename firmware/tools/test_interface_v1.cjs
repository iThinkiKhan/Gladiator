// Browser regression tests against in-memory fixtures only. No hardware access.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(process.env.GLADIATOR_NODE_MODULES+'/playwright');
const root=path.resolve(__dirname,'..');
const html=file=>fs.readFileSync(path.join(root,file),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.GLADIATOR_CHROME||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 try {
  const context=await browser.newContext({viewport:{width:390,height:844}}),page=await context.newPage(),errors=[],requests=[];
  page.on('pageerror',e=>errors.push(e.message));
  let connected=true,fail=false,heading=true,sampleAge=10,armed=false,client=0,epoch=123,last=0,denied=false;
  const key='a'.repeat(32),now=()=>Date.now()%10000000;
  const controller=()=>{if(armed&&now()-last>350){armed=false;client=0;++epoch;}return {s3Ms:now(),epoch,client,armed,state:armed?'ARMED':'SAFE',leftPercent:90,rightPercent:90,battery:'WARN',power:50};};
  const meta=()=>({health:'OK',ageMs:sampleAge,rateHz:20,hasSample:true});
  const robot=()=>({controller:controller(),power:{meta:meta(),derived:{busVolts:12.2,currentAmps:.12,watts:1.464}},imu:{meta:meta(),derived:{valid:true,headingValid:heading,yawDeg:42,rollDeg:1,pitchDeg:2}},tofFront:{meta:meta(),derived:{valid:true,nearestMm:900,usableZones:64}},presence:{meta:meta(),derived:{presenceValid:true,present:false,targetValid:false}}});
  await context.route('http://gladiator.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());let body,status=200,type='application/json';
   if(req.method()==='POST')requests.push({path:url.pathname,body:req.postData(),key:req.headers()['x-gladiator-key']});
   if(url.pathname==='/'||url.pathname==='/field'){body=html('shared/FieldPage.h');type='text/html';}
   else if(url.pathname==='/dev'){body=html('gateway/src/GatewayPage.h');type='text/html';}
   else if(url.pathname==='/service'){body=html('shared/ServicePage.h');type='text/html';}
   else if(fail){status=503;body='unavailable';}
   else if(url.pathname==='/api/field')body=JSON.stringify({requiresKey:true,connected,controller:controller(),sensors:robot(),transport:'WIFI',telemetryAgeMs:8,powerValid:sampleAge<500,volts:12.2});
   else if(url.pathname==='/api/robot')body=JSON.stringify({gateway:{robotConnected:connected,transport:'wifi',firmware:'C6 fixture',telemetryAgeMs:8,apSsid:'Gladiator-Gateway'},robot:robot(),native:{tofFront:{raw:{distanceMm:Array.from({length:64},(_,i)=>i+500)}}},nativeAgeMs:1200,system:{firmware:'S3 fixture',resetReason:'POWERON'},systemAgeMs:1400,logs:'[1] Sensors | sample\n[2] SAFE <script>alert(1)</script>\n[3] Link WIFI',logAgeMs:1500});
   else if(url.pathname==='/api/control'){
    const c=req.postDataJSON();
    if(denied||req.headers()['x-gladiator-key']!==key){status=401;body='key required';}
    else {if(connected&&c.epoch===epoch){if(c.operation===1&&!armed){armed=true;client=c.client;}if(c.operation===0){armed=false;client=0;++epoch;}last=now();}body='{"queued":true}';}
   }else if(url.pathname==='/api/status')body=JSON.stringify({state:'SAFE',battery:'BENCH',powerSource:'BENCH',resetReason:'POWERON'});
   else if(url.pathname==='/api/logs'){body='Retained fault evidence';type='text/plain';}
   else body='{"queued":true}';
   await route.fulfill({status,contentType:type,body});
  });
  await page.goto('http://gladiator.test/');await page.waitForFunction(()=>document.querySelector('#heading').textContent==='42°');
  assert.equal(await page.locator('#state').textContent(),'SAFE','display S3 state even when motor fields disagree');
  assert.equal(await page.locator('#range').textContent(),'900 mm');assert.equal(await page.locator('#presence').textContent(),'CLEAR');
  assert.match(await page.locator('#warnings').textContent(),/Battery WARN/);assert.equal(await page.locator('#transport').textContent(),'WIFI');
  assert(!await page.locator('#dev').isVisible(),'development deliberately hidden in diagnostics');
  assert.equal(requests.length,0,'opening field never sends control');
  sampleAge=600;await page.waitForFunction(()=>document.querySelector('#heading').textContent==='—');assert.equal(await page.locator('#range').textContent(),'900 mm','500 ms range topics have a suitable freshness budget');
  heading=false;sampleAge=2000;await page.waitForFunction(()=>document.querySelector('#range').textContent==='—');
  assert.equal(await page.locator('#heading').textContent(),'—');assert.equal(await page.locator('#presence').textContent(),'UNKNOWN');
  connected=false;await page.waitForFunction(()=>document.querySelector('#state').textContent==='UNKNOWN');assert.equal(await page.locator('#power').textContent(),'—');assert(await page.locator('#arm').isDisabled());
  connected=true;sampleAge=10;heading=true;await page.waitForFunction(()=>!document.querySelector('#arm').disabled);
  await page.locator('#keyBox summary').click();await page.locator('#key').fill(key);denied=true;await page.locator('#arm').click();await page.waitForFunction(()=>document.querySelector('#reply').textContent.includes('interrupted'));assert(!armed);denied=false;
  await page.screenshot({path:path.join(root,'diagnostics/interface-v1-field.png'),fullPage:true});
  await page.setViewportSize({width:1280,height:900});await page.goto('http://gladiator.test/dev');await page.waitForFunction(()=>document.querySelector('#live').textContent==='Connected');
  assert.deepEqual(await page.locator('[role=tab]').allTextContents(),['Overview','Controller','Power','IMU','Range','Presence','Communications','System','Logs','Firmware']);
  assert.equal(await page.locator('#state').textContent(),'SAFE');
  await page.getByText('Maintenance access',{exact:true}).click();await page.locator('#key').fill(key);
  await page.locator('[data-tab=range]').click();await page.locator('#range [data-inspect]').click();await page.waitForFunction(()=>document.querySelector('#detailReply').textContent.includes('queued'));
  assert(requests.some(r=>r.path==='/api/subscribe'&&JSON.parse(r.body).leaseMs===30000&&r.key===key));
  await page.getByText('Native 8 × 8 range grid (mm, device order)',{exact:true}).click();assert.equal(await page.locator('#rangeGrid > div').count(),64);assert.match(await page.locator('#gridAge').textContent(),/1200 ms/);
  await page.locator('[data-tab=logs]').click();assert(!((await page.locator('#logText').textContent()).includes('Sensors |')));await page.locator('#logFilter').fill('SAFE');assert.match(await page.locator('#logText').textContent(),/<script>/);assert(!((await page.locator('#logText').textContent()).includes('Link WIFI')));
  await page.locator('[data-tab=controller]').click();await page.locator('#openControls').click();const frame=page.frameLocator('#testControls');await frame.locator('#arm').waitFor();
  await page.waitForFunction(()=>document.querySelector('#testControls').contentDocument.querySelector('#key').value==='a'.repeat(32));
  await frame.locator('#diagnostics > summary').click();await frame.locator('#trackTest > summary').click();await frame.locator('#testLeft').fill('20');await frame.locator('#testRight').fill('-30');
  await frame.locator('#arm').click();await page.waitForFunction(()=>!document.querySelector('#testControls').contentDocument.querySelector('[data-drive=test]').disabled);
  await frame.locator('[data-drive=test]').hover();await page.mouse.down();await page.waitForTimeout(180);await page.mouse.up();
  assert(requests.some(r=>r.path==='/api/control'&&JSON.parse(r.body).deadman===1&&JSON.parse(r.body).left===20&&JSON.parse(r.body).right===-30),'independent tracks use existing typed controls');
  await frame.locator('#speed').selectOption('255');await frame.locator('[data-drive=f]').hover();await page.mouse.down();await page.waitForTimeout(180);await page.mouse.up();assert(requests.some(r=>r.path==='/api/control'&&JSON.parse(r.body).overdrive===1&&JSON.parse(r.body).power===100&&JSON.parse(r.body).left===100&&JSON.parse(r.body).right===100),'Overdrive preserves the established full-duty command');
  await page.locator('[data-tab=overview]').click();await page.waitForTimeout(500);assert(!armed,'leaving Controller ends the test session');
  await page.screenshot({path:path.join(root,'diagnostics/interface-v1-dev.png'),fullPage:true});
  fail=true;await page.waitForFunction(()=>document.querySelector('#live').textContent==='Gateway unreachable');assert.equal(await page.locator('#state').textContent(),'UNKNOWN');assert.equal(await page.locator('#volts').textContent(),'—');assert(!((await page.locator('#values-tofFront').textContent()).includes('900')));fail=false;
  await page.goto('http://gladiator.test/service');await page.waitForFunction(()=>document.querySelector('#state').textContent==='SAFE');assert.equal(await page.locator('[data-drive]').count(),0);assert.equal(await page.locator('input[name=firmware]').count(),1);
  assert(!await page.getByText('Open recovery field controls',{exact:true}).isVisible());await page.getByText('Retained S3 log',{exact:true}).click();await page.locator('#readLogs').click();await page.waitForFunction(()=>document.querySelector('#logs').textContent==='Retained fault evidence');
  await page.screenshot({path:path.join(root,'diagnostics/interface-v1-service.png'),fullPage:true});
  for(const url of ['/','/dev','/service']){await page.setViewportSize({width:320,height:720});await page.goto('http://gladiator.test'+url);await page.waitForTimeout(200);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),url+' fits narrow viewport');}
  assert.deepEqual(errors,[]);console.log('Interface v1 passed: authoritative state, health/validity, stale/offline, auth failure, ten tabs, native grid, logs, subscriptions, embedded track control, tab disarm, service, narrow layouts.');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
