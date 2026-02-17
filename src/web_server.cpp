/*
 * @file web_server.cpp
 * @brief WiFi Soft-AP + async-style HTTP server with WebSocket
 *        Serves a real-time hand visualisation page.
 *
 * NOTE: Uses the built-in WiFi + WebServer classes (no external lib).
 *       A single WebSocket-style approach is emulated with Server-Sent
 *       Events (SSE) because the ESP32 Arduino core's WebServer lacks
 *       native WebSocket.  SSE is simpler and works in all browsers.
 */
#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>

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
body{background:#111;color:#eee;font-family:'Segoe UI',sans-serif;
     display:flex;flex-direction:column;align-items:center;padding:16px}
h1{font-size:1.3rem;margin-bottom:8px;color:#0af}
h2{font-size:1rem;color:#888;margin:12px 0 6px}
#gesture{font-size:2.4rem;color:#0fa;margin:16px 0;min-height:50px;
         text-align:center}
.bars{display:flex;gap:6px;justify-content:center;width:100%;max-width:360px}
.bar-wrap{display:flex;flex-direction:column;align-items:center;flex:1}
.bar-outer{width:100%;height:120px;background:#222;border-radius:6px;
           position:relative;overflow:hidden}
.bar-inner{position:absolute;bottom:0;width:100%;border-radius:6px;
           transition:height .15s}
.bar-lbl{font-size:.7rem;color:#999;margin-top:4px}
.flex .bar-inner{background:#0cf}
.hall .bar-inner{background:#f80}
#imu{font-size:.85rem;color:#aaa;margin-top:14px;text-align:center}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;
     margin-right:6px}
.dot.on{background:#0f0}.dot.off{background:#f00}
#status{margin-top:12px;font-size:.8rem}
</style>
</head>
<body>
<h1>&#x1F91F; Signa Glove</h1>
<div id="gesture">---</div>

<h2>Flex Sensors</h2>
<div class="bars flex" id="flex-bars"></div>

<h2>Hall Sensors</h2>
<div class="bars hall" id="hall-bars"></div>

<div id="imu"></div>
<div id="status"><span class="dot off" id="dot"></span>Connecting&hellip;</div>

<script>
const FINGERS=['Thumb','Index','Middle','Ring','Pinky'];
function makeBars(id){
  const c=document.getElementById(id);
  FINGERS.forEach(f=>{
    const w=document.createElement('div');w.className='bar-wrap';
    w.innerHTML='<div class="bar-outer"><div class="bar-inner" id="'+id+'-'+f+'"'+
      ' style="height:0%"></div></div><div class="bar-lbl">'+f[0]+'</div>';
    c.appendChild(w);
  });
}
makeBars('flex-bars');makeBars('hall-bars');

const gest=document.getElementById('gesture');
const imu=document.getElementById('imu');
const dot=document.getElementById('dot');
const stat=document.getElementById('status');

let src;
function connect(){
  src=new EventSource('/events');
  src.onopen=()=>{dot.className='dot on';stat.innerHTML='<span class="dot on"></span>Connected';};
  src.onerror=()=>{dot.className='dot off';stat.innerHTML='<span class="dot off"></span>Reconnecting&hellip;';};
  src.onmessage=e=>{
    try{
      const d=JSON.parse(e.data);
      gest.textContent=d.g||'---';
      FINGERS.forEach((f,i)=>{
        document.getElementById('flex-bars-'+f).style.height=(d.f[i]/4095*100).toFixed(1)+'%';
        document.getElementById('hall-bars-'+f).style.height=(d.h[i]/4095*100).toFixed(1)+'%';
      });
      imu.innerHTML='Pitch: '+d.p.toFixed(1)+'&deg; &nbsp; Roll: '+d.r.toFixed(1)+'&deg;';
    }catch(err){}
  };
}
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

    server.on("/",       handle_root);
    server.on("/events", handle_events);
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
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"g\":\"%s\","
        "\"f\":[%u,%u,%u,%u,%u],"
        "\"h\":[%u,%u,%u,%u,%u],"
        "\"p\":%.1f,\"r\":%.1f}",
        gesture ? gesture : "---",
        d.flex[0], d.flex[1], d.flex[2], d.flex[3], d.flex[4],
        d.hall[0], d.hall[1], d.hall[2], d.hall[3], d.hall[4],
        d.pitch, d.roll);

    sse_broadcast(String(buf));
}

bool web_server_is_running() { return running; }

int web_server_num_clients() {
    return WiFi.softAPgetStationNum();
}

String web_server_get_url() {
    return "http://" + WiFi.softAPIP().toString();
}
