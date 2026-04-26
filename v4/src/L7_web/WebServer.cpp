#include "WebServer.h"
#include "Wifi.h"
#include "../L0_platform/Platform.h"
#include "../L0_platform/Logger.h"
#include "../L3_driver/Tmc2209.h"

namespace WebServer {

static AsyncWebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="UTF-8">
<title>v4 — Phase 1</title>
<style>
body{font-family:sans-serif;background:#121212;color:#e0e0e0;padding:20px;max-width:600px;margin:auto}
h1{color:#ff9800}
.card{background:#1e1e1e;padding:16px;border-radius:8px;border-left:4px solid #ff9800;margin-bottom:14px}
.val{font-family:monospace;color:#0f0;font-size:1.2em}
button{background:#333;color:#fff;border:1px solid #555;padding:10px 16px;border-radius:4px;cursor:pointer}
button.on{background:#4CAF50;border-color:#4CAF50}
#log{background:#000;color:#0f0;padding:10px;height:180px;overflow-y:auto;font-size:0.85em;border-radius:4px}
</style></head><body>
<h1>v4 — Phase 1 Skelett</h1>
<div class="card">
  <div>FW: <span class="val" id="fw">…</span></div>
  <div>Uptime: <span class="val" id="up">…</span> s</div>
  <div>IP: <span class="val" id="ip">…</span></div>
</div>
<div class="card">
  <div>Motor X: <span class="val" id="px">…</span></div>
  <button id="pwrBtn" onclick="cmd('pwr',0)">POWER TOGGLE</button>
</div>
<div id="log"></div>
<script>
function cmd(a,m){fetch('/cmd?a='+a+'&m='+m)}
function pad(n){return n<10?'0'+n:n}
setInterval(()=>{
  fetch('/status').then(r=>r.json()).then(s=>{
    document.getElementById('fw').textContent=s.fw;
    document.getElementById('up').textContent=s.uptime_s;
    document.getElementById('ip').textContent=s.ip;
    const px=document.getElementById('px');
    px.textContent=s.m[0].e?'POWERED':'off';
    const b=document.getElementById('pwrBtn');
    b.className=s.m[0].e?'on':'';
    if(s.log){
      const log=document.getElementById('log');
      s.log.split('\n').forEach(l=>{if(l.length>1){const d=new Date();const div=document.createElement('div');div.textContent='['+pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds())+'] '+l;log.prepend(div)}});
    }
  }).catch(()=>{})
},500)
</script></body></html>)HTML";

void begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        String log = Logger::drainBuffer();
        log.replace("\"", "'");
        log.replace("\n", "\\n");
        log.replace("\r", "");

        String json;
        json.reserve(256);
        json += "{\"fw\":\"";
        json += Platform::FW_VERSION;
        json += "\",\"uptime_s\":";
        json += String((unsigned long)(millis() / 1000));
        json += ",\"ip\":\"";
        json += Wifi::localIp();
        json += "\",\"log\":\"";
        json += log;
        json += "\",\"m\":[{\"e\":";
        json += Tmc::isPoweredX() ? "true" : "false";
        json += "}]}";

        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", json);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("a")) { r->send(400, "text/plain", "missing a"); return; }
        String a = r->getParam("a")->value();
        int m = r->hasParam("m") ? r->getParam("m")->value().toInt() : 0;

        if (a == "pwr") {
            if (m == 0) {
                Tmc::setPowerX(!Tmc::isPoweredX());
                r->send(200, "text/plain", "OK");
            } else {
                r->send(501, "text/plain", "Motor " + String(m) + " in Phase 1 noch nicht aktiv");
            }
            return;
        }
        r->send(400, "text/plain", "unknown action");
    });

    server.begin();
    Logger::addLog("HTTP: server up on port 80");
}

} // namespace WebServer
