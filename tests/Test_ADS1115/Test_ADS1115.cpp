/*
 * @file Test_ADS1115.cpp
 * @brief ADS101x I2C ADC test -- reads A0-A3 and streams values to both
 *        serial console and a live web dashboard via WiFi AP + SSE.
 *        Supports both ADS1015 (12-bit) and ADS1115 (16-bit).
 *
 * Wiring:
 *   ADSx01x SDA  ->  GPIO 7  (IIC_SDA)
 *   ADSx01x SCL  ->  GPIO 6  (IIC_SCL)
 *   ADSx01x ADDR ->  GND     (I2C address 0x48)
 *   ADSx01x VDD  ->  3.3 V
 *
 * WiFi AP:  SSID "ADS_Test"  /  password "adstest"
 * Open browser ->  http://192.168.4.1
 */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_ADS1X15.h>

// -- ADS Variant Selection (1015 = 12-bit, 1115 = 16-bit) --------------------
#define ADS_VARIANT 1015    // Change to 1115 for ADS1115

#if ADS_VARIANT == 1015
    #define ADS_CLASS Adafruit_ADS1015
    #define ADS_BITS 12
    #define ADS_MV_PER_COUNT_ONE 2.0f    // mV per LSB for GAIN_ONE on ADS1015
    #define ADS_MAX_MV 4096.0f
#elif ADS_VARIANT == 1115
    #define ADS_CLASS Adafruit_ADS1115
    #define ADS_BITS 16
    #define ADS_MV_PER_COUNT_ONE 0.125f  // mV per LSB for GAIN_ONE on ADS1115
    #define ADS_MAX_MV 4096.0f
#else
    #error "ADS_VARIANT must be 1015 or 1115"
#endif

// -- Hardware ------------------------------------------------------------------
#define IIC_SDA   7
#define IIC_SCL   6
#define ADS_ADDR  0x48          // ADDR pin -> GND

// -- Gain ----------------------------------------------------------------------
#define ADS_GAIN          GAIN_ONE
#define ADS_MV_PER_COUNT  ADS_MV_PER_COUNT_ONE

// -- WiFi AP -------------------------------------------------------------------
static const char *AP_SSID = "ADS1115_Test";
static const char *AP_PASS = "ads1115test";
static const int   WEB_PORT = 80;

// -- SSE -----------------------------------------------------------------------
#define MAX_SSE_CLIENTS 4
static WebServer   server(WEB_PORT);
static WiFiClient  sse_clients[MAX_SSE_CLIENTS];
static bool        sse_active[MAX_SSE_CLIENTS] = {};

// -- ADSx01x ------------------------------------------------------------------
ADS_CLASS ads;

// -- Readings (updated each loop) ---------------------------------------------
static int16_t raw_counts[4] = {};
static float   voltage_mv[4] = {};

// =============================================================================
//  HTML Dashboard (PROGMEM)
// =============================================================================
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ADS1115 Live Monitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#c9d1d9;font-family:'Segoe UI',system-ui,sans-serif;
     display:flex;flex-direction:column;align-items:center;padding:20px;min-height:100vh}
h1{font-size:1.4rem;color:#58a6ff;margin-bottom:4px;letter-spacing:.5px}
.sub{font-size:.8rem;color:#8b949e;margin-bottom:24px}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:16px;width:100%;max-width:520px}
.card{background:#161b22;border:1px solid #30363d;border-radius:12px;
      padding:18px 20px;display:flex;flex-direction:column;gap:10px}
.card-title{font-size:.75rem;color:#8b949e;text-transform:uppercase;letter-spacing:1px}
.ch-label{font-size:1.5rem;font-weight:700}
.volt{font-size:1.1rem;color:#3fb950}
.raw{font-size:.8rem;color:#8b949e}
.bar-outer{height:8px;background:#21262d;border-radius:4px;overflow:hidden}
.bar-inner{height:100%;border-radius:4px;transition:width .2s ease}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;vertical-align:middle}
.dot.on{background:#3fb950}.dot.off{background:#f85149}
#status{margin-top:20px;font-size:.8rem;color:#8b949e}
</style>
</head>
<body>
<h1>&#x26A1; ADSx01x Live Monitor</h1>
<p class="sub" id="subtext">Single-ended &bull; GAIN_ONE &bull; &plusmn;4.096 V</p>
<div class="grid" id="grid"></div>
<div id="status"><span class="dot off" id="dot"></span>Connecting&hellip;</div>

<script>
const CHANNELS=['A0','A1','A2','A3'];
const COLORS=['#58a6ff','#3fb950','#f0883e','#bc8cff'];
const grid=document.getElementById('grid');
CHANNELS.forEach((ch,i)=>{
  grid.innerHTML+=`<div class="card">
    <div class="card-title">Channel</div>
    <div class="ch-label" style="color:${COLORS[i]}">${ch}</div>
    <div class="volt" id="v${i}">--- mV</div>
    <div class="raw"  id="r${i}">--- counts</div>
    <div class="bar-outer"><div class="bar-inner" id="b${i}"
         style="width:0%;background:${COLORS[i]}"></div></div>
  </div>`;
});

let src;
const dot=document.getElementById('dot');
const stat=document.getElementById('status');

function connect(){
  src=new EventSource('/events');
  src.onopen=()=>{
    dot.className='dot on';
    stat.innerHTML='<span class="dot on"></span>Connected &mdash; live data';
  };
  src.onerror=()=>{
    dot.className='dot off';
    stat.innerHTML='<span class="dot off"></span>Reconnecting&hellip;';
    setTimeout(connect,2000);
  };
  src.onmessage=e=>{
    try{
      const d=JSON.parse(e.data);
      d.ch.forEach((v,i)=>{
        document.getElementById('v'+i).textContent=v.mv.toFixed(3)+' mV';
        document.getElementById('r'+i).textContent=v.raw+' counts';
        const pct=Math.max(0,Math.min(100,(v.mv/4096)*100));
        document.getElementById('b'+i).style.width=pct.toFixed(1)+'%';
      });
      // Update subtitle with variant info (sent as 'variant' in JSON)
      document.getElementById('subtext').textContent=
        'ADS'+d.variant+' &bull; '+d.bits+'-bit &bull; '+d.lsb+' mV/LSB';
    }catch(err){}
  };
}
connect();
</script>
</body>
</html>
)rawliteral";

// =============================================================================
//  SSE helpers
// =============================================================================
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

static void handle_root() {
    server.send_P(200, "text/html", PAGE_HTML);
}

static void handle_events() {
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println();
    client.flush();

    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!sse_active[i] || !sse_clients[i].connected()) {
            sse_clients[i] = client;
            sse_active[i]  = true;
            break;
        }
    }
    server.client().stop(); // prevent WebServer from closing the held connection
}

// =============================================================================
//  Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n+==========================================+");
    Serial.printf("|  Test_ADS%d - Live Web + Serial ADC  |\n", ADS_VARIANT);
    Serial.println("|  Channels: A0  A1  A2  A3               |");
    Serial.println("+==========================================+\n");

    // -- I2C + ADSx01x --
    Serial.printf("  I2C  SDA = GPIO %d  |  SCL = GPIO %d\n", IIC_SDA, IIC_SCL);
    Serial.printf("  ADS%d  addr = 0x%02X  |  %d-bit\n", ADS_VARIANT, ADS_ADDR, ADS_BITS);

    Wire.begin(IIC_SDA, IIC_SCL);

    if (!ads.begin(ADS_ADDR, &Wire)) {
        Serial.printf("\n[ERROR] ADS%d not found! Check wiring and ADDR pin.\n", ADS_VARIANT);
        while (true) { delay(1000); Serial.printf("[ERROR] ADS%d halted.\n", ADS_VARIANT); }
    }
    ads.setGain(ADS_GAIN);
    Serial.printf("  ADS%d  [OK]  GAIN_ONE (+-4.096 V)\n\n", ADS_VARIANT);

    // -- WiFi AP --
    char ssid_buf[32];
    snprintf(ssid_buf, sizeof(ssid_buf), "ADS%d_Test", ADS_VARIANT);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid_buf, AP_PASS);
    delay(300);
    Serial.printf("  WiFi AP  SSID: %s\n", ssid_buf);
    Serial.printf("  WiFi AP  PASS: %s\n", AP_PASS);
    Serial.printf("  Dashboard  ->  http://%s\n\n", WiFi.softAPIP().toString().c_str());

    // -- HTTP routes --
    server.on("/",       handle_root);
    server.on("/events", handle_events);
    server.begin();
    Serial.println("  WebServer started.\n");

    Serial.println("-----------------------------------------------------");
    Serial.println("     A0 (mV)      A1 (mV)      A2 (mV)      A3 (mV)");
    Serial.println("-----------------------------------------------------");
}

// =============================================================================
//  Loop
// =============================================================================
void loop() {
    server.handleClient();

    // Read all 4 channels
    for (uint8_t ch = 0; ch < 4; ch++) {
        raw_counts[ch] = ads.readADC_SingleEnded(ch);
        voltage_mv[ch] = raw_counts[ch] * ADS_MV_PER_COUNT;
    }

    // -- Serial output --
    Serial.printf("  %9.3f    %9.3f    %9.3f    %9.3f\n",
                  voltage_mv[0], voltage_mv[1], voltage_mv[2], voltage_mv[3]);

    // -- SSE broadcast --
    char json[280];
    snprintf(json, sizeof(json),
        "{\"variant\":%d,\"bits\":%d,\"lsb\":%.3f,\"ch\":["
        "{\"raw\":%d,\"mv\":%.3f},"
        "{\"raw\":%d,\"mv\":%.3f},"
        "{\"raw\":%d,\"mv\":%.3f},"
        "{\"raw\":%d,\"mv\":%.3f}"
        "]}",
        ADS_VARIANT, ADS_BITS, ADS_MV_PER_COUNT,
        raw_counts[0], voltage_mv[0],
        raw_counts[1], voltage_mv[1],
        raw_counts[2], voltage_mv[2],
        raw_counts[3], voltage_mv[3]);

    sse_broadcast(String(json));

    delay(200); // ~5 updates per second
}
