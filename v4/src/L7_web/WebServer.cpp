#include "WebServer.h"
#include "Wifi.h"
#include "../L0_platform/Platform.h"
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L3_driver/Tmc2209.h"
#include "../L3_driver/Motion.h"
#include "../L6_telemetry_safety/OpState.h"

namespace WebServer {

static AsyncWebServer server(80);

// HTML-Inhalt liegt in eigener Datei (in Phase 2 noch hier inline).
// Der `frontend-design`-Skill wird das HTML in einer späteren Iteration
// überschreiben — die /status- und /cmd-API hier muss dann unverändert
// bleiben (siehe webdesign/briefing.md API-Vertrag).
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>perlin v4</title>
<style>
:root{--paper:#f4f1ea;--ink:#1a1a1a;--rule:#1a1a1a;--red:#d62828;--blue:#003049;--yellow:#fcbf49;--mute:#6b6b6b}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--paper);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Inter,"Helvetica Neue",Arial,sans-serif;font-size:16px;line-height:1.4}
body{padding:24px 16px;max-width:840px;margin:0 auto}
header{display:flex;justify-content:space-between;align-items:baseline;border-bottom:2px solid var(--rule);padding-bottom:12px;margin-bottom:24px}
header h1{font-size:1.6em;font-weight:700;letter-spacing:-0.02em}
header .meta{font-size:0.85em;color:var(--mute);font-variant-numeric:tabular-nums}
.strip{display:grid;grid-template-columns:repeat(3,1fr);border:2px solid var(--rule);margin-bottom:32px}
.strip .cell{padding:14px 16px;border-right:2px solid var(--rule)}
.strip .cell:last-child{border-right:none}
.strip .label{font-size:0.7em;text-transform:uppercase;letter-spacing:0.1em;color:var(--mute);margin-bottom:4px}
.strip .value{font-size:1.1em;font-weight:600;font-variant-numeric:tabular-nums}
h2{font-size:0.85em;text-transform:uppercase;letter-spacing:0.15em;font-weight:700;border-bottom:1px solid var(--rule);padding-bottom:6px;margin-bottom:16px}
.motors{display:grid;grid-template-columns:1fr;border:2px solid var(--rule);margin-bottom:32px}
@media(min-width:600px){.motors{grid-template-columns:1fr 1fr}}
.motor{padding:18px;border-bottom:2px solid var(--rule);display:grid;grid-template-columns:auto 1fr;gap:14px;align-items:center}
.motor .actions{grid-column:1/-1;display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
@media(min-width:600px){
  .motor{border-right:2px solid var(--rule)}
  .motor:nth-child(2n){border-right:none}
  .motor:nth-last-child(-n+2){border-bottom:none}
}
.motor:last-child{border-bottom:none}
.dot{width:24px;height:24px;border-radius:50%;background:transparent;border:2px solid var(--rule);flex-shrink:0}
.dot.on{background:var(--red);border-color:var(--red)}
.dot.off{background:var(--paper);border-color:var(--mute)}
.dot.dim{background:var(--paper);border-color:var(--mute);border-style:dashed}
.motor .name{font-weight:700;letter-spacing:0.05em}
.motor .state{font-size:0.85em;color:var(--mute);font-variant-numeric:tabular-nums}
.motor .pos{font-size:0.85em;color:var(--mute);font-variant-numeric:tabular-nums;margin-top:2px}
.motor button{font-family:inherit;font-size:0.7em;font-weight:700;letter-spacing:0.08em;text-transform:uppercase;padding:8px 12px;background:var(--paper);color:var(--ink);border:2px solid var(--rule);cursor:pointer;min-height:38px}
.motor button:active{background:var(--ink);color:var(--paper)}
.motor button:disabled{color:var(--mute);border-color:var(--mute);cursor:not-allowed}
.stop-bar{margin-bottom:24px}
.stop-bar button{width:100%;padding:14px;font-family:inherit;font-size:0.9em;font-weight:700;letter-spacing:0.15em;text-transform:uppercase;background:var(--red);color:#fff;border:2px solid var(--red);cursor:pointer}
.log-wrap{border-top:2px solid var(--rule);padding-top:16px}
.log-wrap h2{margin-bottom:10px}
#log{font-size:0.78em;line-height:1.5;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:var(--ink);max-height:220px;overflow-y:auto;font-variant-numeric:tabular-nums}
#log .row{display:grid;grid-template-columns:64px 1fr;gap:10px;padding:3px 0;border-bottom:1px dotted var(--mute)}
#log .ts{color:var(--mute);background:var(--yellow);padding:0 4px;font-weight:700;text-align:center}
footer{margin-top:24px;font-size:0.7em;color:var(--mute);text-align:right;border-top:1px solid var(--rule);padding-top:8px}
</style></head><body>
<header><h1>perlin v4</h1><div class="meta"><span id="fw">…</span> · <span id="ip">…</span></div></header>
<div class="strip">
  <div class="cell"><div class="label">Status</div><div class="value" id="op">—</div></div>
  <div class="cell"><div class="label">Uptime</div><div class="value"><span id="up">…</span> s</div></div>
  <div class="cell"><div class="label">RPM</div><div class="value" id="rpm">—</div></div>
</div>
<h2>Motoren</h2>
<div class="motors" id="motors"></div>
<div class="stop-bar"><button onclick="cmd('stop',0)">Emergency Stop</button></div>
<div class="log-wrap"><h2>Log</h2><div id="log"></div></div>
<footer>perlin v4 · phase 2</footer>
<script>
function cmd(a,m){fetch('/cmd?a='+a+'&m='+m)}
function pad(n){return String(n).padStart(2,'0')}
function ts(){const d=new Date();return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds())}
const NAMES=['X','Y','Z','E'];
const HAS_SENSOR=[true,true,true,false];
const motorsEl=document.getElementById('motors');
const log=document.getElementById('log');
NAMES.forEach((n,i)=>{
  const card=document.createElement('div');card.className='motor';
  card.innerHTML=`<span class="dot dim" id="d${i}"></span>
    <div><div class="name">M·${n}</div><div class="state" id="s${i}">—</div><div class="pos" id="p${i}"></div></div>
    <div class="actions">
      <button onclick="cmd('pwr',${i})">Power</button>
      <button onclick="cmd('setzero',${i})">Set Zero</button>
      ${HAS_SENSOR[i]?`<button onclick="cmd('cal',${i})">Calib</button><button onclick="cmd('home',${i})">Home</button>`:''}
    </div>`;
  motorsEl.appendChild(card);
});
function appendLog(msg){
  const row=document.createElement('div');row.className='row';
  const t=document.createElement('span');t.className='ts';t.textContent=ts();
  const m=document.createElement('span');m.textContent=msg;
  row.appendChild(t);row.appendChild(m);log.prepend(row);
  while(log.children.length>80) log.removeChild(log.lastChild);
}
setInterval(()=>{
  fetch('/status').then(r=>r.json()).then(s=>{
    document.getElementById('fw').textContent='v'+s.fw;
    document.getElementById('up').textContent=s.uptime_s;
    document.getElementById('ip').textContent=s.ip;
    document.getElementById('op').textContent=(s.op||'idle').toUpperCase();
    document.getElementById('rpm').textContent=(s.rpm!=null)?s.rpm:'—';
    const ms=s.m||[];
    for(let i=0;i<4;i++){
      const m=ms[i]||{};
      const dot=document.getElementById('d'+i),state=document.getElementById('s'+i),pos=document.getElementById('p'+i);
      if(m.e===true){dot.className='dot on';state.textContent='powered'}
      else if(m.e===false){dot.className='dot off';state.textContent='off'}
      else{dot.className='dot dim';state.textContent=i===3?'open-loop':'—'}
      pos.textContent=(m.p!=null)?(m.p.toFixed(1)+'°'):'';
    }
    if(s.log) s.log.split('\n').forEach(l=>{if(l.length>1)appendLog(l)});
  }).catch(()=>{})
},500);
</script></body></html>)HTML";

static String motorJson(uint8_t i) {
    String j;
    j.reserve(48);
    if (Tmc::ready()) {
        bool e = Tmc::isPowered(i);
        float p = Motion::getPositionDeg(i);
        j += "{\"e\":";
        j += e ? "true" : "false";
        j += ",\"p\":";
        j += String(p, 2);
        j += "}";
    } else {
        j += "{\"e\":null,\"p\":null}";
    }
    return j;
}

void begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        String log = Logger::drainBuffer();
        log.replace("\"", "'");
        log.replace("\n", "\\n");
        log.replace("\r", "");

        // RPM: nimm den ersten Motor mit Sensor, der eine Drehzahl meldet (in Phase 2 typisch nur Z)
        uint16_t rpm = 0;
        for (uint8_t i = 0; i < 4; i++) {
            if (HalPins::hasSensor(i)) {
                uint16_t v = HalTacho::getRpm(i);
                if (v > rpm) rpm = v;
            }
        }

        String json;
        json.reserve(512);
        json += "{\"fw\":\"";
        json += Platform::FW_VERSION;
        json += "\",\"uptime_s\":";
        json += String((unsigned long)(millis() / 1000));
        json += ",\"ip\":\"";
        json += Wifi::localIp();
        json += "\",\"op\":\"";
        json += Op::stateStr();
        json += "\",\"rpm\":";
        json += rpm > 0 ? String(rpm) : "null";
        json += ",\"log\":\"";
        json += log;
        json += "\",\"m\":[";
        for (uint8_t i = 0; i < 4; i++) {
            if (i > 0) json += ",";
            json += motorJson(i);
        }
        json += "]}";

        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", json);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    server.on("/cmd", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("a")) { r->send(400, "text/plain", "missing a"); return; }
        String a = r->getParam("a")->value();
        int m = r->hasParam("m") ? r->getParam("m")->value().toInt() : 0;

        // STOP und PWR sind immer erlaubt
        if (a == "stop") { Op::requestStop(); r->send(200, "text/plain", "OK"); return; }
        if (a == "pwr") {
            if (m < 0 || m >= 4) { r->send(400, "text/plain", "motor out of range"); return; }
            Op::requestPower(m, !Tmc::isPowered(m));
            r->send(200, "text/plain", "OK");
            return;
        }

        // Alle anderen nur im IDLE
        if (Op::isBusy()) { r->send(409, "text/plain", "BUSY"); return; }

        if (a == "setzero") { Op::requestSetZero(m); r->send(200, "text/plain", "OK"); return; }
        if (a == "home") {
            if (!HalPins::hasSensor(m)) { r->send(400, "text/plain", "no sensor"); return; }
            Op::requestHome(m); r->send(200, "text/plain", "OK"); return;
        }
        if (a == "cal") {
            if (!HalPins::hasSensor(m)) { r->send(400, "text/plain", "no sensor"); return; }
            Op::requestCalib(m); r->send(200, "text/plain", "OK"); return;
        }

        r->send(501, "text/plain", "unknown action");
    });

    server.begin();
    Logger::addLog("HTTP: server up on port 80");
}

} // namespace WebServer
