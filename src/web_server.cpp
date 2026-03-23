/*
 * @file web_server.cpp
 * @brief WiFi Soft-AP + HTTP server with SSE
 *        3D hand visualization, fixed MP3 audio serving from LittleFS.
 */
#include "web_server.h"
#include "config.h"
#include "sensors.h"
#include "sensor_module/sensor_module.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ctype.h>

extern bool    cfg_local_speech;
extern uint8_t cfg_local_voice;
extern bool    cfg_local_sensors;

static WebServer server(WEB_SERVER_PORT);
static bool      running    = false;
static bool      fs_mounted = false;
static uint16_t  s_adc_max  = 4095;

static const char PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Signa</title>
<style>
:root{--bg:#07090f;--bg1:#0d1020;--bg2:#131829;--bg3:#1a2035;--bd:rgba(255,255,255,.08);--bd2:rgba(255,255,255,.14);--txt:#e8efff;--txt2:#8fa3cc;--txt3:#5a6e96;--acc:#38bdf8;--grn:#4ade80;--red:#f87171;--cyn:#22d3ee;--r:14px}
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;background:var(--bg);color:var(--txt);font-family:system-ui,sans-serif;display:flex;justify-content:center;padding:10px 10px 28px}
.wrap{width:100%;max-width:780px;display:flex;flex-direction:column;gap:10px}
.hdr{background:var(--bg1);border:1px solid var(--bd);border-radius:var(--r);padding:12px 16px;display:flex;align-items:center;justify-content:space-between;gap:10px}
.hdr-l{display:flex;align-items:center;gap:10px}
.logo{width:34px;height:34px;background:linear-gradient(135deg,#1e40af,#0ea5e9);border-radius:9px;display:flex;align-items:center;justify-content:center;font-size:17px;flex-shrink:0}
.hdr h1{font-size:.95rem;font-weight:600;color:var(--acc)}
.hdr-sub{font-size:.68rem;color:var(--txt3);margin-top:1px}
.pill{display:flex;align-items:center;gap:5px;padding:4px 9px;border-radius:99px;background:var(--bg3);border:1px solid var(--bd);font-size:.68rem;color:var(--txt2)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--red);transition:.3s}
.dot.on{background:var(--grn);box-shadow:0 0 7px rgba(74,222,128,.5)}
.hero{background:var(--bg1);border:1px solid var(--bd);border-radius:var(--r);padding:22px 16px;text-align:center}
.g-lbl{font-size:.68rem;letter-spacing:2px;text-transform:uppercase;color:var(--txt3);margin-bottom:7px}
#gesture{font-size:3rem;font-weight:600;color:var(--grn);min-height:58px;line-height:1.1;transition:color .2s}
#gesture.idle{color:var(--txt3)}
.cbar-w{margin:9px auto 0;width:160px;height:3px;background:rgba(255,255,255,.07);border-radius:99px;overflow:hidden}
.cbar{height:100%;width:0%;background:linear-gradient(90deg,var(--cyn),var(--grn));border-radius:99px;transition:width .15s}
.cval{font-size:.68rem;color:var(--txt3);margin-top:4px}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}
@media(max-width:480px){.stats{grid-template-columns:repeat(2,1fr)}}
.stat{background:var(--bg2);border:1px solid var(--bd);border-radius:10px;padding:9px 11px}
.sv{font-size:1.05rem;font-weight:600;color:var(--txt);font-variant-numeric:tabular-nums;font-family:monospace}
.sl{font-size:.62rem;color:var(--txt3);margin-top:2px}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media(max-width:540px){.cols{grid-template-columns:1fr}}
.card{background:var(--bg1);border:1px solid var(--bd);border-radius:var(--r);padding:13px 15px}
.ctitle{font-size:.68rem;letter-spacing:1.4px;text-transform:uppercase;color:var(--txt3);margin-bottom:11px}
.hcard{background:var(--bg1);border:1px solid var(--bd);border-radius:var(--r);padding:9px 15px 13px}
#hcanvas{width:100%;height:250px;border-radius:9px;cursor:grab;display:block}
#hcanvas:active{cursor:grabbing}
.hleg{display:flex;gap:8px;flex-wrap:wrap;margin-top:7px}
.hli{display:flex;align-items:center;gap:4px;font-size:.63rem;color:var(--txt2)}
.hld{width:7px;height:7px;border-radius:50%}
.crow{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:9px 11px;border:1px solid var(--bd);border-radius:9px;background:var(--bg2);margin-bottom:6px}
.ci strong{font-size:.82rem;font-weight:500;color:var(--txt)}
.ci span{font-size:.67rem;color:var(--txt3);display:block;margin-top:1px}
.tog{width:42px;height:22px;background:var(--bg3);border:1px solid var(--bd2);border-radius:99px;cursor:pointer;position:relative;transition:.2s;flex-shrink:0}
.tog.on{background:#16a34a;border-color:#22c55e}
.tog::after{content:'';position:absolute;top:3px;left:3px;width:16px;height:16px;background:#e2e8f0;border-radius:50%;transition:.2s;box-shadow:0 1px 3px rgba(0,0,0,.4)}
.tog.on::after{left:23px}
select.vsel{padding:5px 9px;background:var(--bg3);color:var(--txt);border:1px solid var(--bd2);border-radius:7px;font-size:.78rem;cursor:pointer;outline:none}
.astat{display:flex;align-items:center;gap:7px;padding:7px 11px;border-radius:8px;background:rgba(56,189,248,.06);border:1px solid rgba(56,189,248,.15);font-size:.72rem;color:var(--acc);margin-bottom:8px}
.astat.err{background:rgba(248,113,113,.06);border-color:rgba(248,113,113,.2);color:var(--red)}
.astat.ok{background:rgba(74,222,128,.06);border-color:rgba(74,222,128,.2);color:var(--grn)}
.brow{display:flex;gap:7px;flex-wrap:wrap}
button{padding:8px 13px;border:1px solid var(--bd2);background:var(--bg2);color:var(--txt);border-radius:8px;cursor:pointer;font-size:.76rem;font-weight:500;transition:.15s}
button:hover{background:var(--bg3);border-color:var(--acc)}
button:active{transform:scale(.97)}
button.p{background:linear-gradient(135deg,#1e40af,#0ea5e9);border:none;color:#fff}
button.p:hover{opacity:.9}
.sgrid{display:grid;grid-template-columns:repeat(5,1fr);gap:5px}
.sw{display:flex;flex-direction:column;align-items:center;gap:3px}
.so{width:100%;height:88px;background:rgba(255,255,255,.04);border:1px solid var(--bd);border-radius:7px;position:relative;overflow:hidden}
.si{position:absolute;bottom:0;width:100%;transition:height .12s;border-radius:7px}
.si.fx{background:linear-gradient(0deg,#1d4ed8,#38bdf8)}
.si.hl{background:linear-gradient(0deg,#7c3aed,#c084fc)}
.slbl{font-size:.6rem;color:var(--txt3)}
.sval{font-size:.58rem;color:var(--txt2);font-family:monospace}
.igrid{display:grid;grid-template-columns:1fr 1fr;gap:7px}
.ic{background:var(--bg2);border:1px solid var(--bd);border-radius:9px;padding:8px 10px}
.ict{font-size:.6rem;color:var(--txt3);margin-bottom:3px}
.icv{font-family:monospace;font-size:.76rem;color:var(--txt);line-height:1.55;white-space:pre}
.footer{text-align:center;font-size:.63rem;color:var(--txt3);padding:5px}
</style>
</head>
<body>
<div class="wrap">
<header class="hdr">
  <div class="hdr-l">
    <div class="logo">&#9996;</div>
    <div><h1>Signa Glove &middot; Dashboard</h1><div class="hdr-sub">ESP32-S3 &middot; LittleFS Audio &middot; SSE</div></div>
  </div>
  <div class="pill"><div class="dot" id="dot"></div><span id="ctxt">Connecting</span></div>
</header>
<div class="hero">
  <div class="g-lbl">Detected Sign</div>
  <div id="gesture" class="idle">---</div>
  <div class="cbar-w"><div class="cbar" id="cbar"></div></div>
  <div class="cval" id="cval">Confidence: --</div>
</div>
<div class="stats">
  <div class="stat"><div class="sv" id="sf">0</div><div class="sl">Frames</div></div>
  <div class="stat"><div class="sv" id="shz">--</div><div class="sl">Stream Hz</div></div>
  <div class="stat"><div class="sv" id="sfx">--</div><div class="sl">Flex avg</div></div>
  <div class="stat"><div class="sv" id="shl">--</div><div class="sl">Hall avg</div></div>
</div>
<div class="cols">
  <div class="hcard">
    <div class="ctitle">3D Hand View <span style="font-size:.6rem;color:var(--txt3)">(drag to rotate)</span></div>
    <canvas id="hcanvas"></canvas>
    <div class="hleg" id="hleg">
      <div class="hli"><div class="hld" style="background:#38bdf8"></div>Thumb</div>
      <div class="hli"><div class="hld" style="background:#4ade80"></div>Index</div>
      <div class="hli"><div class="hld" style="background:#f59e0b"></div>Middle</div>
      <div class="hli"><div class="hld" style="background:#c084fc"></div>Ring</div>
      <div class="hli"><div class="hld" style="background:#f87171"></div>Pinky</div>
    </div>
  </div>
  <div class="card" style="display:flex;flex-direction:column">
    <div class="ctitle">Settings</div>
    <div class="crow">
      <div class="ci"><strong>&#128266; Audio</strong><span>MP3 from LittleFS</span></div>
      <div class="tog" id="ta" onclick="togAudio()"></div>
    </div>
    <div class="crow">
      <div class="ci"><strong>&#128202; Sensors</strong><span>Show bars</span></div>
      <div class="tog" id="ts" onclick="togSensors()"></div>
    </div>
    <div class="crow">
      <div class="ci"><strong>&#128100; Voice</strong><span>Audio folder</span></div>
      <select class="vsel" id="vs" onchange="chgVoice()">
        <option value="0">Boy</option>
        <option value="1">Girl</option>
      </select>
    </div>
    <div class="crow">
      <div class="ci"><strong>&#128400; 3D Hand</strong><span>Toggle view</span></div>
      <div class="tog on" id="th" onclick="tog3D()"></div>
    </div>
    <div class="astat" id="ast">&#9208; Audio idle</div>
    <div class="brow">
      <button class="p" onclick="testAudio()">&#128266; Test</button>
      <button onclick="location.reload()">&#8635; Reload</button>
    </div>
  </div>
</div>
<div id="ssec" style="display:none;flex-direction:column;gap:9px">
  <div class="card"><div class="ctitle">Flex Sensors</div><div class="sgrid" id="fb"></div></div>
  <div class="card"><div class="ctitle">Hall Sensors</div><div class="sgrid" id="hb"></div></div>
  <div class="card">
    <div class="ctitle">IMU</div>
    <div class="igrid">
      <div class="ic"><div class="ict">Pitch / Roll</div><div class="icv" id="ipr">--</div></div>
      <div class="ic"><div class="ict">Accel m/s&#178;</div><div class="icv" id="iacc">--</div></div>
      <div class="ic"><div class="ict">Gyro &#176;/s</div><div class="icv" id="igyr">--</div></div>
      <div class="ic"><div class="ict">|a| / |g|</div><div class="icv" id="imag">--</div></div>
    </div>
  </div>
</div>
<div class="footer">Signa v4 &middot; ESP32-S3</div>
</div>
<script src="/web/three.min.js"></script>
<script>
const FN=['Thumb','Index','Middle','Ring','Pinky'];
const FC=[0x38bdf8,0x4ade80,0xf59e0b,0xc084fc,0xf87171];
const D2R=Math.PI/180;

let audioOn=false,sensOn=false,hand3D=true,voice='0';
let curAudio=null,lastG='',frames=0,lastTs=0;
let fPct=[0,0,0,0,0];
let imuPitch=0,imuRoll=0,imuYaw=0,imuGz=0;

function mkBars(id,cls){
  const c=document.getElementById(id);if(!c)return;
  FN.forEach(f=>{
    c.insertAdjacentHTML('beforeend',
      '<div class="sw"><div class="so"><div class="si '+cls+'" id="'+id+f+'" style="height:0%"></div></div>'
      +'<div class="slbl">'+f[0]+'</div><div class="sval" id="'+id+'v'+f+'">0</div></div>');
  });
}
mkBars('fb','fx');mkBars('hb','hl');

function loadSettings(){
  fetch('/api/settings').then(r=>r.json()).then(d=>{
    audioOn=!!d.speech;sensOn=!!d.sensors;voice=String(d.voice||0);
    document.getElementById('ta').classList.toggle('on',audioOn);
    document.getElementById('ts').classList.toggle('on',sensOn);
    document.getElementById('vs').value=voice;
    showSens(sensOn);
  }).catch(()=>{});
}
function saveSettings(){
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({speech:audioOn,voice:+voice,sensors:sensOn})}).catch(()=>{});
}
function togAudio(){audioOn=!audioOn;document.getElementById('ta').classList.toggle('on',audioOn);saveSettings();}
function togSensors(){sensOn=!sensOn;document.getElementById('ts').classList.toggle('on',sensOn);showSens(sensOn);saveSettings();}
function tog3D(){
  hand3D=!hand3D;
  document.getElementById('th').classList.toggle('on',hand3D);
  document.getElementById('hcanvas').style.display=hand3D?'block':'none';
  document.getElementById('hleg').style.display=hand3D?'flex':'none';
}
function chgVoice(){voice=document.getElementById('vs').value;saveSettings();}
function showSens(v){document.getElementById('ssec').style.display=v?'flex':'none';}

function setAS(cls,msg){const el=document.getElementById('ast');el.className='astat'+(cls?' '+cls:'');el.textContent=msg;}
function audioUrl(lbl){return'/audio/'+(voice==='0'?'boy':'girl')+'/'+encodeURIComponent(lbl)+'.mp3';}
function stopAudio(){if(curAudio){try{curAudio.pause();curAudio.currentTime=0;}catch(_){}curAudio=null;}}
function playAudio(label){
  if(!audioOn)return;
  const base=String(label||'').trim();
  if(!base||base==='---')return;
  const tries=[base];
  if(base!==base.toLowerCase())tries.push(base.toLowerCase());
  stopAudio();setAS('','&#8987; '+base);
  let idx=0;
  function next(){
    if(idx>=tries.length){
      setAS('err','&#9888; No MP3: '+base+' (TTS)');
      if(window.speechSynthesis){speechSynthesis.cancel();const u=new SpeechSynthesisUtterance(base);u.rate=0.9;speechSynthesis.speak(u);}
      return;
    }
    const url=audioUrl(tries[idx]);
    fetch(url,{method:'HEAD'}).then(r=>{
      if(!r.ok){idx++;next();return;}
      const a=new Audio(url);
      a.onerror=()=>{idx++;next();};
      a.onended=()=>{curAudio=null;setAS('','&#9208; Idle');};
      a.play().then(()=>{curAudio=a;setAS('ok','&#128266; '+tries[idx]);}).catch(()=>{idx++;next();});
    }).catch(()=>{idx++;next();});
  }
  next();
}
function testAudio(){if(!audioOn){alert('Enable audio first');return;}playAudio('hello');}

function N(v,fb=0){const n=Number(v);return isFinite(n)?n:fb;}
function clamp(v,a,b){return Math.max(a,Math.min(b,v));}
function pctS(v){return clamp((N(v,0)+100)*0.5,0,100);}
function avg(a){return a.length?a.reduce((s,v)=>s+N(v),0)/a.length:0;}

let src;
function connect(){
  src=new EventSource('/events');
  src.onopen=()=>{document.getElementById('dot').className='dot on';document.getElementById('ctxt').textContent='Connected';};
  src.onerror=()=>{document.getElementById('dot').className='dot';document.getElementById('ctxt').textContent='Reconnecting\u2026';setTimeout(connect,3000);};
  src.onmessage=ev=>{
    let d;try{d=JSON.parse(ev.data);}catch(_){return;}
    const g=d.g||'---';
    const ge=document.getElementById('gesture');
    ge.textContent=g;ge.className=g==='---'?'idle':'';
    const conf=N(d.c,0);
    document.getElementById('cbar').style.width=clamp(conf*100,0,100).toFixed(1)+'%';
    document.getElementById('cval').textContent=conf>0?'Confidence: '+(conf*100).toFixed(1)+'%':'Confidence: --';
    if(g!=='---'&&g!==lastG&&audioOn){lastG=g;playAudio(g);}else if(g==='---')lastG='';
    const fpv=Array.isArray(d.fp)?d.fp:[];
    const hpv=Array.isArray(d.hp)?d.hp:[];
    const fv=Array.isArray(d.f)?d.f:[];
    const hv=Array.isArray(d.h)?d.h:[];
    const amax=N(d.am,4095);
    FN.forEach((f,i)=>{
      const fp=fpv.length?pctS(N(fpv[i],0)):clamp((N(fv[i],0)/amax)*100,0,100);
      const hp=hpv.length?pctS(N(hpv[i],0)):clamp((N(hv[i],0)/amax)*100,0,100);
      fPct[i]=fp;
      const fbe=document.getElementById('fb'+f),hbe=document.getElementById('hb'+f);
      const fve=document.getElementById('fbv'+f),hve=document.getElementById('hbv'+f);
      if(fbe)fbe.style.height=fp.toFixed(1)+'%';
      if(hbe)hbe.style.height=hp.toFixed(1)+'%';
      if(fve)fve.textContent=Math.round(fp)+'%';
      if(hve)hve.textContent=Math.round(hp)+'%';
    });
    imuPitch=N(d.p);imuRoll=N(d.r);imuGz=N(d.gz);
    const now2=Date.now();
    if(lastTs>0){const dt=(now2-lastTs)/1000;imuYaw=clamp(imuYaw+imuGz*dt,-180,180);}
    const ax=N(d.ax),ay=N(d.ay),az=N(d.az),gx=N(d.gx),gy=N(d.gy),gz=N(d.gz);
    const am=Math.sqrt(ax*ax+ay*ay+az*az),gm=Math.sqrt(gx*gx+gy*gy+gz*gz);
    document.getElementById('ipr').textContent=imuPitch.toFixed(1)+'\u00b0 / '+imuRoll.toFixed(1)+'\u00b0';
    document.getElementById('iacc').textContent=ax.toFixed(2)+'\n'+ay.toFixed(2)+'\n'+az.toFixed(2);
    document.getElementById('igyr').textContent=gx.toFixed(2)+'\n'+gy.toFixed(2)+'\n'+gz.toFixed(2);
    document.getElementById('imag').textContent=am.toFixed(2)+' / '+gm.toFixed(2);
    frames++;
    const now=Date.now(),hz=lastTs?(1000/(now-lastTs)):0;lastTs=now;
    document.getElementById('sf').textContent=frames;
    document.getElementById('shz').textContent=hz>0?hz.toFixed(1)+' Hz':'--';
    document.getElementById('sfx').textContent=fpv.length?Math.round(avg(fpv))+'%':fv.length?Math.round(avg(fv)):'--';
    document.getElementById('shl').textContent=hpv.length?Math.round(avg(hpv))+'%':hv.length?Math.round(avg(hv)):'--';
  };
}

/* ══════════════════════════════════════════════════════════════════
   3-D HAND  — anatomically proportioned pivot-chained skeleton

   Local hand coordinate frame (inside hGroup):
     +Y = dorsal normal (points OUT of back of hand)
     -Z = toward fingertips
     +X = thumb side

   Palm sits at origin. Finger roots at y=+palmH/2 (top surface).
   Each phalange pivot has rotation.x for flexion.
   Rotating pivot.rotation.x pulls every distal joint with it because
   they are children attached at the tip offset.

   IMU maps:
     pitch  → hGroup.rotation.x  (hand nodding up/down)
     roll   → hGroup.rotation.z  (hand tilting left/right)
     yaw    → hGroup.rotation.y  (hand spinning, from integrated gyro-Z)
══════════════════════════════════════════════════════════════════ */

let renderer,scene,camera,hGroup;
let fingerPivots=[];   /* [fi][pi] pivot Groups */
let camDrag=false,camPrev={x:0,y:0},camTh=0.25,camPh=0.88,camDist=4.2,camAuto=true;

function hmat(col,r,m){return new THREE.MeshStandardMaterial({color:col,roughness:r||0.45,metalness:m||0.25});}

/*  buildFinger
    Returns root Group and fills fingerPivots[fi].
    knuckleX, knuckleZ  : attach point on palm top surface (y=0 here, palm adds offset)
    lens   : phalange lengths   e.g. [0.38, 0.24, 0.18]
    radii  : n+1 radii          e.g. [0.075, 0.065, 0.056, 0.048]
    angleX : rest tilt in X (whole finger root, for natural spread)
    angleZ : rest splay in Z
*/
function buildFinger(fi,kx,kz,lens,radii,color,angleX,angleZ){
  const root=new THREE.Group();
  root.position.set(kx,0,kz);
  if(angleX)root.rotation.x=angleX;
  if(angleZ)root.rotation.z=angleZ;

  const pivots=[];
  let parent=root;
  let offset=0; /* Y offset from parent where next pivot sits */

  for(let pi=0;pi<lens.length;pi++){
    const len=lens[pi];
    const r0=radii[pi], r1=radii[pi+1]||r0*0.82;

    const pivot=new THREE.Group();
    pivot.position.y=offset;
    parent.add(pivot);
    pivots.push(pivot);

    /* knuckle sphere at every joint except the base (palm provides that) */
    if(pi>0){
      const ks=new THREE.Mesh(new THREE.SphereGeometry(r0*1.1,10,7),hmat(color,0.35,0.4));
      pivot.add(ks);
    }

    /* tapered bone cylinder */
    const bone=new THREE.Mesh(
      new THREE.CylinderGeometry(r1,r0,len,12,1),
      hmat(color,0.42,0.28));
    bone.position.y=len/2;
    pivot.add(bone);

    offset=len;   /* next pivot attaches at tip of this bone */
    parent=pivot;
  }

  /* fingertip sphere */
  const tipR=radii[radii.length-1]*0.95;
  const tip=new THREE.Mesh(new THREE.SphereGeometry(tipR,10,7),hmat(color,0.28,0.45));
  tip.position.y=offset;
  parent.add(tip);

  fingerPivots[fi]=pivots;
  return root;
}

function initHand(){
  const cv=document.getElementById('hcanvas');
  const W=cv.clientWidth||340,H=250;

  renderer=new THREE.WebGLRenderer({canvas:cv,antialias:true,alpha:true});
  renderer.setPixelRatio(Math.min(window.devicePixelRatio,2));
  renderer.setSize(W,H);
  renderer.setClearColor(0,0);

  scene=new THREE.Scene();
  camera=new THREE.PerspectiveCamera(40,W/H,0.1,60);

  scene.add(new THREE.AmbientLight(0xffffff,0.42));
  const sun=new THREE.DirectionalLight(0xddeeff,1.1);sun.position.set(3,6,4);scene.add(sun);
  const fill=new THREE.DirectionalLight(0x7dd3fc,0.55);fill.position.set(-5,2,-3);scene.add(fill);
  const rim=new THREE.DirectionalLight(0xc084fc,0.32);rim.position.set(0,-4,2);scene.add(rim);

  hGroup=new THREE.Group();
  hGroup.rotation.x=-Math.PI*0.35; /* rest pose: palm tilted toward camera */
  scene.add(hGroup);

  /* PALM — 95x20x95mm scaled 1:18
     Origin at palm centre. Knuckles at z=-PD/2, wrist at z=+PD/2.     */
  const PW=1.05,PH=0.20,PD=1.05;
  const palmMesh=new THREE.Mesh(new THREE.BoxGeometry(PW,PH,PD),hmat(0x1c2a42,0.55,0.18));
  hGroup.add(palmMesh);

  /* dorsal ridge */
  const ridge=new THREE.Mesh(new THREE.BoxGeometry(PW*0.6,PH*0.2,PD*0.65),hmat(0x253654,0.58,0.14));
  ridge.position.set(0,PH*0.58,-0.05);
  hGroup.add(ridge);

  /* wrist cylinder */
  const wrist=new THREE.Mesh(new THREE.CylinderGeometry(0.27,0.31,0.30,14,1),hmat(0x1c2a42,0.55,0.18));
  wrist.position.set(0,0,PD*0.5+0.13);
  hGroup.add(wrist);

  /* knuckle spheres on palm edge */
  const kxs=[-0.315,-0.095,0.12,0.33];
  const krs=[0.078,0.082,0.075,0.060];
  kxs.forEach((kx,i)=>{
    const ks=new THREE.Mesh(new THREE.SphereGeometry(krs[i],10,7),hmat(FC[i+1],0.35,0.42));
    ks.position.set(kx,PH*0.5,-PD*0.5);
    hGroup.add(ks);
  });

  /* finger roots sit on top of palm (y=+PH/2), so shift all buildFinger roots up */
  const Y0=PH/2;

  /* helper: attach finger root shifted up by Y0 */
  function addFinger(fi,kx,kz,lens,radii,color,ax,az){
    const root=buildFinger(fi,kx,kz,lens,radii,color,ax,az);
    root.position.y=Y0; /* lift to palm surface */
    hGroup.add(root);
  }

  /* THUMB — lateral side, 2 joints (MCP flexion + IP flexion) */
  addFinger(0,
    -(PW*0.5), 0.10,
    [0.26, 0.22],
    [0.082,0.072,0.060],
    FC[0], -0.18, 0.52);

  /* INDEX */
  addFinger(1,
    -0.315, -PD*0.5,
    [0.38,0.24,0.18],
    [0.075,0.065,0.056,0.048],
    FC[1], -0.04, 0.04);

  /* MIDDLE */
  addFinger(2,
    -0.095, -(PD*0.5+0.01),
    [0.42,0.26,0.20],
    [0.080,0.069,0.059,0.050],
    FC[2], 0, 0);

  /* RING */
  addFinger(3,
    0.12, -PD*0.5,
    [0.38,0.24,0.17],
    [0.073,0.063,0.054,0.046],
    FC[3], -0.03,-0.04);

  /* PINKY */
  addFinger(4,
    0.33, -(PD*0.5-0.04),
    [0.28,0.18,0.14],
    [0.058,0.050,0.043,0.036],
    FC[4], -0.06,-0.10);

  /* floor grid */
  const grid=new THREE.GridHelper(5,14,0x1a2540,0x1a2540);
  grid.position.y=-1.5;grid.material.opacity=0.26;grid.material.transparent=true;
  scene.add(grid);

  /* orbit controls */
  cv.addEventListener('mousedown',e=>{camDrag=true;camAuto=false;camPrev={x:e.clientX,y:e.clientY};});
  cv.addEventListener('mousemove',e=>{
    if(!camDrag)return;
    camTh-=(e.clientX-camPrev.x)*0.011;
    camPh-=(e.clientY-camPrev.y)*0.011;
    camPh=clamp(camPh,0.1,2.5);
    camPrev={x:e.clientX,y:e.clientY};
  });
  cv.addEventListener('mouseup',()=>camDrag=false);
  cv.addEventListener('mouseleave',()=>camDrag=false);
  let lt=null;
  cv.addEventListener('touchstart',e=>{camAuto=false;lt={x:e.touches[0].clientX,y:e.touches[0].clientY};e.preventDefault();},{passive:false});
  cv.addEventListener('touchmove',e=>{
    if(!lt)return;
    camTh-=(e.touches[0].clientX-lt.x)*0.013;
    camPh-=(e.touches[0].clientY-lt.y)*0.013;
    camPh=clamp(camPh,0.1,2.5);
    lt={x:e.touches[0].clientX,y:e.touches[0].clientY};
    e.preventDefault();
  },{passive:false});
  cv.addEventListener('touchend',()=>lt=null);
  cv.addEventListener('wheel',e=>{camDist=clamp(camDist+e.deltaY*0.006,2.0,9.0);},{passive:true});

  renderLoop();
}

/* max curl angles per phalange [fi][pi] in radians */
const MAXCURL=[
  [0.85,0.70],          /* Thumb: MCP, IP */
  [1.45,1.30,1.05],     /* Index: MCP, PIP, DIP */
  [1.45,1.30,1.05],     /* Middle */
  [1.40,1.25,1.00],     /* Ring */
  [1.30,1.15,0.90],     /* Pinky */
];

function updateHand(){
  /* finger curl from flex sensor percentages */
  fingerPivots.forEach((pivots,fi)=>{
    if(!pivots)return;
    const curl=clamp(fPct[fi]/100,0,1);
    pivots.forEach((piv,pi)=>{
      piv.rotation.x=THREE.MathUtils.lerp(piv.rotation.x,curl*(MAXCURL[fi][pi]||1.0),0.10);
    });
  });

  /* IMU orientation — all three axes
     pitch: hand pitches forward/back   → X rotation (on top of rest tilt)
     roll : hand rolls left/right       → Z rotation
     yaw  : hand spins in table plane   → Y rotation (integrated from gyro-Z) */
  const BASE=-Math.PI*0.35;
  const tX=clamp(imuPitch,-90,90)*D2R*0.9+BASE;
  const tZ=clamp(imuRoll, -90,90)*D2R*0.9;
  const tY=clamp(imuYaw, -180,180)*D2R*0.5;
  hGroup.rotation.x=THREE.MathUtils.lerp(hGroup.rotation.x,tX,0.08);
  hGroup.rotation.z=THREE.MathUtils.lerp(hGroup.rotation.z,tZ,0.08);
  hGroup.rotation.y=THREE.MathUtils.lerp(hGroup.rotation.y,tY,0.05);
}

function renderLoop(){
  requestAnimationFrame(renderLoop);
  if(camAuto)camTh+=0.004;
  camera.position.x=camDist*Math.sin(camPh)*Math.sin(camTh);
  camera.position.y=camDist*Math.cos(camPh);
  camera.position.z=camDist*Math.sin(camPh)*Math.cos(camTh);
  camera.lookAt(0,0,0);
  if(hand3D&&hGroup)updateHand();
  renderer.render(scene,camera);
}

window.addEventListener('resize',()=>{
  if(!renderer)return;
  const cv=document.getElementById('hcanvas');
  const W=cv.clientWidth||340;
  renderer.setSize(W,250);camera.aspect=W/250;camera.updateProjectionMatrix();
});

loadSettings();
connect();
window.addEventListener('load',()=>{
  if(typeof THREE!=='undefined'){initHand();return;}
  let t=0;
  const iv=setInterval(()=>{
    if(typeof THREE!=='undefined'){clearInterval(iv);initHand();}
    else if(++t>25){
      clearInterval(iv);
      document.getElementById('hcanvas').insertAdjacentHTML('afterend',
        '<p style="color:var(--txt3);font-size:.72rem;padding:6px 0">3D unavailable \u2014 place three.min.js in /data/web/ on LittleFS</p>');
    }
  },200);
});
</script>
</body>
</html>)rawliteral";

// ════════════════════════════════════════════════════════════════════
//  SSE client management
// ════════════════════════════════════════════════════════════════════
#define MAX_SSE_CLIENTS 4
static WiFiClient sse_clients[MAX_SSE_CLIENTS];
static bool       sse_active[MAX_SSE_CLIENTS] = {};

static void handle_root() { server.send_P(200,"text/html",PAGE_HTML); }

static void handle_events() {
    WiFiClient client=server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.flush();
    for(int i=0;i<MAX_SSE_CLIENTS;i++){
        if(!sse_active[i]||!sse_clients[i].connected()){
            sse_clients[i]=client;sse_active[i]=true;break;
        }
    }
    server.client().stop();
}

static String url_decode(const String &in){
    String out;out.reserve(in.length());
    for(size_t i=0;i<in.length();i++){
        char c=in.charAt(i);
        if(c=='%'&&i+2<in.length()){
            char h1=in.charAt(i+1),h2=in.charAt(i+2);
            if(isxdigit((unsigned char)h1)&&isxdigit((unsigned char)h2)){
                char hex[3]={h1,h2,'\0'};out+=(char)strtol(hex,nullptr,16);i+=2;continue;
            }
        }
        out+=(c=='+')?'  ':c;
    }
    return out;
}

static const char *mime_for(const String &p){
    if(p.endsWith(".js"))  return "application/javascript";
    if(p.endsWith(".css")) return "text/css";
    if(p.endsWith(".html"))return "text/html";
    return "application/octet-stream";
}

static void handle_static(){
    if(!fs_mounted){server.send(503,"text/plain","FS not ready");return;}
    String uri=server.uri();
    if(uri.indexOf("..")>=0){server.send(400,"text/plain","Bad path");return;}
    if(!LittleFS.exists(uri)){server.send(404,"text/plain","Not found");return;}
    File f=LittleFS.open(uri,"r");
    if(!f){server.send(500,"text/plain","Open failed");return;}
    server.sendHeader("Cache-Control","public, max-age=86400");
    server.streamFile(f,mime_for(uri));f.close();
}

static void handle_audio(){
    String uri=server.uri();
    if(!uri.startsWith("/audio/")||uri.length()<=7){server.send(400,"text/plain","Bad path");return;}
    if(!fs_mounted){server.send(503,"text/plain","FS not ready");return;}
    String rel=url_decode(uri.substring(7));
    if(rel.indexOf("..")>=0){server.send(400,"text/plain","Bad path");return;}
    String path="/"+rel;
    Serial.printf("[WEB] Audio: %s\n",path.c_str());
    if(!LittleFS.exists(path)){server.send(404,"text/plain","Not found");return;}
    File f=LittleFS.open(path,"r");
    if(!f){server.send(500,"text/plain","Open failed");return;}
    size_t fileSize=f.size(),rStart=0,rEnd=fileSize-1;
    bool isRange=server.hasHeader("Range");
    if(isRange){
        String rh=server.header("Range");
        if(rh.startsWith("bytes=")){
            String r=rh.substring(6);int dash=r.indexOf('-');
            if(dash>=0){
                String s=r.substring(0,dash),e=r.substring(dash+1);
                if(s.length())rStart=(size_t)s.toInt();
                if(e.length())rEnd=(size_t)e.toInt();
                if(rEnd>=fileSize)rEnd=fileSize-1;
            }
        }
    }
    size_t cLen=rEnd-rStart+1;
    server.sendHeader("Access-Control-Allow-Origin","*");
    server.sendHeader("Accept-Ranges","bytes");
    server.sendHeader("Cache-Control","no-store");
    if(isRange){
        char cr[64];snprintf(cr,sizeof(cr),"bytes %u-%u/%u",(unsigned)rStart,(unsigned)rEnd,(unsigned)fileSize);
        server.sendHeader("Content-Range",cr);
        server.sendHeader("Content-Length",String(cLen));
        server.setContentLength(cLen);
        server.send(206,"audio/mpeg","");
        if(rStart>0)f.seek(rStart);
        uint8_t buf[512];size_t rem=cLen;
        WiFiClient cl=server.client();
        while(rem>0&&cl.connected()){size_t rd=f.read(buf,min(rem,sizeof(buf)));if(!rd)break;cl.write(buf,rd);rem-=rd;}
    }else{
        server.sendHeader("Content-Length",String(fileSize));
        server.streamFile(f,"audio/mpeg");
    }
    f.close();
    Serial.printf("[WEB] Served %u bytes\n",(unsigned)cLen);
}

static void handle_settings(){
    server.sendHeader("Access-Control-Allow-Origin","*");
    if(server.method()==HTTP_GET){
        char j[128];
        snprintf(j,sizeof(j),"{\"speech\":%s,\"voice\":%d,\"sensors\":%s}",
                 cfg_local_speech?"true":"false",(int)cfg_local_voice,cfg_local_sensors?"true":"false");
        server.send(200,"application/json",j);
    }else if(server.method()==HTTP_POST){
        if(!server.hasArg("plain")){server.send(400,"application/json","{\"error\":\"no body\"}");return;}
        String b=server.arg("plain");
        if     (b.indexOf("\"speech\":true") >=0)cfg_local_speech=true;
        else if(b.indexOf("\"speech\":false")>=0)cfg_local_speech=false;
        if     (b.indexOf("\"voice\":0")     >=0)cfg_local_voice=0;
        else if(b.indexOf("\"voice\":1")     >=0)cfg_local_voice=1;
        if     (b.indexOf("\"sensors\":true")>=0)cfg_local_sensors=true;
        else if(b.indexOf("\"sensors\":false")>=0)cfg_local_sensors=false;
        server.send(200,"application/json","{\"ok\":true}");
    }
}

static void sse_broadcast(const String &json){
    String msg="data: "+json+"\n\n";
    for(int i=0;i<MAX_SSE_CLIENTS;i++){
        if(sse_active[i]){
            if(sse_clients[i].connected())sse_clients[i].print(msg);
            else sse_active[i]=false;
        }
    }
}

void web_server_start(){
    if(running)return;
    fs_mounted=LittleFS.begin(true);
    Serial.printf("[WEB] LittleFS: %s\n",fs_mounted?"OK":"FAILED");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID,WIFI_AP_PASS);
    delay(300);
    Serial.printf("[WEB] AP: %s  IP: %s\n",WIFI_AP_SSID,WiFi.softAPIP().toString().c_str());
    s_adc_max=sensors_ads_available()?32767:4095;
    static const char *hdrs[]={"Range"};
    server.collectHeaders(hdrs,1);
    server.on("/",            HTTP_GET,handle_root);
    server.on("/events",      HTTP_GET,handle_events);
    server.on("/api/settings",HTTP_ANY,handle_settings);
    server.onNotFound([](){
        const String &uri=server.uri();
        if(uri.startsWith("/audio/"))    handle_audio();
        else if(uri.startsWith("/web/")) handle_static();
        else{server.sendHeader("Access-Control-Allow-Origin","*");server.send(404,"text/plain","Not found");}
    });
    server.begin();running=true;
    Serial.println("[WEB] Server ready");
}

void web_server_stop(){
    if(!running)return;
    server.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);
    if(fs_mounted){LittleFS.end();fs_mounted=false;}
    running=false;
    for(int i=0;i<MAX_SSE_CLIENTS;i++)if(sse_active[i]){sse_clients[i].stop();sse_active[i]=false;}
    Serial.println("[WEB] Stopped");
}

void web_server_update(const SensorData &d,const ProcessedSensorData &pd,
                       const char *gesture,float confidence){
    if(!running)return;
    server.handleClient();
    char buf[640];
    snprintf(buf,sizeof(buf),
        "{\"g\":\"%s\",\"c\":%.3f,"
        "\"f\":[%u,%u,%u,%u,%u],"
        "\"h\":[%u,%u,%u,%u,%u],"
        "\"fp\":[%d,%d,%d,%d,%d],"
        "\"hp\":[%d,%d,%d,%d,%d],"
        "\"am\":%u,"
        "\"p\":%.1f,\"r\":%.1f,"
        "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
        "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}",
        gesture?gesture:"---",confidence,
        d.flex[0],d.flex[1],d.flex[2],d.flex[3],d.flex[4],
        d.hall[0],d.hall[1],d.hall[2],d.hall[3],d.hall[4],
        (int)pd.flex_pct[0],(int)pd.flex_pct[1],(int)pd.flex_pct[2],(int)pd.flex_pct[3],(int)pd.flex_pct[4],
        (int)pd.hall_pct[0],(int)pd.hall_pct[1],(int)pd.hall_pct[2],(int)pd.hall_pct[3],(int)pd.hall_pct[4],
        s_adc_max,d.pitch,d.roll,d.ax,d.ay,d.az,d.gx,d.gy,d.gz);
    sse_broadcast(String(buf));
}
void web_server_update(const SensorData &d,const ProcessedSensorData &pd,const char *gesture){
    web_server_update(d,pd,gesture,0.0f);
}
void web_server_update(const SensorData &d,const char *gesture,float confidence){
    ProcessedSensorData pd={};web_server_update(d,pd,gesture,confidence);
}
void web_server_update(const SensorData &d,const char *gesture){
    web_server_update(d,gesture,0.0f);
}

bool   web_server_is_running()  {return running;}
int    web_server_num_clients() {return WiFi.softAPgetStationNum();}
String web_server_get_url()     {return "http://"+WiFi.softAPIP().toString();}