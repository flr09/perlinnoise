#include "WebServer.h"
#include "Wifi.h"
#include "../L0_platform/Platform.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L2_storage/Storage_Calib.h"
#include "../L2_storage/Storage_Runtime.h"
#include "../L3_driver/Units.h"
#include "../L3_driver/Tmc2209.h"
#include "../L3_driver/Stepper.h"
#include "../L3_driver/Motion.h"
#include "../L5_programs/synthesis/RuntimeConfig.h"
#include "../L5_programs/synthesis/Synthesis.h"
#include "../L6_telemetry_safety/OpState.h"
#include "../L6_telemetry_safety/Telemetry.h"
#include "../L6_telemetry_safety/Watchdog.h"
#include "../L6_telemetry_safety/MotorProfile.h"
#include <ElegantOTA.h>

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
  font-variant-numeric:tabular-nums;-webkit-font-smoothing:antialiased;
  height:100vh;overflow:hidden}
@supports(height:100dvh){html,body{height:100dvh}}
.wrap{max-width:1200px;margin:0 auto;padding:14px;border-left:1px solid var(--line);border-right:1px solid var(--line);
  height:100vh;display:flex;flex-direction:column;overflow:hidden}
@supports(height:100dvh){.wrap{height:100dvh}}
@media(max-width:1220px){.wrap{border:0}}
.cols{display:grid;grid-template-columns:1fr;gap:14px;flex:1;min-height:0;overflow:hidden}
@media(min-width:920px){.cols{grid-template-columns:1fr 1fr;gap:18px}}
.cols>div{display:flex;flex-direction:column;gap:14px;min-height:0;overflow:auto}
.sec{margin-top:0}
.hd{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;padding-bottom:10px;margin-bottom:12px;border-bottom:2px solid var(--ink);flex:0 0 auto}
.hd h1{font-size:24px;font-weight:800;letter-spacing:-.02em;line-height:.9}
.hd h1 b{background:var(--ink);color:var(--pa);padding:0 6px}
.hd .nav{display:flex;align-items:center;gap:14px}
.hd .nav a{color:var(--ink);text-decoration:none;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.14em;border:1px solid var(--ink);padding:6px 10px}
.hd .nav a:hover{background:var(--ink);color:var(--pa)}
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
/* Live noise canvas */
.noise{border:1px solid var(--ink);background:#111;position:relative;aspect-ratio:1/1;overflow:hidden}
.noise canvas{display:block;width:100%;height:100%;image-rendering:pixelated}
.noise .tag{position:absolute;left:0;top:0;background:var(--ink);color:var(--pa);
  font-size:10px;text-transform:uppercase;letter-spacing:.16em;padding:4px 8px;font-weight:700}
/* Angle dials — 4 mini dials for X/Y/Z/E */
.dials{display:grid;grid-template-columns:repeat(4,1fr);gap:0;border:1px solid var(--ink);margin-top:12px}
.dial{padding:10px;border-right:1px solid var(--ink);text-align:center}
.dial:last-child{border-right:0}
.dial svg{width:100%;height:auto;max-width:80px;display:block;margin:0 auto}
.dial .dlabel{font-size:9px;text-transform:uppercase;letter-spacing:.16em;color:var(--mute);margin-top:4px}
.dial .dval{font-size:11px;font-weight:700;font-variant-numeric:tabular-nums;margin-top:2px}
.dial circle{fill:none;stroke:var(--ink);stroke-width:2}
.dial line{stroke:var(--red);stroke-width:3;stroke-linecap:round;transition:transform .15s linear;transform-origin:50% 50%}
.dial.off line{stroke:var(--mute)}
</style>
</head>
<body>
<div class="wrap">
  <header class="hd">
    <h1>perlin <b>v4</b></h1>
    <div class="nav">
      <a href="/test">Tests →</a>
      <div class="meta"><b id="fw">…</b><span id="ip">…</span></div>
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

  <div class="cols">
   <div>
    <section class="sec">
      <div class="lbl"><span class="n">2</span><span>Noise field · Motor angles</span><span class="ln"></span></div>
      <div class="noise"><span class="tag">Live · Simplex 2D</span><canvas id="noiseCanvas"></canvas></div>
      <div class="dials" id="dials"></div>
    </section>

    <section class="sec">
      <div class="lbl"><span class="n">3</span><span>Motors</span><span class="ln"></span></div>
      <div class="mg" id="mg"></div>
    </section>
   </div>

   <div>
    <section class="sec">
      <div class="lbl"><span class="n">4</span><span>Performance</span><span class="ln"></span></div>
      <div class="perf">
      <h3>Movement Synthesis</h3>
      <div class="caps" id="caps" style="font-size:10px;letter-spacing:.10em;text-transform:uppercase;color:var(--mute);margin:0 0 8px 0;border-left:2px solid var(--ink);padding-left:8px;line-height:1.5">Engine-Cap: lädt …</div>
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
      <div class="lbl"><span class="n">5</span><span>Log</span><span class="ln"></span></div>
      <div class="log" id="log"></div>
    </section>
   </div>
  </div>

  <footer class="ft">
    <span>perlin v4 · phase 6</span>
    <span>esp32 · web ui</span>
  </footer>
</div>

<script>
var PHASE=4;
var MNAMES=['M·X','M·Y','M·Z','M·E'];
var HAS_SENSOR=[1,1,1,0];

function cmd(a,m){fetch('/cmd?a='+a+(m!=null?'&m='+m:'')).catch(function(){})}
function setP(k,v){fetch('/set?'+k+'='+v).then(function(r){return r.json()}).then(applyConfig).catch(function(){})}
function applyConfig(c){
  if(!c)return;
  var f=function(id,v){var e=document.getElementById(id);if(e&&v!=null)e.value=v};
  f('psType',c.type);f('psSpeed',c.speed);f('psRange',c.range);f('psCont',c.cont);
  f('psFrame',c.frame);f('psShape',c.shape);f('psMspace',c.mspace);f('psDyn',c.dyn);
}
function dotCls(e){return e===true?'on':e===false?'off':'na'}
function stTxt(e){return e===true?'powered':e===false?'off':'—'}

/* DIALS --------------------------------------------------------------- */
function buildDials(){
  var d=document.getElementById('dials'),h='';
  for(var i=0;i<4;i++){
    h+='<div class="dial off" id="di'+i+'"><svg viewBox="0 0 60 60" aria-hidden="true">'
      +'<circle cx="30" cy="30" r="26"/>'
      +'<line x1="30" y1="30" x2="30" y2="6" id="dl'+i+'"/>'
      +'</svg><div class="dlabel">'+MNAMES[i]+'</div><div class="dval" id="dv'+i+'">—</div></div>';
  }
  d.innerHTML=h;
}
function updateDials(M){
  for(var i=0;i<4;i++){
    var m=M[i]||{},p=m.p,e=m.e;
    var di=document.getElementById('di'+i),dl=document.getElementById('dl'+i),dv=document.getElementById('dv'+i);
    if(p==null){dv.textContent='—';di.className='dial off';continue}
    dv.textContent=p.toFixed(0)+'°';
    di.className='dial'+(e===true?'':' off');
    dl.style.transform='rotate('+p+'deg)';
  }
}

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
  var ms=d.m||[];
  renderMotors(ms);
  updateDials(ms);
  if(d.log)appendLog(d.log);
}

/* CANVAS-PREVIEW (10 Hz Polling von /preview) ------------------------- */
/* Engine liefert je nach moveType: 32x32 Noise-Pixmap, 128-Byte Wave    */
/* (Bit7 = Phasen-Marker), oder w=0 für Idle/Step.                       */
(function(){
  var c=document.getElementById('noiseCanvas');if(!c)return;
  var ctx=c.getContext('2d');
  c.width=128;c.height=128;
  var idleHue=0;
  function drawIdle(){
    ctx.fillStyle='#0b0e14';ctx.fillRect(0,0,128,128);
    idleHue=(idleHue+1)%360;
    ctx.fillStyle='hsl('+idleHue+',60%,18%)';
    ctx.font='10px monospace';ctx.textAlign='center';
    ctx.fillText('idle',64,68);
  }
  function drawNoise(data){
    var img=ctx.createImageData(32,32),d=img.data;
    for(var k=0;k<1024;k++){
      var g=data[k]||0;
      d[k*4]=d[k*4+1]=d[k*4+2]=g;d[k*4+3]=255;
    }
    // 32x32 → 128x128 hochskaliert via temporäres Canvas
    var tmp=document.createElement('canvas');tmp.width=32;tmp.height=32;
    tmp.getContext('2d').putImageData(img,0,0);
    ctx.imageSmoothingEnabled=false;
    ctx.drawImage(tmp,0,0,128,128);
  }
  function drawWave(data){
    ctx.fillStyle='#0b0e14';ctx.fillRect(0,0,128,128);
    ctx.strokeStyle='#7fd0ff';ctx.lineWidth=1;
    ctx.beginPath();
    for(var x=0;x<128;x++){
      var b=data[x]||0;
      var v=b&0x7f;          // unteres 7 Bit = Sample (0..127)
      var y=64-(v-64);        // zentriert um Mitte
      if(x===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
    // Phasen-Marker (Bit7 gesetzt) als kurze vertikale Linie
    ctx.strokeStyle='#ffb84d';
    for(var i=0;i<128;i++){
      if(data[i]&0x80){
        ctx.beginPath();ctx.moveTo(i,0);ctx.lineTo(i,12);ctx.stroke();
      }
    }
  }
  function drawPreview(){
    if(document.visibilityState==='hidden')return;
    fetch('/preview').then(function(r){return r.json()}).then(function(j){
      if(!j||!j.data||!j.data.length){drawIdle();return}
      var mode=j.mode|0;
      if(mode<=2)drawNoise(j.data);
      else if(mode<=5)drawWave(j.data);
      else drawIdle();
    }).catch(function(){drawIdle()});
  }
  setInterval(drawPreview,100);drawPreview();
})();
buildDials();

function loadConfig(){
  fetch('/config').then(function(r){return r.json()}).then(applyConfig).catch(function(){})
}

function loadBounds(){
  fetch('/bounds').then(function(r){return r.json()}).then(function(b){
    var rows=[];
    for(var i=0;i<4;i++){
      var m=b.motors[i];
      if(m.valid){
        rows.push(MNAMES[i]+': '+m.maxRpm+' rpm · '+(m.maxAccel/1000).toFixed(0)+'k acc');
      } else {
        rows.push(MNAMES[i]+': uncalibrated → 8000 sps / 4000 acc default');
      }
    }
    document.getElementById('caps').innerHTML='Engine-Cap (95% margin)<br>'+rows.join('<br>');
  }).catch(function(){
    document.getElementById('caps').textContent='Engine-Cap: /bounds nicht erreichbar';
  })
}

function poll(){
  if(document.visibilityState==='hidden')return;
  fetch('/status').then(function(r){return r.json()}).then(apply).catch(function(){})
}
loadConfig();
loadBounds();
setInterval(poll,100);poll();   // 10 Hz Status-Polling
</script>
</body>
</html>
)HTML";

// Engineering-Testbench (Subpage /test) — analog v3 "Motor Lab"
static const char TEST_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>perlin v4 · tests</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--pa:#0e0e0e;--ink:#ededea;--red:#ff5a4a;--yel:#fcbf49;--mute:#7a7a78;--line:#ededea;--blue:#003049;--green:#4caf50}
html,body{background:var(--pa);color:var(--ink);font:14px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Inter,"Helvetica Neue",Arial,sans-serif;font-variant-numeric:tabular-nums;height:100vh;overflow:hidden}
@supports(height:100dvh){html,body{height:100dvh}}
.wrap{max-width:1200px;margin:0 auto;padding:14px;border-left:1px solid var(--line);border-right:1px solid var(--line);height:100vh;display:flex;flex-direction:column;overflow:hidden}
@supports(height:100dvh){.wrap{height:100dvh}}
@media(max-width:1220px){.wrap{border:0}}
.hd{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;padding-bottom:10px;margin-bottom:12px;border-bottom:2px solid var(--ink);flex:0 0 auto}
.hd h1{font-size:24px;font-weight:800;letter-spacing:-.02em;line-height:.9}
.hd h1 b{background:var(--yel);color:var(--pa);padding:0 6px}
.hd .nav{display:flex;align-items:center;gap:14px}
.hd .nav a{color:var(--ink);text-decoration:none;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.14em;border:1px solid var(--ink);padding:6px 10px}
.hd .meta{text-align:right;font-size:11px;color:var(--mute);text-transform:uppercase;letter-spacing:.12em}
.hd .meta b{display:block;color:var(--ink);font-weight:600;font-size:12px;letter-spacing:.08em}
.strip{display:grid;grid-template-columns:repeat(4,1fr);border:1px solid var(--ink);margin-bottom:12px;flex:0 0 auto}
.strip>div{padding:10px 12px;border-right:1px solid var(--ink)}
.strip>div:last-child{border-right:0}
.strip .k{font-size:9px;text-transform:uppercase;letter-spacing:.16em;color:var(--mute);margin-bottom:4px}
.strip .v{font-size:18px;font-weight:700;line-height:1}
.strip .v.busy{color:var(--red)}
.strip .v.ok{color:var(--green)}
.cols{display:grid;grid-template-columns:1fr;gap:14px;flex:1;min-height:0;overflow:hidden}
@media(min-width:920px){.cols{grid-template-columns:1fr 1fr;gap:18px}}
.cols>div{display:flex;flex-direction:column;gap:12px;min-height:0;overflow:auto}
.sec{display:flex;flex-direction:column;gap:6px}
.sec>.lbl{display:flex;align-items:center;gap:10px;font-size:10px;text-transform:uppercase;letter-spacing:.18em;font-weight:700}
.sec>.lbl .n{display:inline-block;width:20px;height:20px;background:var(--ink);color:var(--pa);text-align:center;line-height:20px;font-size:11px;font-weight:700}
.sec>.lbl .ln{flex:1;height:1px;background:var(--ink)}
.mc{border:1px solid var(--ink);padding:10px}
.mc .top{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.dot{width:12px;height:12px;border-radius:50%;border:1.5px solid var(--ink);flex-shrink:0}
.dot.on{background:var(--red);border-color:var(--red)}
.dot.off{background:transparent}
.dot.na{border-style:dashed;border-color:var(--mute)}
.dot.hit{background:var(--green);border-color:var(--green);box-shadow:0 0 6px var(--green)}
.mc .nm{font-size:14px;font-weight:800;letter-spacing:.04em;flex:1}
.mc .nums{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:8px;padding:6px 0;border-top:1px solid var(--ink);border-bottom:1px solid var(--ink)}
.mc .nums .k{font-size:8px;text-transform:uppercase;letter-spacing:.14em;color:var(--mute)}
.mc .nums .vv{font-size:13px;font-weight:600;margin-top:1px}
.mc .nums .vv.hit{color:var(--green);font-weight:800}
.btns{display:grid;gap:0;border:1px solid var(--ink)}
.btns.g3{grid-template-columns:repeat(3,1fr)}
.btns.g4{grid-template-columns:repeat(4,1fr)}
.btn{appearance:none;background:var(--pa);color:var(--ink);border:0;border-right:1px solid var(--ink);border-bottom:1px solid var(--ink);padding:0 6px;height:36px;font:inherit;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.1em;cursor:pointer}
.btn:last-child{border-right:0}
.btn:active{background:var(--ink);color:var(--pa)}
.btn[disabled]{color:var(--mute);cursor:not-allowed}
.btn.pri{background:var(--ink);color:var(--pa)}
.btn.go{background:var(--yel);color:var(--pa)}
.btn.stop{background:var(--red);color:#fff}
.btns .btn:nth-last-child(-n+3){border-bottom:0}
.tests{border:1px solid var(--ink);padding:10px;display:flex;flex-direction:column;gap:8px}
.tests select,.tests input{appearance:none;background:var(--pa);color:var(--ink);border:1px solid var(--ink);padding:6px 8px;font:inherit;font-size:12px;width:100%}
.tests .row{display:grid;grid-template-columns:1fr 2fr;gap:10px;align-items:center}
.tests label{font-size:10px;text-transform:uppercase;letter-spacing:.14em;color:var(--mute)}
.tests .runs{display:grid;grid-template-columns:1fr 1fr;gap:0;border:1px solid var(--ink);margin-top:4px}
.tests .runs button{height:38px;background:var(--pa);color:var(--ink);border:0;border-right:1px solid var(--ink);font:inherit;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.12em;cursor:pointer}
.tests .runs button.go{background:var(--yel);color:var(--pa)}
.tests .runs button:last-child{border-right:0}
.wd{border:1px solid var(--ink);padding:10px;display:grid;grid-template-columns:1fr 1fr;gap:6px;font-size:11px}
.wd .k{color:var(--mute);font-size:9px;text-transform:uppercase;letter-spacing:.14em}
.wd .v{font-weight:700;font-variant-numeric:tabular-nums}
.wd .v.act{color:var(--green)}
.wd .v.fault{color:var(--red)}
.tele{display:flex;gap:8px}
.tele a{flex:1;text-align:center;text-decoration:none;color:var(--ink);border:1px solid var(--ink);padding:8px;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.12em}
.tele a:hover{background:var(--ink);color:var(--pa)}
.estop{flex:0 0 auto}
.estop button{width:100%;height:46px;background:var(--red);color:#fff;border:0;font:inherit;font-size:13px;font-weight:800;text-transform:uppercase;letter-spacing:.18em;cursor:pointer}
.log{flex:1;border:1px solid var(--ink);overflow:auto;background:#1a1a1a;min-height:160px}
.log .row{display:grid;grid-template-columns:auto 1fr;gap:8px;padding:4px 8px;border-bottom:1px solid #2a2a2a;font-size:11px}
.log .ts{background:var(--ink);color:var(--pa);padding:1px 5px;font-size:9px;font-weight:700}
.log .msg{font-family:ui-monospace,Menlo,Consolas,monospace;color:var(--ink);word-break:break-word}
</style>
</head>
<body>
<div class="wrap">
  <header class="hd">
    <h1>perlin <b>tests</b></h1>
    <div class="nav">
      <a href="/">← Steuerung</a>
      <div class="meta"><b id="fw">…</b><span id="ip">…</span></div>
    </div>
  </header>

  <div class="strip">
    <div><div class="k">Op</div><div class="v" id="op">idle</div></div>
    <div><div class="k">Uptime</div><div class="v" id="up">0s</div></div>
    <div><div class="k">RPM</div><div class="v" id="rpm">—</div></div>
    <div><div class="k">Active</div><div class="v" id="act">—</div></div>
  </div>

  <div class="cols">
   <div>
    <section class="sec">
      <div class="lbl"><span class="n">1</span><span>Motors</span><span class="ln"></span></div>
      <div id="mlist"></div>
    </section>
   </div>

   <div>
    <section class="sec">
      <div class="lbl"><span class="n">2</span><span>Engineering Tests</span><span class="ln"></span></div>
      <div class="tests">
        <div class="row"><label>Motor</label>
          <select id="tMotor"><option value="0">M·X</option><option value="1">M·Y</option><option value="2" selected>M·Z</option><option value="3">M·E</option></select></div>
        <div class="row"><label>Programm</label>
          <select id="tProg">
            <option value="0">Speed Test</option>
            <option value="1">Inertia Test</option>
            <option value="2">Coast Test</option>
            <option value="3">Katapult</option>
            <option value="4">Freq Sweep</option>
            <option value="5">Current Sweep HiRPM</option>
          </select></div>
        <div class="runs"><button onclick="runTest()" class="go">Run</button><button onclick="cmd('show',getMotor())">Performance Show</button></div>
      </div>
    </section>

    <section class="sec">
      <div class="lbl"><span class="n">3</span><span>Watchdog · Profile</span><span class="ln"></span></div>
      <div class="wd" id="wd"></div>
    </section>

    <section class="sec">
      <div class="lbl"><span class="n">4</span><span>Telemetrie</span><span class="ln"></span></div>
      <div class="tele">
        <a href="/telemetry" download>CSV download</a>
        <a href="/config" target="_blank">/config</a>
        <a href="/watchdog" target="_blank">/watchdog raw</a>
      </div>
    </section>

    <div class="estop"><button onclick="cmd('stop',0)">Emergency Stop</button></div>

    <section class="sec" style="flex:1;min-height:0;display:flex;flex-direction:column">
      <div class="lbl"><span class="n">5</span><span>Log</span><span class="ln"></span></div>
      <div class="log" id="log"></div>
    </section>
   </div>
  </div>
</div>

<script>
var MNAMES=['M·X','M·Y','M·Z','M·E'];
var HAS_SENSOR=[1,1,1,0];

function cmd(a,m){fetch('/cmd?a='+a+(m!=null?'&m='+m:'')).catch(function(){})}
function getMotor(){return document.getElementById('tMotor').value|0}
function runTest(){var m=getMotor(),p=document.getElementById('tProg').value|0;fetch('/cmd?a=test&m='+m+'&prog='+p).catch(function(){})}

function dotCls(e,hit){if(hit===true)return 'hit';return e===true?'on':e===false?'off':'na'}
function fmtUp(s){if(s<60)return s+'s';var m=Math.floor(s/60),r=s%60;if(m<60)return m+'m '+r+'s';var h=Math.floor(m/60);return h+'h '+(m%60)+'m'}

function renderMotors(M){
  var box=document.getElementById('mlist'),h='';
  for(var i=0;i<4;i++){
    var m=M[i]||{},e=m.e,p=m.p,s=m.s,puls=m.pulses,hit=m.hit;
    var pos=(p==null?'—':p.toFixed(1)+'°');
    var spd=(s==null?'—':s);
    var pulsTxt=(puls==null?'—':puls);
    var hitTxt=(hit===true?'HIT':hit===false?'—':'n/a');
    var btns=[];
    btns.push({a:'pwr',l:'Power',cls:e===true?'pri':''});
    btns.push({a:'setzero',l:'Zero'});
    if(HAS_SENSOR[i]){btns.push({a:'cal',l:'Calib'});btns.push({a:'home',l:'Home'});btns.push({a:'learn',l:'SG'})}
    var bcls=btns.length===2?'g2':btns.length===5?'g3':'g3';
    var bh='';for(var j=0;j<btns.length;j++){var b=btns[j];bh+='<button class="btn '+(b.cls||'')+'" onclick="cmd(\''+b.a+'\','+i+')">'+b.l+'</button>'}
    h+='<div class="mc"><div class="top"><span class="dot '+dotCls(e,hit)+'"></span><span class="nm">'+MNAMES[i]+'</span></div>'
      +'<div class="nums">'
      +'<div><div class="k">Pos</div><div class="vv">'+pos+'</div></div>'
      +'<div><div class="k">Speed</div><div class="vv">'+spd+'</div></div>'
      +'<div><div class="k">Pulses</div><div class="vv">'+pulsTxt+'</div></div>'
      +'<div><div class="k">Sensor</div><div class="vv'+(hit===true?' hit':'')+'">'+hitTxt+'</div></div>'
      +'</div>'
      +'<div class="btns '+bcls+'">'+bh+'</div></div>';
  }
  box.innerHTML=h;
}

function renderWd(W){
  if(!W)return;
  var box=document.getElementById('wd'),h='';
  W.motors.forEach(function(w,i){
    if(!HAS_SENSOR[i])return;
    h+='<div><span class="k">M·'+'XYZE'[i]+' state</span><div class="v '+(w.trig?'fault':w.act?'act':'')+'">'
       +(w.trig?'FAULT 0b'+w.fault.toString(2).padStart(3,'0'):w.act?'armed':'idle')+'</div></div>';
    h+='<div><span class="k">M·'+'XYZE'[i]+' profile</span><div class="v">'+(w.profileValid?(w.profilePts+' pts'):'unlearned')+'</div></div>';
  });
  box.innerHTML=h;
}

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
  while(box.children.length>200)box.removeChild(box.firstChild);
  box.scrollTop=box.scrollHeight;
}

function pollStatus(){
  fetch('/status').then(function(r){return r.json()}).then(function(d){
    document.getElementById('fw').textContent=d.fw||'';
    document.getElementById('ip').textContent=d.ip||'';
    document.getElementById('op').textContent=d.op||'—';
    document.getElementById('op').className='v'+(d.op&&d.op!=='idle'?' busy':'');
    document.getElementById('up').textContent=fmtUp(d.uptime_s||0);
    document.getElementById('rpm').textContent=d.rpm==null?'—':d.rpm;
    var ms=d.m||[],actCount=0;
    ms.forEach(function(m){if(m&&m.e===true)actCount++});
    document.getElementById('act').textContent=actCount+'/4 powered';
    document.getElementById('act').className='v'+(actCount?' ok':'');
    renderMotors(ms);
    if(d.log)appendLog(d.log);
  }).catch(function(){})
}

function pollWd(){
  fetch('/watchdog').then(function(r){return r.json()}).then(renderWd).catch(function(){})
}

setInterval(pollStatus,500);pollStatus();
setInterval(pollWd,1000);pollWd();
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
    // Bug-ID 23a: Runtime-Slider-Werte aus NVS laden, bevor WebServer-
    // Endpoints arbeiten. Wenn NVS leer/stale, bleiben die Defaults aus
    // RuntimeConfig.h.
    StorageRuntime::load(v4::rt);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "text/html", FPSTR(INDEX_HTML));
    });
    server.on("/test", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(200, "text/html", FPSTR(TEST_HTML));
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
        if (a == "resetTele") { Telemetry::resetBuffer(); r->send(200, "text/plain", "OK"); return; }
        if (a == "synth") { Synthesis::start(); r->send(200, "text/plain", "OK"); return; }
        // Bug-ID 23b: Preset-Slots in NVS (analog v1, aber Server-seitig)
        if (a == "savep") {
            int slot = r->hasParam("slot") ? r->getParam("slot")->value().toInt() : -1;
            if (slot < 0 || slot >= 8) { r->send(400, "text/plain", "slot 0..7"); return; }
            StorageRuntime::savePreset((uint8_t)slot, v4::rt);
            r->send(200, "text/plain", "OK"); return;
        }
        if (a == "loadp") {
            int slot = r->hasParam("slot") ? r->getParam("slot")->value().toInt() : -1;
            if (slot < 0 || slot >= 8) { r->send(400, "text/plain", "slot 0..7"); return; }
            bool ok = StorageRuntime::loadPreset((uint8_t)slot, v4::rt);
            if (!ok) { r->send(404, "text/plain", "slot empty"); return; }
            // Wenn Engine läuft, sanft restarten damit neue Werte sofort wirken
            if (v4::rt.running) { Synthesis::stop(); Synthesis::start(); }
            r->send(200, "text/plain", "OK"); return;
        }

        r->send(501, "text/plain", "unknown action");
    });

    // /presets — JSON-Array mit valid-Status pro Slot 0..7. Browser-UI kann
    // damit belegte vs. leere Slots visualisieren.
    server.on("/presets", HTTP_GET, [](AsyncWebServerRequest* r) {
        String j = "{\"slots\":[";
        for (uint8_t i = 0; i < 8; i++) {
            if (i > 0) j += ",";
            j += StorageRuntime::isPresetValid(i) ? "true" : "false";
        }
        j += "]}";
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", j);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    // Helper: serialisiert v4::rt als JSON. Quelle für /config und /set-Echo.
    static auto writeConfigJson = [](char* buf, size_t n) -> int {
        const auto& c = v4::rt;
        return snprintf(buf, n,
            "{\"run\":%s,\"type\":%d,\"speed\":%.3f,\"angle\":%.1f,"
            "\"rad\":%.1f,\"range\":%.1f,\"frame\":%.4f,\"cont\":%.2f,"
            "\"shape\":%.2f,\"edgec\":%.2f,\"mspace\":%.1f,\"dyn\":%d,"
            "\"fan\":%d,\"lamp\":%d,"
            "\"sa\":%.1f,\"so\":%.2f,\"ht\":%.1f,\"am\":%lu}",
            c.running ? "true":"false", c.moveType, c.speed, c.angle,
            c.radius, c.rangeDeg, c.framesize, c.contrast,
            c.zShape, c.edgeC, c.mspace, c.dynamics,
            c.fan, c.lamp,
            c.stepAngle, c.stepOffset, c.holdMs, (unsigned long)c.accelMax);
    };

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
        // STEP-Modus
        getF("sa",     c.stepAngle);   // step angle
        getF("so",     c.stepOffset);  // step offset
        getF("ht",     c.holdMs);      // hold time [ms]
        if (r->hasParam("am")) c.accelMax = (uint32_t)r->getParam("am")->value().toInt();
        // Bug-ID 23a: NVS-Persistence mit Debounce. touch() markiert dirty +
        // setzt Zeitstempel; tickFlush() im main-loop schreibt erst nach 5 s
        // Ruhe. Schutz vor NVS-Wear-Out beim Slider-Drag (ID 27).
        StorageRuntime::touch();
        // Echo: aktuelle State zurück, damit Slider/Anzeige sich synchronisieren.
        char buf[512]; writeConfigJson(buf, sizeof(buf));
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    // /preview — Engine-Zustand für die Browser-Canvas-Visualisierung.
    // Layout abhängig vom aktuellen moveType (siehe Synthesis::getPreviewBytes):
    //   Noise (0..2): {mode, w:32, h:32, data:[1024 Bytes]}
    //   Wave  (3..5): {mode, n:128, data:[128 Bytes]}    Bit7 = Phasen-Marker
    //   STEP  (6):    {mode, w:0}
    server.on("/preview", HTTP_GET, [](AsyncWebServerRequest* r) {
        static uint8_t pbuf[1024];
        size_t n = Synthesis::getPreviewBytes(pbuf, sizeof(pbuf));
        String json;
        json.reserve(n * 4 + 64);
        json = "{\"mode\":";
        json += v4::rt.moveType;
        if (n == 0) {
            json += ",\"w\":0,\"data\":[]}";
        } else {
            const bool noise = (v4::rt.moveType <= 2);
            json += noise ? ",\"w\":32,\"h\":32,\"data\":[" : ",\"n\":128,\"data\":[";
            for (size_t i = 0; i < n; i++) {
                if (i > 0) json += ',';
                json += pbuf[i];
            }
            json += "]}";
        }
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", json);
        res->addHeader("Access-Control-Allow-Origin", "*");
        res->addHeader("Cache-Control", "no-store");
        r->send(res);
    });

    // /bounds — Pro-Motor-Charakterisierungs-Werte aus NVS. Quelle für die in
    // Synthesis::start() / applyEngineCap() angewandten Caps. Slider in der UI
    // dürfen nicht über diese Werte hinaus angeboten werden.
    server.on("/bounds", HTTP_GET, [](AsyncWebServerRequest* r) {
        char buf[640];
        int n = snprintf(buf, sizeof(buf), "{\"motors\":[");
        for (uint8_t i = 0; i < 4; i++) {
            v4::CalibrationData cal;
            StorageCalib::load(i, cal);
            uint32_t maxSps = (cal.valid && cal.maxRpm > 0.0f)
                ? (uint32_t)Units::rpmToSps(i, cal.maxRpm) : 0;
            n += snprintf(buf + n, sizeof(buf) - n,
                "%s{\"valid\":%s,\"maxRpm\":%.0f,\"maxAccel\":%.0f,"
                "\"maxSps\":%lu,\"learnedCurrentMA\":%u,\"sgThrs\":%u}",
                i == 0 ? "" : ",",
                cal.valid ? "true" : "false",
                cal.maxRpm, cal.maxAccel,
                (unsigned long)maxSps,
                cal.learnedCurrentMA, cal.sgThrs);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "]}");
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* r) {
        char buf[512]; writeConfigJson(buf, sizeof(buf));
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    Telemetry::registerHandlers(server);

    // /sensor — Diagnose: liest alle Tacho-Pins direkt, optional Pull-Mode-Wechsel.
    // Aufruf: /sensor                  → reine Lesung
    //         /sensor?pin=15&mode=up   → pinMode(15, INPUT_PULLUP)
    //         /sensor?pin=15&mode=down → pinMode(15, INPUT_PULLDOWN)
    //         /sensor?pin=15&mode=none → pinMode(15, INPUT)
    server.on("/sensor", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (r->hasParam("pin") && r->hasParam("mode")) {
            int pin = r->getParam("pin")->value().toInt();
            String m = r->getParam("mode")->value();
            if (m == "up")        pinMode(pin, INPUT_PULLUP);
            else if (m == "down") pinMode(pin, INPUT_PULLDOWN);
            else if (m == "none") pinMode(pin, INPUT);
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"x_min_34\":%d,\"y_min_35\":%d,\"z_min_15\":%d,\"e_step_16\":%d,\"e_dir_17\":%d,\"now_ms\":%lu}",
            digitalRead(34), digitalRead(35), digitalRead(15),
            digitalRead(16), digitalRead(17),
            (unsigned long)millis());
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

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
                w.lastDeltaPerPulse, (unsigned long)(w.lastPeriodUs / 1000),
                MotorProfileNs::profiles[i].valid ? "true":"false",
                MotorProfileNs::profiles[i].count);
        }
        n += snprintf(buf+n, sizeof(buf)-n, "]}");
        AsyncWebServerResponse* res = r->beginResponse(200, "application/json", buf);
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });

    server.begin();
    ElegantOTA.begin(&server, "admin", "12345678");
    ElegantOTA.setAutoReboot(true);
    Logger::addLog("HTTP: server up on port 80 (+OTA)");
}

} // namespace WebServer
