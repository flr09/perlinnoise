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
function setP(k,v){fetch('/set?'+k+'='+v).catch(function(){})}
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

/* SIMPLEX NOISE FIELD ANIMATION --------------------------------------- */
(function(){
  var c=document.getElementById('noiseCanvas');if(!c)return;
  var ctx=c.getContext('2d');
  var W=128,H=128;c.width=W;c.height=H;
  var p=new Uint8Array(256);for(var i=0;i<256;i++)p[i]=Math.floor(Math.random()*256);
  var perm=new Uint8Array(512),pm12=new Uint8Array(512);
  for(i=0;i<512;i++){perm[i]=p[i&255];pm12[i]=perm[i]%12}
  var F2=.5*(Math.sqrt(3)-1),G2=(3-Math.sqrt(3))/6;
  function grad(h,x,y){var hh=h%6,u=hh<4?x:y,v=hh<4?y:x;return((hh&1)===0?u:-u)+((hh&2)===0?v:-v)}
  function noise(x,y){
    var s=(x+y)*F2,i=Math.floor(x+s),j=Math.floor(y+s);
    var t=(i+j)*G2,X0=i-t,Y0=j-t,x0=x-X0,y0=y-Y0;
    var i1,j1;if(x0>y0){i1=1;j1=0}else{i1=0;j1=1}
    var x1=x0-i1+G2,y1=y0-j1+G2,x2=x0-1+2*G2,y2=y0-1+2*G2;
    var ii=i&255,jj=j&255,n0=0,n1=0,n2=0;
    var t0=.5-x0*x0-y0*y0;if(t0>=0){t0*=t0;n0=t0*t0*grad(pm12[ii+perm[jj]],x0,y0)}
    var t1a=.5-x1*x1-y1*y1;if(t1a>=0){t1a*=t1a;n1=t1a*t1a*grad(pm12[ii+i1+perm[jj+j1]],x1,y1)}
    var t2a=.5-x2*x2-y2*y2;if(t2a>=0){t2a*=t2a;n2=t2a*t2a*grad(pm12[ii+1+perm[jj+1]],x2,y2)}
    return 70*(n0+n1+n2);
  }
  var off=0;
  function draw(){
    var img=ctx.createImageData(W,H),d=img.data;
    var scale=0.04;
    for(var y=0;y<H;y++)for(var x=0;x<W;x++){
      var n=noise((x+off)*scale,y*scale);
      var g=Math.max(0,Math.min(255,Math.floor((n+1)/2*255)));
      var k=(y*W+x)*4;d[k]=d[k+1]=d[k+2]=g;d[k+3]=255;
    }
    ctx.putImageData(img,0,0);
    off+=.4;requestAnimationFrame(draw);
  }
  draw();
})();
buildDials();

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
