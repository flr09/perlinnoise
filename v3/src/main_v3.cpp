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
    .btn.on  { background: #4CAF50; border-color: #4CAF50; }
    .btn.test { background: #9c27b0; font-weight: bold; }
    .btn.learn { background: #0277bd; font-weight: bold; }
    .btn.stop { background: #f44336; padding: 20px; font-weight: bold; font-size: 1.2em; }
    #log { background: #000; color: #0f0; padding: 15px; border-radius: 4px; height: 300px; overflow-y: auto; margin-top: 20px; font-size: 0.9em; text-align: left; line-height: 1.5em; border: 1px solid #333; }
    .nav { margin-bottom: 20px; padding: 12px; background: #003366; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #ff9800; }
    .nav a { color: #fff; text-decoration: none; font-weight: bold; }
    .version { color: #ff9800; font-size: 0.8em; }
    .learn-card { background: #1e1e1e; border-left: 5px solid #0277bd; border-radius: 8px; padding: 14px 20px; margin-bottom: 20px; }
    .learn-card h3 { margin: 0 0 10px; color: #29b6f6; font-size: 0.9em; text-transform: uppercase; }
    .param-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px; }
    .param { background: #111; border-radius: 4px; padding: 8px 12px; }
    .param .label { font-size: 0.65em; color: #666; text-transform: uppercase; }
    .param .value { font-family: monospace; color: #00ff00; font-size: 1.1em; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
        <a href="/update">OTA UPDATE</a>
        <span class="version" id="fwVer">V3 )</rawliteral" FW_VERSION R"rawliteral(</span>
    </div>

    <div class="card">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:15px;">
          <h2 style="margin:0; color:#ff9800;">MOTOR X</h2>
          <div style="font-size:0.8em; color:#888;">SENSOR: <span id="rawVal">---</span></div>
          <div class="led" id="led0"></div>
        </div>
        <div style="display:grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom:16px;">
            <div>Winkel: <span class="val" id="pos0">0.0</span>°</div>
            <div>Speed: <span class="val" id="spd0">0</span> sps</div>
        </div>
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
          <button class="btn on" id="pwr0">POWER ON</button>
          <button class="btn" onclick="cmd('home',0)">HOME MOTOR</button>
          <button class="btn" onclick="cmd('cal',0)" style="background:#006064;">CALIB SENSOR</button>
          <button class="btn learn" onclick="cmd('learn',0)">LEARN SG PROFILE</button>
          <button class="btn test" onclick="cmd('test',0)" style="grid-column:1/-1;">START PARCOUR</button>
        </div>
    </div>

    <div class="learn-card">
      <h3>Adaptives Tuning</h3>
      <div class="param-grid">
        <div class="param"><div class="label">Strom</div><div class="value"><span id="calCur">—</span> mA</div></div>
        <div class="param"><div class="label">SGTHRS</div><div class="value"><span id="calSG">—</span></div></div>
        <div class="param"><div class="label">Max RPM</div><div class="value"><span id="calRpm">—</span></div></div>
        <div class="param"><div class="label">Stabile Läufe</div><div class="value"><span id="calStable">—</span></div></div>
      </div>
    </div>

    <button class="btn stop" onclick="cmd('stop',0)">EMERGENCY STOP</button>
    <div id="log">Bereit. v)rawliteral" FW_VERSION R"rawliteral( – Adaptive Tuning aktiv.</div>
  </div>

  <script>
    function cmd(a, m) { fetch(`/cmd?a=${a}&m=${m}`); }
    function addLog(msg) {
      const log = document.getElementById('log');
      const div = document.createElement('div');
      div.innerText = `[${new Date().toLocaleTimeString()}] ${msg}`;
      log.prepend(div);
    }

    setInterval(() => {
      fetch('/status').then(r => r.json()).then(s => {
        const motor = s.m[0];
        document.getElementById('pos0').innerText = motor.p.toFixed(1);
        document.getElementById('spd0').innerText = Math.round(motor.s);
        document.getElementById('led0').className = s.hit ? 'led hit' : 'led';
        document.getElementById('rawVal').innerText = s.hit ? 'LOW' : 'HIGH';
        const pBtn = document.getElementById('pwr0');
        pBtn.innerText = motor.e ? 'POWER ON' : 'POWER OFF';
        pBtn.className = motor.e ? 'btn on' : 'btn';
        pBtn.onclick = () => cmd('pwr', 0);

        if (s.cal) {
          document.getElementById('calCur').innerText    = s.cal.cur    || '—';
          document.getElementById('calSG').innerText     = s.cal.sgthrs || '—';
          document.getElementById('calRpm').innerText    = s.cal.maxRpm ? s.cal.maxRpm.toFixed(0) : '—';
          document.getElementById('calStable').innerText = s.cal.stable != null ? s.cal.stable : '—';
        }

        if (s.log) {
          s.log.split('\\n').forEach(l => { if (l.length > 2) addLog(l); });
        }
      }).catch(e => console.log("Offline..."));
    }, 350);
  </script>
</body>
</html>
)rawliteral";

void TaskCore1(void * pvParameters) {
    for(;;) {
        if (sys.pendingHome   != -1) { int m = sys.pendingHome;   sys.pendingHome   = -1; homeMotor(m); }
        if (sys.pendingCalib  != -1) { int m = sys.pendingCalib;  sys.pendingCalib  = -1; characterizeSensor(m); }
        if (sys.pendingLearn  != -1) { int m = sys.pendingLearn;  sys.pendingLearn  = -1; learnSGProfile(m); }
        if (sys.pendingTest   != -1) { int m = sys.pendingTest;   sys.pendingTest   = -1; runSpeedTest(m); runInertiaTest(m); }
        updateMotors();
        vTaskDelay(1);
    }
}

void setup() {
    Serial.begin(115200);
    initMotors();
    WiFi.setHostname("perlin-v3");
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
    unsigned long startWiFi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 8000) { delay(500); }
    if (WiFi.status() != WL_CONNECTED) { WiFi.mode(WIFI_AP); WiFi.softAP("perlin-v3-setup", "12345678"); }
    else { MDNS.begin("perlin-v3"); }

    ArduinoOTA.setHostname("perlin-v3");
    ArduinoOTA.begin();

    server.on("/", [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });

    server.on("/status", [](AsyncWebServerRequest *r){
        portENTER_CRITICAL(&motorMux);
        String logData = sys.log; sys.log = "";
        portEXIT_CRITICAL(&motorMux);

        long pos = steppers[0]->currentPosition();
        float spd = steppers[0]->speed();
        float deg = (pos % 3200) * 360.0 / 3200.0;

        uint16_t cur    = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
        uint8_t  sgthrs = sys.cal[0].sgThrs;
        float    maxRpm = sys.cal[0].maxRpm;
        uint8_t  stable = sys.cal[0].stableRuns;

        String j = "{\"hit\":"  + String(digitalRead(TACHO_PIN) == LOW ? "true" : "false")
                 + ",\"fw\":\"" FW_VERSION "\""
                 + ",\"log\":\"" + logData + "\""
                 + ",\"m\":[{\"p\":" + String(deg, 1)
                 + ",\"s\":"  + String(spd)
                 + ",\"e\":"  + String(sys.m[0].enabled ? "true" : "false") + "}]"
                 + ",\"cal\":{\"cur\":" + String(cur)
                 + ",\"sgthrs\":"       + String(sgthrs)
                 + ",\"maxRpm\":"       + String(maxRpm, 1)
                 + ",\"stable\":"       + String(stable) + "}}";

        r->send(200, "application/json", j);
    });

    server.on("/cmd", [](AsyncWebServerRequest *r){
        if (!r->hasParam("a")) { r->send(400); return; }
        String a = r->getParam("a")->value();
        int m = r->hasParam("m") ? r->getParam("m")->value().toInt() : 0;
        if      (a == "pwr")   { setMotorPower(m, !sys.m[m].enabled); }
        else if (a == "home")  { sys.pendingHome  = m; }
        else if (a == "cal")   { sys.pendingCalib = m; }
        else if (a == "learn") { sys.pendingLearn = m; }
        else if (a == "test")  { sys.pendingTest  = m; }
        else if (a == "stop")  { sys.pendingStop  = true; setMotorPower(0, false); }
        r->send(200, "text/plain", "OK");
    });

    server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest *r){
        if (telemCSV.length() < 50) { r->send(204, "text/plain", "No data yet - run parcour first"); return; }
        String ts = String(millis());
        AsyncWebServerResponse *res = r->beginResponse(200, "text/csv; charset=utf-8", telemCSV);
        res->addHeader("Content-Disposition", "attachment; filename=\"parcour_" + ts + ".csv\"");
        res->addHeader("Cache-Control", "no-store");
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    ElegantOTA.begin(&server, "admin", "12345678");
    ElegantOTA.setAutoReboot(true);
    server.begin();
    xTaskCreatePinnedToCore(TaskCore1, "MotorTask", 10000, NULL, 1, NULL, 1);
}

void loop() { ArduinoOTA.handle(); }
