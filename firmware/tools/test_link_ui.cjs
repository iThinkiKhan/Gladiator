// Isolated browser test with simulated S3 responses. Never connects to hardware.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require(process.env.GLADIATOR_NODE_MODULES+'/playwright');
const root=path.resolve(__dirname,'..');
const html=file=>fs.readFileSync(path.join(root,file),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.GLADIATOR_CHROME||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844},deviceScaleFactor:1});const errors=[];page.on('pageerror',e=>errors.push(e.message));
  let connected=true,epoch=123,armed=false,client=0,lastCommand=0;const commands=[];
  const now=()=>Date.now()%10000000;
  const meta={health:'OK',online:true,hasSample:true,ageMs:10,rateHz:20};
  const controller=()=>({s3Ms:now(),epoch,client,armed,deadman:armed,state:armed?'ARMED':'SAFE',owner:armed?3:0,leftPercent:0,rightPercent:0});
  const robot=()=>({controller:controller(),power:{meta,derived:{busVolts:12.2,currentAmps:.12}},imu:{meta,derived:{valid:true,rollDeg:1,pitchDeg:2,yawDeg:3}},tofFront:{meta:{...meta,health:'OFFLINE'},derived:{}},presence:{meta:{...meta,health:'OFFLINE'},derived:{}}});
  await page.route('http://gladiator.test/**',async route=>{
   const url=new URL(route.request().url());let body,type='application/json';
   if(url.pathname==='/'){body=html('shared/FieldPage.h');type='text/html';}
   else if(url.pathname==='/dev'){body=html('gateway/src/GatewayPage.h');type='text/html';}
   else if(url.pathname==='/api/field'){
    if(armed&&now()-lastCommand>350){armed=false;client=0;++epoch;}
    body=JSON.stringify({sensors:robot(),transport:'UART',telemetryAgeMs:5,requiresKey:true,connected,interface:'C6 field · UART to S3',controller:controller(),powerValid:true,volts:12.2,amps:.12});
   }else if(url.pathname==='/api/control'){
    const c=route.request().postDataJSON();commands.push(c);
    if(c.epoch===epoch&&connected){if(c.operation===1&&!armed){armed=true;client=c.client;}if(c.operation===0){armed=false;client=0;++epoch;}lastCommand=now();}
    body=JSON.stringify({queued:true});
   }else if(url.pathname==='/api/robot')body=JSON.stringify({gateway:{robotConnected:connected,transport:'uart',uartAlive:true,uartStable:true,wifiLinkAlive:false,telemetryAgeMs:5,firmware:'gateway-2.0.0',apSsid:'Gladiator-Gateway'},robot:robot(),native:null,system:null,nativeAgeMs:null});
   else body='{"queued":true}';
   await route.fulfill({status:200,contentType:type,body});
  });
  await page.goto('http://gladiator.test/');await page.waitForFunction(()=>document.querySelector('#arm').disabled===false);
  await page.screenshot({path:path.join(root,'diagnostics/link-field-phone.png'),fullPage:true});
  await page.locator('#arm').click();await page.waitForFunction(()=>!document.querySelector('[data-drive=f]').disabled);
  const forward=page.locator('[data-drive=f]');await forward.hover();await page.mouse.down();await sleep(250);await page.mouse.up();await sleep(180);
  assert(commands.some(c=>c.operation===2&&c.deadman===1&&c.left===25&&c.right===25),'hold sends movement');
  assert(commands.some(c=>c.operation===2&&c.deadman===0),'release sends coast');
  const armCount=commands.filter(c=>c.operation===1).length;
  connected=false;armed=false;client=0;++epoch;await sleep(500);connected=true;await sleep(400);
  assert.equal(commands.filter(c=>c.operation===1).length,armCount,'recovery never auto-arms');
  assert(await forward.isDisabled(),'drive disabled after recovered link');
  const after=commands.length;await sleep(250);assert.equal(commands.length,after,'expired session sends no keepalive/movement');
  await page.locator('#arm').click();await page.waitForFunction(()=>!document.querySelector('[data-drive=f]').disabled);
  assert.equal(commands.filter(c=>c.operation===1).length,armCount+1,'explicit arm resumes new session');
  await page.locator('#stop').click();await sleep(450);assert(!armed,'disarm or watchdog ends session');
  await page.setViewportSize({width:1280,height:900});await page.goto('http://gladiator.test/dev');await page.waitForFunction(()=>document.querySelector('#live').textContent==='Connected');
  for(const tab of ['controller','power','imu','range','presence','communications','system','logs','firmware','overview']){await page.locator(`[data-tab=${tab}]`).click();assert(await page.locator('#'+tab).isVisible());}
  await page.screenshot({path:path.join(root,'diagnostics/link-development-desktop.png'),fullPage:true});
  assert.deepEqual(errors,[]);console.log('UI tests passed: phone hold/release, explicit arming, no auto-resume, stop, development tabs, missing native data and browser error checks.');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
