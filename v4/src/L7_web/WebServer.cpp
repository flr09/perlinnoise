#include "WebServer.h"
#include "Wifi.h"
#include "../L0_platform/Platform.h"
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L3_driver/Tmc2209.h"
#include "../L3_driver/Stepper.h"
#include "../L3_driver/Motion.h"
#include "../L5_programs/synthesis/RuntimeConfig.h"
#include "../L5_programs/synthesis/Synthesis.h"
#include "../L6_telemetry_safety/OpState.h"
#include "../L6_telemetry_safety/Telemetry.h"
#include "../L6_telemetry_safety/Watchdog.h"
#include "../L6_telemetry_safety/MotorProfile.h"

namespace WebServer {

static AsyncWebServer server(80);

// HTML-UI: Bauhaus-strict Variante, Design vom User via Browser-Claude
// (webdesign/variant-a-strict.html, 2026-04-26). Phase 4 → PHASE=4 setzt
// die UI alle Buttons und Bereiche frei.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>perlin v4</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --pa:#0e0e0e;--ink:#ededea;--red:#ff5a4a;--blue:#003049;
  --yel:#fcbf49;--mute:#7a7a78;--line:#ededea;
}
html,body{background:var(--pa);color:var(--ink);
  font:14px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Inter,"Helvetica Neue",Arial,sans-serif;
  font-variant-numeric:tabular-nums;-webkit-font-smoothing:antialiased}
.wrap{max-width:760px;margin:0 auto;padding:18px;border-left:1px solid var(--line);border-right:1px solid var(--line);min-height:100vh}
@media(max-width:780px){.wrap{border:0;padding:14px}}
.hd{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;padding-bottom:14px;border-bottom:2px solid var(--ink)}
.hd h1{font-size:30px;font-weight:800;letter-spacing:-.02em;line-height:.9}
.hd h1 b{background:var(--ink);color:var(--pa);padding:0 6px}
.hd .meta{text-align:right;font-size:11px;color:var(--mute);text-transform:uppercase;letter-spacing:.12em}
.hd .meta b{display:block;color:var(--ink);font-weight:600;font-size:12px;letter-spacing:.08em}
.sec{margin-top:22px}
.sec>.lbl{display:flex;align-items:center;gap:10px;margin-bottom:10px;
  font-size:11px;text-transform:uppercase;letter-spacing:.18em;font-weight:700}
.sec>.lbl .n{display:inline-block;width:22px;height:22px;background:var(--ink);color:var(--pa);
  text-align:center;line-height:22px;font-size:12px;font-weight:700;letter-spacing:0}
.sec>.lbl .ln{flex:1;height:1px;background:var(--ink)}
.noise{border:1px solid var(--ink);background:#111;position:relative;aspect-ratio:1/1;overflow:hidden}
.noise canvas{display:block;width:100%;height:100%}
.noise .tag{position:absolute;left:0;top:0;background:var(--ink);color:var(--pa);
  font-size:10px;text-transform:uppercase;letter-spacing:.16em;padding:4px 8px;font-weight:700}
.strip{display:grid;grid-template-columns:repeat(3,1fr);border:1px solid var(--ink)}
.strip>div{padding:14px;border-right:1px solid var(--ink)}
.strip>div:last-child{border-right:0}
.strip .k{font-size:10px;text-transform:uppercase;letter-spacing:.18em;color:var(--mute);margin-bottom:6px}
.strip .v{font-size:24px;font-weight:700;line-height:1}
.strip .v.busy{color:var(--red)}
.mg{display:grid;grid-template-columns:1fr;gap:0;border:1px solid var(--ink)}
@media(min-width:640px){.mg{grid-template-columns:1fr 1fr}}
.mc{padding:14px;border-bottom:1px solid var(--ink);border-right:1px solid var(--ink)}
.mc:nth-child(2n){border-right:0}
.mc:nth-last-child(-n+2){border-bottom:0}
@media(max-width:639px){
  .mc{border-right:0}
  .mc:not(:last-child){border-bottom:1px solid var(--ink)}
  .mc:last-child{border-bottom:0}
}
.mc .top{display:flex;align-items:center;gap:10px;margin-bottom:14px}
.dot{width:14px;height:14px;border-radius:50%;background:transparent;border:1.5px solid var(--ink);flex-shrink:0}
.dot.on{background:var(--red);border-color:var(--red)}
.dot.off{background:transparent;border-color:var(--ink)}
.dot.na{background:transparent;border:1.5px dashed var(--mute)}
.mc .name{font-size:18px;font-weight:800;letter-spacing:.04em}
.mc .st{margin-left:auto;font-size:10px;text-transform:uppercase;letter-spacing:.14em;color:var(--mute)}
.mc .st.on{color:var(--red)}
.mc .nums{display:flex;gap:18px;margin-bottom:14px;padding:8px 0;border-top:1px solid var(--ink);border-bottom:1px solid var(--ink)}
.mc .nums .nm{flex:1}
.mc .nums .k{font-size:9px;text-transform:uppercase;letter-spacing:.16em;color:var(--mute)}
.mc .nums .vv{font-size:16px;font-weight:600;margin-top:2px}
.mc .btns{display:grid;grid-template-columns:1fr 1fr;gap:0;border:1px solid var(--ink)}
.mc .btns.three{grid-template-columns:1fr 1fr 1fr}
.mc .btns.four{grid-template-columns:1fr 1fr;grid-auto-rows:1fr}
.btn{appearance:none;background:var(--pa);color:var(--ink);border:0;border-right:1px solid var(--ink);border-bottom:1px solid var(--ink);
  padding:0 8px;height:44px;font:inherit;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.12em;cursor:pointer;
  font-variant-numeric:tabular-nums}
.btn:last-child{border-right:0}
.btn:active{background:var(--ink);color:var(--pa)}
.btn[disabled]{color:var(--mute);cursor:not-allowed}
.btn.pri{background:var(--ink);color:var(--pa)}
.btns .btn:nth-last-child(-n+2){border-bottom:0}
.btns.three .btn:nth-last-child(-n+3){border-bottom:0}
.perf{border:1px solid var(--ink);padding:14px}
.perf .soon{display:inline-block;background:var(--ink);color:var(--pa);padding:3px 8px;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.14em;margin-bottom:8px}
.perf h3{font-size:14px;font-weight:800;text-transform:uppercase;letter-spacing:.12em;margin-bottom:6px}
.perf p{font-size:12px;color:var(--mute);max-width:46ch}
.perf .row{display:grid;grid-template-columns:1fr 2fr;gap:10px;align-items:center;margin-bottom:8px}
.perf .row label{font-size:10px;text-transform:uppercase;letter-spacing:.14em;color:var(--mute)}
.perf .row input,.perf .row select{appearance:none;background:var(--pa);color:var(--ink);border:1px solid var(--ink);padding:6px 8px;font:inherit;font-size:12px;width:100%}
.perf .row input[type=range]{padding:0;height:24px}
.perf .stopstart{display:grid;grid-template-columns:1fr 1fr;gap:0;border:1px solid var(--ink);margin-top:10px}
.perf .stopstart button{height:44px;background:var(--pa);color:var(--ink);border:0;border-right:1px solid var(--ink);font:inherit;font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.14em;cursor:pointer}
.perf .stopstart button:last-child{border-right:0}
.perf .stopstart button.run{background:var(--red);color:#fff}
.log{border:1px solid var(--ink);max-height:220px;overflow:auto;background:#1a1a1a}
.log .row{display:grid;grid-template-columns:auto 1fr;gap:10px;padding:6px 10px;border-bottom:1px solid #2a2a2a;font-size:12px;align-items:baseline}
.log .row:last-child{border-bottom:0}
.log .ts{background:var(--ink);color:var(--pa);padding:1px 6px;font-size:10px;font-weight:700;letter-spacing:.06em;font-variant-numeric:tabular-nums}
.log .msg{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px;color:var(--ink);word-break:break-word}
.ft{margin-top:24px;padding-top:14px;border-top:2px solid var(--ink);display:flex;justify-content:space-between;font-size:10px;text-transform:uppercase;letter-spacing:.18em;color:var(--mute)}
</style>
</head>
<body>
<div class="wrap">
  <header class="hd">
    <h1>perlin <b>v4</b></h1>
    <div class="meta">
      <b id="fw">…</b>
      <span id="ip">…</span>
    </div>
  </header>

  <section class="sec">
    <div class="lbl"><span class="n">1</span><span>Status</span><span class="ln"></span></div>
    <div class="strip">
      <div><div class="k">Op</div><div class="v" id="op">idle</div></div>
      <div><div class="k">Uptime</div><div class="v" id="up">0s</div></div>
      <div><div class="k">RPM</div><div class="v" id="rpm">—</div></div>
    </div>
  </section>

  <section class="sec">
    <div class="lbl"><span class="n">2</span><span>Motors</span><span class="ln"></span></div>
    <div class="mg" id="mg"></div>
  </section>

  <section class="sec">
    <div class="lbl"><span class="n">3</span><span>Performance</span><span class="ln"></span></div>
    <div class="perf">
      <h3>Movement Synthesis</h3>
      <div class="row"><label>Pattern</label>
        <select id="psType" onchange="setP('type',this.value|0)">
          <option value="0">Linear noise</option>
          <option value="1">Circle noise</option>
          <option value="2">Figure-8 noise</option>
          <option value="3">Sinus</option>
          <option value="4">Sawtooth</option>
          <option value="5">Square</option>
        </select></div>
      <div class="row"><label>Speed</label><input type="range" id="psSpeed" min="0" max="2" step="0.01" oninput="setP('speed',this.value)"></div>
      <div class="row"><label>Range (deg)</label><input type="range" id="psRange" min="30" max="360" step="1" oninput="setP('range',this.value)"></div>
      <div class="row"><label>Contrast</label><input type="range" id="psCont" min="0" max="2" step="0.01" oninput="setP('cont',this.value)"></div>
      <div class="row"><label>Frame</label><input type="range" id="psFrame" min="0.001" max="0.5" step="0.001" oninput="setP('frame',this.value)"></div>
      <div class="row"><label>Shape</label><input type="range" id="psShape" min="-5" max="5" step="0.1" oninput="setP('shape',this.value)"></div>
      <div class="row"><label>Spacing</label><input type="range" id="psMspace" min="0" max="100" step="1" oninput="setP('mspace',this.value)"></div>
      <div class="row"><label>Dynamics</label>
        <select id="psDyn" onchange="setP('dyn',this.value|0)">
          <option value="0">Langsam</option>
          <option value="1" selected>Normal</option>
          <option value="2">Rasant</option>
        </select></div>
      <div class="stopstart"><button onclick="setP('run',0)">Stop</button><button class="run" onclick="setP('run',1)">Start</button></div>
    </div>
  </section>

  <section class="sec">
    <div class="lbl"><span class="n">4</span><span>Log</span><span class="ln"></span></div>
    <div class="log" id="log"></div>
  </section>

  <footer class="ft">
    <span>perlin v4 · phase 4</span>
    <span>esp32 · web ui</span>
  </footer>
</div>

<script>
var PHASE=4;
var MNAMES=['M·X','M·Y','M·Z','M·E'];
var HAS_SENSOR=[1,1,1,0];

function cmd(a,m){fetch('/cmd?a='+a+(m!=null?'&m='+m:'')).catch(function(){})}
function setP(k,v){fetch('/set?'+k+'='+v).catch(function(){})}
function dotCls(e){return e===true?'on':e===false?'off':'na'}
function stTxt(e){return e===true?'powered':e===false?'off':'—'}

function renderMotors(M){
  var g=document.getElementById('mg'),h='';
  for(var i=0;i<4;i++){
    var m=M[i]||{},e=m.e,p=m.p,s=m.s,puls=m.pulses,hit=m.hit;
    var pos=(p==null?'—':(p.toFixed(1)+'°'));
    var spd=(s==null?'—':s);
    var aux=(puls!=null?puls+'p':'')+(hit===true?(puls!=null?' · HIT':'HIT'):'');
    var btns=[];
    btns.push({a:'pwr',l:'Power',cls:e===true?'pri':''});
    btns.push({a:'setzero',l:'Zero'});
    if(PHASE>=2 && HAS_SENSOR[i]){btns.push({a:'cal',l:'Calib'});btns.push({a:'home',l:'Home'})}
    var bcls=btns.length===2?'':btns.length===3?'three':'four';
    var bh='';for(var j=0;j<btns.length;j++){var b=btns[j];bh+='<button class="btn '+(b.cls||'')+'" data-a="'+b.a+'" data-m="'+i+'">'+b.l+'</button>'}
    h+='<div class="mc"><div class="top"><span class="dot '+dotCls(e)+'"></span><span class="name">'+MNAMES[i]+'</span><span class="st '+(e===true?'on':'')+'">'+stTxt(e)+'</span></div>'
      +'<div class="nums"><div class="nm"><div class="k">Pos</div><div class="vv">'+pos+'</div></div><div class="nm"><div class="k">Speed</div><div class="vv">'+spd+'</div></div><div class="nm"><div class="k">Sensor</div><div class="vv">'+(aux||'—')+'</div></div></div>'
      +'<div class="btns '+bcls+'">'+bh+'</div></div>';
  }
  g.innerHTML=h;
  g.querySelectorAll('.btn').forEach(function(b){b.addEventListener('click',function(){cmd(b.dataset.a,b.dataset.m)})});
}

function fmtUp(s){if(s<60)return s+'s';var m=Math.floor(s/60),r=s%60;if(m<60)return m+'m '+r+'s';var h=Math.floor(m/60);return h+'h '+(m%60)+'m'}

function appendLog(chunk){
  if(!chunk)return;
  var box=document.getElementById('log');
  var lines=chunk.split('\n');
  for(var i=0;i<lines.length;i++){
    var t=lines[i];if(!t)continue;
    var d=new Date(),ts=String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0');
    var row=document.createElement('div');row.className='row';
    row.innerHTML='<span class="ts">'+ts+'</span><span class="msg"></span>';
    row.lastChild.textContent=t;
    box.appendChild(row);
  }
  while(box.children.length>120)box.removeChild(box.firstChild);
  box.scrollTop=box.scrollHeight;
}

function apply(d){
  document.getElementById('fw').textContent=d.fw||'';
  document.getElementById('ip').textContent=d.ip||'';
  document.getElementById('op').textContent=d.op||'—';
  document.getElementById('op').className='v'+(d.op&&d.op!=='idle'?' busy':'');
  document.getElementById('up').textContent=fmtUp(d.uptime_s||0);
  document.getElementById('rpm').textContent=d.rpm==null?'—':d.rpm;
  renderMotors(d.m||[]);
  if(d.log)appendLog(d.log);
}

function loadConfig(){
  fetch('/config').then(function(r){return r.json()}).then(function(c){
    document.getElementById('psType').value=c.type;
    document.getElementById('psSpeed').value=c.speed;
    document.getElementById('psRange').value=c.range;
    document.getElementById('psCont').value=c.cont;
    document.getElementById('psFrame').value=c.frame;
    document.getElementById('psShape').value=c.shape;
    document.getElementById('psMspace').value=c.mspace;
    document.getElementById('psDyn').value=c.dyn;
  }).catch(function(){})
}

function poll(){fetch('/status').then(function(r){return r.json()}).then(apply).catch(function(){})}
loadConfig();
setInterval(poll,500);poll();
</script>
</body>
</html>
)HTML";

static String motorJson(uint8_t i) {
    String j;
    j.reserve(96);
    if (Tmc::ready()) {
        bool e = Tmc::isPowered(i);
        float p = Motion::getPositionDeg(i);
        auto* st = Stepper::get(i);
        int sps = (st && st->isRunning()) ? (int)(st->getCurrentSpeedInMilliHz() / 1000) : 0;
        j += "{\"e\":";
        j += e ? "true" : "false";
        j += ",\"p\":";
        j += String(p, 2);
        j += ",\"s\":";
        j += String(sps);
        if (HalPins::hasSensor(i)) {
            bool hit = digitalRead(HalPins::MOTORS[i].tachoPin) == LOW;
            j += ",\"hit\":";
            j += hit ? "true" : "false";
            j += ",\"pulses\":";
            j += String(HalTacho::getPulseCount(i));
        } else {
            j += ",\"hit\":null";
        }
        j += "}";
    } else {
        j += "{\"e\":null,\"p\":null,\"s\":null,\"hit\":null}";
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

        uint16_t rpm = 0;
        for (uint8_t i = 0; i < 4; i++) {
            if (HalPins::hasSensor(i)) {
                uint16_t v = HalTacho::getRpm(i);
                if (v > rpm) rpm = v;
            }
        }

        String json;
        json.reserve(640);
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

        if (a == "stop") { Op::requestStop(); r->send(200, "text/plain", "OK"); return; }
        if (a == "pwr") {
            if (m < 0 || m >= 4) { r->send(400, "text/plain", "motor out of range"); return; }
            Op::requestPower(m, !Tmc::isPowered(m));
            r->send(200, "text/plain", "OK");
            return;
        }

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
        if (a == "learn") { Op::pending.learn = m; r->send(200, "text/plain", "OK"); return; }
        if (a == "test") {
            int prog = r->hasParam("prog") ? r->getParam("prog")->value().toInt() : 0;
            Op::pending.testProg = prog;
            Op::pending.test = m;
            r->send(200, "text/plain", "OK"); return;
        }
        if (a == "show") { Op::pending.show = m; r->send(200, "text/plain", "OK"); return; }
        if (a == "synth") { Synthesis::start(); r->send(200, "text/plain", "OK"); return; }

        r->send(501, "text/plain", "unknown action");
    });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest* r) {
        auto& c = v4::rt;
        auto getF = [&](const char* k, float& out) {
            if (r->hasParam(k)) out = r->getParam(k)->value().toFloat();
        };
        auto getI = [&](const char* k, int& out) {
            if (r->hasParam(k)) out = r->getParam(k)->value().toInt();
        };
        if (r->hasParam("run")) {
            int v = r->getParam("run")->value().toInt();
            if (v) Synthesis::start(); else Synthesis::stop();
        }
        getI("type",   c.moveType);
        getF("speed",  c.speed);
        getF("angle",  c.angle);
        getF("rad",    c.radius);
        getF("range",  c.rangeDeg);
        getF("frame",  c.framesize);
        getF("cont",   c.contrast);
        getF("shape",  c.zShape);
        getF("edgec",  c.edgeC);
        getF("mspace", c.mspace);
        getI("dyn",    c.dynamics);
        getI("fan",    c.fan);
        getI("lamp",   c.lamp);
        r->send(200, "text/plain", "OK");
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* r) {
        const auto& c = v4::rt;
        char buf[400];
        snprintf(buf, sizeof(buf),
            "{\"run\":%s,\"type\":%d,\"speed\":%.3f,\"angle\":%.1f,"
            "\"rad\":%.1f,\"range\":%.1f,\"frame\":%.4f,\"cont\":%.2f,"
            "\"shape\":%.2f,\"edgec\":%.2f,\"mspace\":%.1f,\"dyn\":%d,"
            "\"fan\":%d,\"lamp\":%d}",
            c.running ? "true":"false", c.moveType, c.speed, c.angle,
            c.radius, c.rangeDeg, c.framesize, c.contrast,
            c.zShape, c.edgeC, c.mspace, c.dynamics,
            c.fan, c.lamp);
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    Telemetry::registerHandlers(server);

    server.on("/watchdog", HTTP_GET, [](AsyncWebServerRequest* r) {
        char buf[512];
        int n = 0;
        n += snprintf(buf+n, sizeof(buf)-n, "{\"motors\":[");
        for (uint8_t i = 0; i < 4; i++) {
            const auto& w = Watchdog::state[i];
            n += snprintf(buf+n, sizeof(buf)-n,
                "%s{\"act\":%s,\"trig\":%s,\"fault\":%u,\"err\":%u,\"settle\":%u,"
                "\"delta\":%ld,\"period\":%lu,\"profileValid\":%s,\"profilePts\":%u}",
                i == 0 ? "" : ",",
                w.active ? "true":"false", w.triggered ? "true":"false",
                w.lastFaultCode, w.errorCount, w.settleCount,
                w.lastDelta, (unsigned long)w.lastPeriodMs,
                MotorProfileNs::profiles[i].valid ? "true":"false",
                MotorProfileNs::profiles[i].count);
        }
        n += snprintf(buf+n, sizeof(buf)-n, "]}");
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    server.begin();
    Logger::addLog("HTTP: server up on port 80");
}

} // namespace WebServer
