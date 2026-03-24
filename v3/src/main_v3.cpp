#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ElegantOTA.h>
#include "MotorControl.h"
#include "wifi_settings.h"

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>V3 Motor Lab</title>
  <style>
    body { font-family: sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 20px; }
    .container { max-width: 800px; margin: 0 auto; }
    .card { background: #1e1e1e; padding: 20px; border-radius: 8px; border-left: 5px solid #ff9800; margin-bottom: 20px; }
    .led { width: 15px; height: 15px; border-radius: 50%; background: #333; }
    .led.hit { background: #f44336; box-shadow: 0 0 10px #f44336; }
    .val { font-family: monospace; color: #00ff00; font-size: 1.4em; }
    .btn { background: #333; color: #fff; border: 1px solid #444; padding: 12px; border-radius: 4px; cursor: pointer; width: 100%; margin: 5px 0; font-size: 0.9em; }
    .btn.on { background: #4CAF50; border-color: #4CAF50; }
    .btn.test { background: #9c27b0; font-weight: bold; }
    .btn.stop { background: #f44336; padding: 20px; font-weight: bold; font-size: 1.2em; }
    #log { background: #000; color: #0f0; padding: 15px; border-radius: 4px; height: 250px; overflow-y: auto; margin-top: 20px; font-size: 0.85em; text-align: left; line-height: 1.5em; border: 1px solid #333; }
    .nav { margin-bottom: 20px; padding: 12px; background: #003366; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #ff9800; }
    .nav a { color: #fff; text-decoration: none; font-weight: bold; }
    .version { color: #ff9800; font-size: 0.8em; }
    .mask-card { background: #1a1a1a; border-left: 5px solid #37474f; border-radius: 8px; padding: 15px; margin-bottom: 20px; }
    .mask-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px; }
    .tog { padding: 10px; border-radius: 4px; border: 1px solid #444; cursor: pointer; font-size: 0.8em; text-align: center; background: #2e7d32; color: #fff; }
    .tog.off { background: #333; color: #666; border-color: #333; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
        <div style="display:flex; gap:20px;">
            <a href="http://perlin-bench.local/">← HAUPT-UI</a>
            <a href="/update">OTA UPDATE</a>
        </div>
        <span class="version" id="fwVer">V3 SINGLE-MOTOR ...</span>
    </div>

    <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:15px;">
          <h2 style="margin:0; color:#ff9800;">MOTOR X</h2>
          <div class="led" id="led0"></div>
        </div>
        <div style="display:grid; grid-template-columns: 1fr 1fr; gap: 20px;">
            <div>Winkel: <span class="val" id="pos0">0.0</span>°</div>
            <div>Speed: <span class="val" id="spd0">0</span> sps</div>
        </div>
        <div style="margin-top:20px; display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
          <button class="btn on" id="pwr0">POWER ON</button>
          <button class="btn" onclick="cmd('home', 0)">HOME MOTOR</button>
          <button class="btn" onclick="cmd('cal', 0)" style="background:#006064;">CALIB SENSOR</button>
          <button class="btn" onclick="cmd('learn', 0)" style="background:#455a64;">SG-LEARN</button>
        </div>
    </div>

    <div class="mask-card">
      <div class="mask-grid">
        <div class="tog" id="tog_speed" onclick="toggle('speed')">SPEED-TEST</div>
        <div class="tog" id="tog_accel" onclick="toggle('accel')">TRÄGHEIT</div>
        <div class="tog" id="tog_coast" onclick="toggle('coast')">COAST-TEST</div>
      </div>
      <button class="btn test" onclick="cmd('test', 0)" style="margin-top:15px;">START PARCOUR</button>
    </div>

    <button class="btn stop" onclick="cmd('stop', 0)">EMERGENCY STOP</button>
    <div id="log">Bereit. v3.4.2 (Full Logic Sanitize)</div>
  </div>

  <script>
    const cfg = { speed:1, accel:1, coast:1 };
    function toggle(k) { cfg[k]=cfg[k]?0:1; document.getElementById('tog_'+k).classList.toggle('off', !cfg[k]); }
    function cmd(a, m) { 
        let url = `/cmd?a=${a}&m=${m}`;
        if(a==='test') url += `&speed=${cfg.speed}&accel=${cfg.accel}&coast=${cfg.coast}`;
        fetch(url); 
    }
    function addLog(msg) {
      const log = document.getElementById('log');
      const div = document.createElement('div');
      div.innerText = `[${new Date().toLocaleTimeString()}] ${msg}`;
      log.prepend(div);
    }

    setInterval(() => {
      fetch('/status').then(r => r.json()).then(s => {
        document.getElementById('pos0').innerText = s.m[0].p.toFixed(1);
        document.getElementById('spd0').innerText = Math.round(s.m[0].s);
        document.getElementById('led0').className = s.hit ? 'led hit' : 'led';
        const pBtn = document.getElementById('pwr0');
        pBtn.innerText = s.m[0].e ? 'POWER ON' : 'POWER OFF';
        pBtn.className = s.m[0].e ? 'btn on' : 'btn';
        if(s.fw) document.getElementById('fwVer').innerText = 'V3 ' + s.fw;
        if(s.log) s.log.split('\\n').forEach(l => { if(l.length > 2) addLog(l); });
      }).catch(e => console.log("Offline..."));
    }, 350);
  </script>
</body>
</html>
)rawliteral";

void TaskCore1(void * pvParameters) {
    for(;;) {
        if (sys.pendingPower   >= 0) { setMotorPower(0, sys.pendingPower); sys.pendingPower = -1; }
        if (sys.pendingHome    != -1) { int m = sys.pendingHome;   sys.pendingHome   = -1; homeMotor(m); }
        if (sys.pendingCalib   != -1) { int m = sys.pendingCalib;  sys.pendingCalib  = -1; characterizeSensor(m); }
        if (sys.pendingLearn   != -1) { int m = sys.pendingLearn;  sys.pendingLearn  = -1; learnSGProfile(m); }
        if (sys.pendingTest    != -1) {
            int m = sys.pendingTest; sys.pendingTest = -1;
            clearTelemetry();
            if (sys.parcour.doSpeed) runSpeedTest(m);
            if (sys.parcour.doAccel) runInertiaTest(m);
            if (sys.parcour.doCoast) runCoastTest(m);
        }
        updateMotors();
        yield();
    }
}

void setup() {
    Serial.begin(115200);
    initMotors();
    WiFi.setHostname("perlin-v3"); WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
    unsigned long sW = millis(); while (WiFi.status() != WL_CONNECTED && millis()-sW < 8000) { delay(500); }
    if (WiFi.status() != WL_CONNECTED) { WiFi.mode(WIFI_AP); WiFi.softAP("perlin-v3-setup", "12345678"); }
    else { MDNS.begin("perlin-v3"); }
    
    ArduinoOTA.setHostname("perlin-v3"); ArduinoOTA.begin();
    
    server.on("/", [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });
    server.on("/status", [](AsyncWebServerRequest *r){
        portENTER_CRITICAL(&motorMux);
        String logData = sys.log; sys.log = "";
        portEXIT_CRITICAL(&motorMux);
        long pos = steppers[0]->currentPosition(); float spd = steppers[0]->speed();
        String j = "{\"hit\":" + String(digitalRead(TACHO_PIN)==LOW?"true":"false");
        j += ",\"fw\":\"" + String(FW_VERSION) + "\",\"log\":\"" + logData + "\"";
        j += ",\"m\":[{\"p\":"+String((pos%3200)*360.0/3200.0,1)+",\"s\":"+String(spd)+",\"e\":"+String(sys.m[0].enabled?"true":"false")+"}]}";
        r->send(200, "application/json", j);
    });
    server.on("/cmd", [](AsyncWebServerRequest *r){
        if(!r->hasParam("a")) { r->send(400); return; }
        String a = r->getParam("a")->value(); int m = r->hasParam("m")?r->getParam("m")->value().toInt():0;
        if(a=="pwr") sys.pendingPower = !sys.m[m].enabled;
        else if(a=="home") sys.pendingHome = m;
        else if(a=="cal") sys.pendingCalib = m;
        else if(a=="learn") sys.pendingLearn = m;
        else if(a=="test") {
            sys.parcour.doSpeed = !r->hasParam("speed") || r->getParam("speed")->value() == "1";
            sys.parcour.doAccel = !r->hasParam("accel") || r->getParam("accel")->value() == "1";
            sys.parcour.doCoast = !r->hasParam("coast") || r->getParam("coast")->value() == "1";
            sys.pendingTest = m;
        }
        else if(a=="stop") { sys.pendingStop = true; sys.pendingPower = 0; }
        r->send(200, "text/plain", "OK");
    });
    server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest *r){
        AsyncWebServerResponse *res = r->beginResponse(200, "text/csv", telemCSV);
        res->addHeader("Content-Disposition", "attachment; filename=\"parcour.csv\"");
        res->addHeader("Access-Control-Allow-Origin", "*"); // Fix X5
        r->send(res);
    });
    
    ElegantOTA.begin(&server, "admin", "12345678"); ElegantOTA.setAutoReboot(true);
    server.begin();
    
    // Fix X1: Start UART background task on Core 0
    xTaskCreatePinnedToCore([](void*){
        for(;;) { updateTelemCache(); vTaskDelay(pdMS_TO_TICKS(300)); }
    }, "TelemCache", 2048, NULL, 1, NULL, 0);

    xTaskCreatePinnedToCore(TaskCore1, "MotorTask", 10000, NULL, 1, NULL, 1);
}
void loop() { ArduinoOTA.handle(); }
