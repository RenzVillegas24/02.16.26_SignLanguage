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
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

// Extern references to local GUI settings (from gui_api.cpp)
extern bool     cfg_local_speech;
extern uint8_t  cfg_local_voice;   // 0 = Boy, 1 = Girl
extern bool     cfg_local_sensors;

static WebServer server(WEB_SERVER_PORT);
static bool      running = false;

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
body{background:linear-gradient(135deg,#0a0e27 0%,#1a1f3a 100%);
     color:#eee;font-family:'Segoe UI',Roboto,sans-serif;
     display:flex;flex-direction:column;align-items:center;padding:16px;
     min-height:100vh;font-size:14px}
.container{width:100%;max-width:500px}
header{text-align:center;margin-bottom:24px;padding:12px 0;
       border-bottom:2px solid #0af;border-radius:8px}
h1{font-size:1.6rem;margin-bottom:4px;color:#0af;text-shadow:0 0 10px rgba(0,175,255,0.3)}
.subtitle{font-size:0.85rem;color:#888}
.prediction-area{background:rgba(0,175,255,0.05);border:2px solid #0af;
               border-radius:12px;padding:24px;margin-bottom:20px;text-align:center}
#gesture{font-size:3rem;color:#0fa;min-height:60px;text-shadow:0 0 10px rgba(0,255,170,0.3);
        font-weight:bold}
.gesture-label{font-size:0.85rem;color:#666;margin-top:8px}
.controls{background:rgba(255,255,255,0.03);border:1px solid #333;border-radius:12px;
         padding:16px;margin-bottom:20px}
.control-group{margin-bottom:16px}
.control-group:last-child{margin-bottom:0}
.control-label{display:flex;align-items:center;gap:8px;margin-bottom:8px;
              color:#aaa;font-size:0.9rem}
.toggle{display:inline-flex;align-items:center;width:44px;height:24px;
       background:#444;border:1px solid #555;border-radius:12px;cursor:pointer;
       position:relative;transition:background 0.3s}
.toggle.on{background:#0fa}
.toggle::after{content:'';position:absolute;width:20px;height:20px;
             background:#222;border-radius:10px;left:2px;transition:left 0.3s}
.toggle.on::after{left:22px;background:#fff}
.dropdown{width:100%;padding:8px 12px;background:#1a1f3a;color:#eee;
         border:1px solid #0af;border-radius:6px;font-size:0.9rem;cursor:pointer}
.sensor-section{background:rgba(255,255,255,0.03);border:1px solid #333;
               border-radius:12px;padding:16px}
.sensor-section.hidden{display:none}
h2{font-size:1rem;color:#0af;margin-bottom:12px;display:flex;align-items:center;gap:8px}
.bars{display:flex;gap:6px;justify-content:center;width:100%;margin-bottom:16px;flex-wrap:wrap}
.bar-wrap{display:flex;flex-direction:column;align-items:center;flex:0 0 calc(20% - 5px)}
.bar-outer{width:100%;height:100px;background:rgba(255,255,255,0.05);border:1px solid #444;
          border-radius:6px;position:relative;overflow:hidden}
.bar-inner{position:absolute;bottom:0;width:100%;border-radius:6px;transition:height 0.15s}
.bar-lbl{font-size:0.65rem;color:#999;margin-top:4px;text-align:center}
.flex .bar-inner{background:linear-gradient(180deg,#0cf,#09f)}
.hall .bar-inner{background:linear-gradient(180deg,#f80,#e60)}
.hall-top .bar-inner{background:linear-gradient(180deg,#0f8,#06d)}
#imu{font-size:0.85rem;color:#aaa;text-align:center;padding:8px;
    background:rgba(255,255,255,0.02);border-radius:6px;margin-bottom:16px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}
.dot.on{background:#0f0;box-shadow:0 0 6px #0f0}.dot.off{background:#f00}
#status{text-align:center;font-size:0.85rem;padding:12px;
       background:rgba(255,255,255,0.02);border-radius:6px;margin-bottom:16px}
.button-group{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-top:16px}
button{padding:10px 20px;background:#0af;color:#000;border:none;border-radius:6px;
       cursor:pointer;font-weight:bold;font-size:0.9rem;transition:background 0.3s}
button:hover{background:#0df}
button:active{background:#09f}
@media(max-width:480px){
  body{padding:12px}
  h1{font-size:1.4rem}
  #gesture{font-size:2.2rem}
  .control-label{font-size:0.85rem}
}
</style>
</head>
<body>
<div class="container">
  <header>
    <h1>✋ Signa Glove</h1>
    <div class="subtitle">Real-time Sign Language Recognition</div>
  </header>

  <div class="prediction-area">
    <div id="gesture">---</div>
    <div class="gesture-label">Detected Sign</div>
  </div>

  <div class="controls">
    <div class="control-group">
      <div class="control-label">
        <span>🔊 Audio Output</span>
        <div class="toggle" id="toggle-audio" onclick="toggleAudio()"></div>
      </div>
    </div>
    
    <div class="control-group">
      <div class="control-label">
        <span>👁️ Show Sensors</span>
        <div class="toggle" id="toggle-sensors" onclick="toggleSensors()"></div>
      </div>
    </div>
    
    <div class="control-group">
      <div class="control-label">👤 Voice</div>
      <select class="dropdown" id="voice-select" onchange="changeVoice()">
        <option value="0">👦 Boy</option>
        <option value="1">👧 Girl</option>
      </select>
    </div>
  </div>

  <div id="sensor-section" class="sensor-section hidden">
    <h2>📊 Flex Sensors</h2>
    <div class="bars flex" id="flex-bars"></div>

    <h2>🧲 Hall Sensors (Side)</h2>
    <div class="bars hall" id="hall-bars"></div>

    <h2>📐 IMU</h2>
    <div id="imu"></div>
  </div>

  <div id="status"><span class="dot off" id="dot"></span>Connecting…</div>

  <div class="button-group">
    <button onclick="testAudio()">🔊 Test Audio</button>
    <button onclick="location.reload()">🔄 Refresh</button>
  </div>
</div>

<script>
const FINGERS=['Thumb','Index','Middle','Ring','Pinky'];
let audioEnabled=true;
let showSensors=false;
let currentVoice='0';
let audioContext=null;
let lastGest='';

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
    w.innerHTML='<div class="bar-outer"><div class="bar-inner" id="'+id+'-'+f+'" style="height:0%"></div></div><div class="bar-lbl">'+f[0]+'</div>';
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

function playAudio(label){
  if(!audioEnabled) return;
  const voiceDir=currentVoice==='0'?'boy':'girl';
  const audioUrl='/audio/'+voiceDir+'/'+encodeURIComponent(label)+'.mp3';
  const audio=new Audio(audioUrl);
  audio.onerror=()=>{
    console.log('MP3 not found, using TTS for:',label);
    if(window.speechSynthesis){
      window.speechSynthesis.cancel();
      const utt=new SpeechSynthesisUtterance(label);
      utt.rate=0.9;
      window.speechSynthesis.speak(utt);
    }
  };
  audio.oncanplaythrough=()=>audio.play().catch(e=>console.log('Audio play failed:',e));
  audio.load();
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

let src;
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
      const currentGest=d.g||'---';
      gest.textContent=currentGest;
      if(currentGest!=='---' && currentGest!==lastGest && audioEnabled){
        lastGest=currentGest;
        playAudio(currentGest);
      }else if(currentGest==='---'){
        lastGest='';
      }
      FINGERS.forEach((f,i)=>{
        const fb=document.getElementById('flex-bars-'+f);
        const hb=document.getElementById('hall-bars-'+f);
        if(fb) fb.style.height=(d.f[i]/4095*100).toFixed(1)+'%';
        if(hb) hb.style.height=(d.h[i]/4095*100).toFixed(1)+'%';
      });
      if(imu) imu.innerHTML='Pitch: '+d.p.toFixed(1)+'° | Roll: '+d.r.toFixed(1)+'° | Accel: '+d.ax.toFixed(1)+', '+d.ay.toFixed(1)+', '+d.az.toFixed(1);
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

static void handle_audio() {
    // Path format: /audio/boy/<label>.mp3 or /audio/girl/<label>.mp3
    String uri = server.uri();
    
    // Extract the path after /audio/
    String filepath = uri.substring(6);  // Skip "/audio"
    
    // Prepend the LittleFS root
    filepath = "/" + filepath;
    
    Serial.printf("[WEB] Audio request: %s\n", filepath.c_str());
    
    // Open the file
    File file = LittleFS.open(filepath, "r");
    if (!file) {
        Serial.printf("[WEB] Audio file not found: %s\n", filepath.c_str());
        server.send(404, "text/plain", "Not Found");
        return;
    }
    
    size_t fileSize = file.size();
    server.sendHeader("Content-Type", "audio/mpeg");
    server.sendHeader("Content-Length", String(fileSize));
    server.sendHeader("Accept-Ranges", "bytes");
    server.setContentLength(fileSize);
    server.send(200, "audio/mpeg", "");
    
    // Stream file in chunks
    uint8_t buf[1024];
    while (file.available()) {
        size_t len = file.read(buf, sizeof(buf));
        server.client().write(buf, len);
    }
    file.close();
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
        "\"p\":%.1f,\"r\":%.1f,"
        "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
        "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}",
        gesture ? gesture : "---",
        d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4],
        d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4],
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
