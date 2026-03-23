/*
 * @file web_server.cpp
 * @brief WiFi Soft-AP + async-style HTTP server with WebSocket
 *        Serves real-time hand visualization + MP3 playback
 *
 * NOTE: Uses the built-in WiFi + WebServer classes (no external lib).
 *       SSE for real-time updates, LittleFS for MP3 file serving.
 */
#include "web_server.h"
#include "config.h"
#include "sensors.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ctype.h>

// Extern references to local GUI settings (from gui_api.cpp)
extern bool     cfg_local_speech;
extern uint8_t  cfg_local_voice;   // 0 = Boy, 1 = Girl
extern bool     cfg_local_sensors;

static WebServer server(WEB_SERVER_PORT);
static bool      running = false;
static uint16_t  s_adc_max = 4095;

// Cached latest JSON payload for SSE
static String last_json = "{}";

// ════════════════════════════════════════════════════════════════════
//  HTML page (stored in PROGMEM)
// ════════════════════════════════════════════════════════════════════
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Signa – Sign Language Glove</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;background:radial-gradient(circle at 20% -20%,#1e2d63 0%,transparent 45%),
     radial-gradient(circle at 80% 120%,#214f5f 0%,transparent 40%),
     linear-gradient(145deg,#080d22 0%,#121937 55%,#0b0f25 100%);
     color:#ebf2ff;font-family:Inter,Segoe UI,Roboto,sans-serif;display:flex;justify-content:center;padding:16px}
.container{width:100%;max-width:720px;display:grid;gap:14px}
.card{background:rgba(255,255,255,.05);backdrop-filter:blur(8px);border:1px solid rgba(255,255,255,.12);
      border-radius:16px;box-shadow:0 10px 35px rgba(0,0,0,.28)}
header{padding:16px 18px;display:flex;align-items:center;justify-content:space-between;gap:12px}
h1{font-size:1.2rem;letter-spacing:.2px;color:#7dd3fc}
.subtitle{font-size:.78rem;color:#9db0d8}
.chip{padding:6px 10px;border-radius:999px;background:rgba(125,211,252,.12);color:#9de6ff;font-size:.72rem;border:1px solid rgba(125,211,252,.35)}
.prediction{padding:22px;text-align:center}
#gesture{font-size:2.8rem;font-weight:800;color:#86efac;text-shadow:0 0 22px rgba(134,239,172,.24);line-height:1.05;min-height:54px}
.gesture-label{margin-top:10px;color:#b8c7e7;font-size:.84rem}
.controls{padding:14px 16px;display:grid;gap:12px}
.control-row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;
            border:1px solid rgba(255,255,255,.08);background:rgba(255,255,255,.03);border-radius:12px}
.control-title{display:flex;flex-direction:column;gap:2px}
.control-title strong{font-size:.9rem;color:#d6e3ff}
.control-title span{font-size:.75rem;color:#97acd4}
.toggle{display:inline-flex;align-items:center;width:50px;height:28px;background:#2d375d;border:1px solid #4d598c;
       border-radius:999px;cursor:pointer;position:relative;transition:.25s}
.toggle.on{background:#22c55e;border-color:#4ade80}
.toggle::after{content:'';position:absolute;left:3px;width:22px;height:22px;background:#f8fafc;border-radius:50%;transition:.25s}
.toggle.on::after{left:24px}
.dropdown{padding:8px 12px;background:#111935;color:#f1f5ff;border:1px solid #3b82f6;border-radius:10px;font-size:.85rem}
.status-row{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;padding:0 16px 14px}
.stat{padding:10px;border-radius:12px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.08)}
.stat b{display:block;font-size:.95rem;color:#dff6ff}
.stat small{font-size:.7rem;color:#95a9d2}
.sensor-section{padding:14px 16px;display:grid;gap:12px}
.sensor-section.hidden{display:none}
h2{font-size:.92rem;color:#7dd3fc;display:flex;align-items:center;gap:8px}
.bars{display:flex;gap:8px;justify-content:space-between;width:100%}
.bar-wrap{display:flex;flex-direction:column;align-items:center;flex:1;min-width:0}
.bar-outer{width:100%;height:120px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.1);
          border-radius:10px;position:relative;overflow:hidden}
.bar-inner{position:absolute;bottom:0;width:100%;border-radius:10px;transition:height .12s ease-out}
.bar-lbl{font-size:.68rem;color:#a8bbde;margin-top:5px}
.bar-val{font-size:.64rem;color:#8ec5ff}
.flex .bar-inner{background:linear-gradient(180deg,#22d3ee,#3b82f6)}
.hall .bar-inner{background:linear-gradient(180deg,#fb923c,#f97316)}
.imu-box{font-size:.82rem;color:#d9e6ff;line-height:1.5;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.08);
         border-radius:12px;padding:10px 12px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}
.dot.on{background:#22c55e;box-shadow:0 0 8px rgba(34,197,94,.8)}
.dot.off{background:#ef4444}
#status{padding:12px 16px;text-align:center;color:#ccdbfb;font-size:.84rem}
.button-group{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;padding:0 16px 16px}
button{padding:10px 14px;background:linear-gradient(135deg,#38bdf8,#2563eb);color:#fff;border:none;border-radius:10px;
       cursor:pointer;font-weight:700;font-size:.82rem;letter-spacing:.2px}
button.alt{background:linear-gradient(135deg,#334155,#0f172a)}
@media(max-width:560px){
  .status-row{grid-template-columns:repeat(2,minmax(0,1fr))}
  #gesture{font-size:2.2rem}
}
</style>
</head>
<body>
<div class="container">
  <header class="card">
    <div>
      <h1>✋ Signa Glove · Live Web Dashboard</h1>
      <div class="subtitle">Real-time recognition, audio playback from ESP32 LittleFS, and sensor diagnostics</div>
    </div>
    <div class="chip" id="chip-model">Model: live</div>
  </header>

  <div class="card prediction">
    <div id="gesture">---</div>
    <div class="gesture-label">Detected sign</div>
  </div>

  <div class="card controls">
    <div class="control-row">
      <div class="control-title">
        <strong>🔊 Audio output</strong>
        <span>Play MP3 directly from ESP32 LittleFS</span>
      </div>
      <div>
        <div class="toggle" id="toggle-audio" onclick="toggleAudio()"></div>
      </div>
    </div>

    <div class="control-row">
      <div class="control-title">
        <strong>👁️ Show sensors</strong>
        <span>Reveal live bars and IMU values</span>
      </div>
      <div>
        <div class="toggle" id="toggle-sensors" onclick="toggleSensors()"></div>
      </div>
    </div>

    <div class="control-row">
      <div class="control-title">
        <strong>👤 Voice bank</strong>
        <span>Select folder: <code>/boy</code> or <code>/girl</code></span>
      </div>
      <select class="dropdown" id="voice-select" onchange="changeVoice()">
        <option value="0">👦 Boy</option>
        <option value="1">👧 Girl</option>
      </select>
    </div>
  </div>

  <div class="status-row">
    <div class="stat"><small>Frames</small><b id="stat-frames">0</b></div>
    <div class="stat"><small>Stream rate</small><b id="stat-fps">0 Hz</b></div>
    <div class="stat"><small>Flex avg</small><b id="stat-flex">0</b></div>
    <div class="stat"><small>Hall avg</small><b id="stat-hall">0</b></div>
  </div>

  <div id="sensor-section" class="card sensor-section hidden">
    <h2>📊 Flex Sensors</h2>
    <div class="bars flex" id="flex-bars"></div>

    <h2>🧲 Hall Sensors (Side)</h2>
    <div class="bars hall" id="hall-bars"></div>

    <h2>📐 IMU Stats</h2>
    <div class="imu-box" id="imu"></div>
  </div>

  <div id="status" class="card"><span class="dot off" id="dot"></span>Connecting…</div>

  <div class="button-group">
    <button onclick="testAudio()">🔊 Test Audio</button>
    <button class="alt" onclick="location.reload()">🔄 Refresh</button>
  </div>
</div>

<script>
const FINGERS=['Thumb','Index','Middle','Ring','Pinky'];
let audioEnabled=true;
let showSensors=false;
let currentVoice='0';
let currentAudio=null;
let lastGest='';
let adcMax=4095;
let frameCount=0;
let lastFrameTs=0;

function initUI(){
  const tc=document.getElementById('toggle-audio');
  const ts=document.getElementById('toggle-sensors');
  tc.classList.add('on');
  fetch('/api/settings')
    .then(r=>r.json())
    .then(d=>{
      audioEnabled=d.speech;
      showSensors=d.sensors;
      currentVoice=String(d.voice);
      tc.classList.toggle('on',audioEnabled);
      ts.classList.toggle('on',showSensors);
      document.getElementById('voice-select').value=currentVoice;
      updateSensorDisplay();
    })
    .catch(e=>console.error('Failed to load settings:',e));
}

function makeBars(id){
  const c=document.getElementById(id);
  if(!c) return;
  FINGERS.forEach(f=>{
    const w=document.createElement('div');w.className='bar-wrap';
    w.innerHTML='<div class="bar-outer"><div class="bar-inner" id="'+id+'-'+f+'" style="height:0%"></div></div><div class="bar-lbl">'+f[0]+'</div><div class="bar-val" id="'+id+'-val-'+f+'">0</div>';
    c.appendChild(w);
  });
}

function toggleAudio(){
  audioEnabled=!audioEnabled;
  document.getElementById('toggle-audio').classList.toggle('on');
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({speech:audioEnabled,voice:parseInt(currentVoice),sensors:showSensors})});
}

function toggleSensors(){
  showSensors=!showSensors;
  document.getElementById('toggle-sensors').classList.toggle('on');
  updateSensorDisplay();
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({speech:audioEnabled,voice:parseInt(currentVoice),sensors:showSensors})});
}

function updateSensorDisplay(){
  const sec=document.getElementById('sensor-section');
  if(showSensors && sec.classList.contains('hidden')){
    sec.classList.remove('hidden');
  }else if(!showSensors && !sec.classList.contains('hidden')){
    sec.classList.add('hidden');
  }
}

function changeVoice(){
  currentVoice=document.getElementById('voice-select').value;
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({speech:audioEnabled,voice:parseInt(currentVoice),sensors:showSensors})});
}

function buildAudioUrl(label){
  const voiceDir=currentVoice==='0'?'boy':'girl';
  return '/audio/'+voiceDir+'/'+encodeURIComponent(label)+'.mp3';
}

function stopCurrentAudio(){
  if(currentAudio){
    try{
      currentAudio.pause();
      currentAudio.currentTime=0;
    }catch(_e){}
    currentAudio=null;
  }
}

function playAudio(label){
  if(!audioEnabled) return;
  const base=String(label||'').trim();
  if(!base) return;

  const candidates=[base];
  const lower=base.toLowerCase();
  if(lower!==base) candidates.push(lower);

  stopCurrentAudio();

  const tryPlay=(idx)=>{
    if(idx>=candidates.length){
      console.log('MP3 not found on ESP32, fallback TTS:',base);
      if(window.speechSynthesis){
        window.speechSynthesis.cancel();
        const utt=new SpeechSynthesisUtterance(base);
        utt.rate=0.9;
        window.speechSynthesis.speak(utt);
      }
      return;
    }
    const audio=new Audio(buildAudioUrl(candidates[idx]));
    audio.preload='auto';
    audio.onerror=()=>tryPlay(idx+1);
    audio.onended=()=>{ currentAudio=null; };
    audio.oncanplaythrough=()=>{
      audio.play()
        .then(()=>{ currentAudio=audio; })
        .catch(()=>tryPlay(idx+1));
    };
    audio.load();
  };

  tryPlay(0);
}

function testAudio(){
  if(!audioEnabled){
    alert('Audio is disabled. Enable it first!');
    return;
  }
  playAudio('hello');
}

makeBars('flex-bars');
makeBars('hall-bars');

const gest=document.getElementById('gesture');
const imu=document.getElementById('imu');
const dot=document.getElementById('dot');
const stat=document.getElementById('status');
const statFrames=document.getElementById('stat-frames');
const statFps=document.getElementById('stat-fps');
const statFlex=document.getElementById('stat-flex');
const statHall=document.getElementById('stat-hall');

let src;
function toNum(v, fb=0){
  const n=Number(v);
  return Number.isFinite(n)?n:fb;
}

function clamp(v,min,max){
  return Math.max(min,Math.min(max,v));
}

function pct(raw){
  return clamp((toNum(raw,0)/Math.max(adcMax,1))*100,0,100);
}

function avg(arr){
  if(!Array.isArray(arr)||!arr.length) return 0;
  let sum=0;
  for(let i=0;i<arr.length;i++) sum+=toNum(arr[i],0);
  return sum/arr.length;
}

function connect(){
  src=new EventSource('/events');
  src.onopen=()=>{
    dot.className='dot on';
    stat.innerHTML='<span class="dot on"></span>Connected';
  };
  src.onerror=()=>{
    dot.className='dot off';
    stat.innerHTML='<span class="dot off"></span>Reconnecting…';
  };
  src.onmessage=e=>{
    try{
      const d=JSON.parse(e.data);
      adcMax=toNum(d.am,adcMax);

      const currentGest=d.g||'---';
      gest.textContent=currentGest;
      if(currentGest!=='---' && currentGest!==lastGest && audioEnabled){
        lastGest=currentGest;
        playAudio(currentGest);
      }else if(currentGest==='---'){
        lastGest='';
      }
      const flexVals=Array.isArray(d.f)?d.f:[];
      const hallVals=Array.isArray(d.h)?d.h:[];

      FINGERS.forEach((f,i)=>{
        const fb=document.getElementById('flex-bars-'+f);
        const hb=document.getElementById('hall-bars-'+f);
        const fv=toNum(flexVals[i],0);
        const hv=toNum(hallVals[i],0);
        if(fb) fb.style.height=pct(fv).toFixed(1)+'%';
        if(hb) hb.style.height=pct(hv).toFixed(1)+'%';
        const fvLabel=document.getElementById('flex-bars-val-'+f);
        const hvLabel=document.getElementById('hall-bars-val-'+f);
        if(fvLabel) fvLabel.textContent=Math.round(fv);
        if(hvLabel) hvLabel.textContent=Math.round(hv);
      });

      const ax=toNum(d.ax,0), ay=toNum(d.ay,0), az=toNum(d.az,0);
      const gx=toNum(d.gx,0), gy=toNum(d.gy,0), gz=toNum(d.gz,0);
      const pitch=toNum(d.p,0), roll=toNum(d.r,0);
      const amag=Math.sqrt(ax*ax+ay*ay+az*az);
      const gmag=Math.sqrt(gx*gx+gy*gy+gz*gz);
      if(imu) imu.innerHTML=
        'Pitch: <b>'+pitch.toFixed(1)+'°</b> · Roll: <b>'+roll.toFixed(1)+'°</b><br>'+
        'Accel: '+ax.toFixed(2)+', '+ay.toFixed(2)+', '+az.toFixed(2)+' m/s² (|a|='+amag.toFixed(2)+')<br>'+
        'Gyro: '+gx.toFixed(2)+', '+gy.toFixed(2)+', '+gz.toFixed(2)+' °/s (|g|='+gmag.toFixed(2)+')';

      frameCount++;
      const now=Date.now();
      let hz=0;
      if(lastFrameTs){
        const dt=now-lastFrameTs;
        if(dt>0) hz=1000/dt;
      }
      lastFrameTs=now;
      if(statFrames) statFrames.textContent=String(frameCount);
      if(statFps) statFps.textContent=hz>0?hz.toFixed(1)+' Hz':'-';
      if(statFlex) statFlex.textContent=Math.round(avg(flexVals));
      if(statHall) statHall.textContent=Math.round(avg(hallVals));
    }catch(err){
      console.error('Data parse error:',err);
    }
  };
}

initUI();
connect();
</script>
</body>
</html>
)rawliteral";

// ════════════════════════════════════════════════════════════════════
//  SSE client management
// ════════════════════════════════════════════════════════════════════
#define MAX_SSE_CLIENTS 4
static WiFiClient sse_clients[MAX_SSE_CLIENTS];
static bool       sse_active[MAX_SSE_CLIENTS] = {};

static void handle_root() {
    server.send_P(200, "text/html", PAGE_HTML);
}

static void handle_events() {
    WiFiClient client = server.client();
    // Send SSE headers
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.flush();

    // Store in slot
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!sse_active[i] || !sse_clients[i].connected()) {
            sse_clients[i] = client;
            sse_active[i]  = true;
            break;
        }
    }
    // Prevent WebServer from closing the connection
    server.client().stop();
}

  static String url_decode(const String &in) {
    String out;
    out.reserve(in.length());

    for (size_t i = 0; i < in.length(); i++) {
      char c = in.charAt(i);

      if (c == '%' && i + 2 < in.length()) {
        char h1 = in.charAt(i + 1);
        char h2 = in.charAt(i + 2);
        if (isxdigit((unsigned char)h1) && isxdigit((unsigned char)h2)) {
          char hex[3] = { h1, h2, '\0' };
          out += (char)strtol(hex, nullptr, 16);
          i += 2;
          continue;
        }
      }

      if (c == '+') out += ' ';
      else out += c;
    }

    return out;
  }

static void handle_audio() {
    // Path format: /audio/boy/<label>.mp3 or /audio/girl/<label>.mp3
    String uri = server.uri();

    // Extract path after /audio/
    const String prefix = "/audio/";
    if (!uri.startsWith(prefix) || uri.length() <= prefix.length()) {
      server.send(400, "text/plain", "Bad audio path");
      return;
    }

    String rel = uri.substring(prefix.length());
    rel = url_decode(rel);
    if (rel.indexOf("..") >= 0) {
      server.send(400, "text/plain", "Invalid path");
      return;
    }

    String filepath = "/" + rel;

    Serial.printf("[WEB] Audio request: %s\n", filepath.c_str());

    if (!LittleFS.begin(true)) {
      Serial.println("[WEB] LittleFS mount failed");
      server.send(500, "text/plain", "LittleFS mount failed");
      return;
    }

    File file = LittleFS.open(filepath, "r");
    if (!file) {
        Serial.printf("[WEB] Audio file not found: %s\n", filepath.c_str());
      LittleFS.end();
        server.send(404, "text/plain", "Not Found");
        return;
    }

    size_t fileSize = file.size();
    server.sendHeader("Cache-Control", "no-store, max-age=0");
    server.sendHeader("Accept-Ranges", "bytes");
    server.sendHeader("Content-Length", String(fileSize));
    server.streamFile(file, "audio/mpeg");

    file.close();
    LittleFS.end();
}

static void handle_api_settings() {
    if (server.method() == HTTP_GET) {
        // Return current settings
        char json[128];
        snprintf(json, sizeof(json),
            "{\"speech\":%s,\"voice\":%d,\"sensors\":%s}",
            cfg_local_speech ? "true" : "false",
            (int)cfg_local_voice,
            cfg_local_sensors ? "true" : "false");
        server.send(200, "application/json", json);
    } else if (server.method() == HTTP_POST) {
        // Parse and set settings
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            // Simple JSON parsing: {"speech":true,"voice":0,"sensors":true}
            if (body.indexOf("\"speech\":true") >= 0) {
                cfg_local_speech = true;
            } else if (body.indexOf("\"speech\":false") >= 0) {
                cfg_local_speech = false;
            }
            
            if (body.indexOf("\"voice\":0") >= 0) {
                cfg_local_voice = 0;
            } else if (body.indexOf("\"voice\":1") >= 0) {
                cfg_local_voice = 1;
            }
            
            if (body.indexOf("\"sensors\":true") >= 0) {
                cfg_local_sensors = true;
            } else if (body.indexOf("\"sensors\":false") >= 0) {
                cfg_local_sensors = false;
            }
            
            Serial.printf("[WEB] Settings updated: speech=%s, voice=%s, sensors=%s\n",
                cfg_local_speech ? "ON" : "OFF",
                cfg_local_voice == 0 ? "Boy" : "Girl",
                cfg_local_sensors ? "ON" : "OFF");
            
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
        }
    }
}

static void sse_broadcast(const String &json) {
    String msg = "data: " + json + "\n\n";
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_active[i]) {
            if (sse_clients[i].connected()) {
                sse_clients[i].print(msg);
            } else {
                sse_active[i] = false;
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════
//  Public API
// ════════════════════════════════════════════════════════════════════
void web_server_start() {
    if (running) return;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    delay(300);   // let AP stabilise

    Serial.printf("[WEB] AP started — SSID: %s  IP: %s\n",
                  WIFI_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    s_adc_max = sensors_ads_available() ? 32767 : 4095;
    Serial.printf("[WEB] Sensor scale max: %u\n", s_adc_max);

    server.on("/",                  handle_root);
    server.on("/events",            handle_events);
    server.on("/api/settings",      handle_api_settings);
    server.onNotFound([]() {
        // Default audio handler for paths starting with /audio/
        if (server.uri().startsWith("/audio/")) {
            handle_audio();
        } else {
            server.send(404, "text/plain", "404 Not Found");
        }
    });
    
    server.begin();
    running = true;
}

void web_server_stop() {
    if (!running) return;
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running = false;
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sse_active[i]) { sse_clients[i].stop(); sse_active[i] = false; }
    }
    Serial.println("[WEB] Server stopped");
}

void web_server_update(const SensorData &d, const char *gesture) {
    if (!running) return;
    server.handleClient();          // pump HTTP state machine

    // Build compact JSON
    char buf[400];
    snprintf(buf, sizeof(buf),
        "{\"g\":\"%s\","
        "\"f\":[%u,%u,%u,%u,%u],"
        "\"h\":[%u,%u,%u,%u,%u],"
      "\"am\":%u,"
        "\"p\":%.1f,\"r\":%.1f,"
        "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
        "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}",
        gesture ? gesture : "---",
        d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4],
        d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4],
      s_adc_max,
        d.pitch, d.roll,
        d.ax, d.ay, d.az,
        d.gx, d.gy, d.gz);

    sse_broadcast(String(buf));
}

bool web_server_is_running() { return running; }

int web_server_num_clients() {
    return WiFi.softAPgetStationNum();
}

String web_server_get_url() {
    return "http://" + WiFi.softAPIP().toString();
}
