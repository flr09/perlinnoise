#!/usr/bin/env python3
import re, threading, time, json, serial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT_PATH = "/dev/ttyACM0"
BAUD = 115200
HTTP_PORT = 8090
MOTOR_IDS = [1, 2, 3, 4]
HIST_MAX = 400  # Samples pro Motor (Ringpuffer) fuer den Speed-Graph

state_lock = threading.Lock()
motors = {
    mid: {"target": 0.0, "actual": 0.0, "speed": 0, "power": 0, "ts": time.time(), "hist": []}
    for mid in MOTOR_IDS
}
log_lines = []
LOG_MAX = 300
params = {"accel": 163, "speed": 210, "amp": 170, "duration": 3000, "stagger": 0}
# Rampe / Speed / Distance live-tunbar. Duration: angelegt, Funktion definiert,
# aber noch NICHT verdrahtet (Chris 2026-08-30: beeinflusst die anderen Werte,
# erst gemeinsam auskoppeln). Versatz: Pause zwischen Bewegungen je Motor, live aktiv.

STAT_RE = re.compile(
    r"STAT t=(\d+) motor=(\d+) target=([-\d.]+) actual=([-\d.]+) speed=(-?\d+) power=(-?\d+)"
)

ser_holder = {"ser": None}
ser_write_lock = threading.Lock()

def send_command(text):
    with ser_write_lock:
        s = ser_holder["ser"]
        if s is not None:
            s.write((text + "\n").encode())

def reader_thread():
    while True:
        try:
            ser = serial.Serial(PORT_PATH, BAUD, timeout=1)
            ser_holder["ser"] = ser
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").rstrip()
                if not line:
                    continue
                with state_lock:
                    log_lines.append(line)
                    if len(log_lines) > LOG_MAX:
                        del log_lines[0]
                m = STAT_RE.search(line)
                if m:
                    t_ms, mid, target, actual, speed, power = m.groups()
                    mid = int(mid)
                    if mid in motors:
                        with state_lock:
                            mo = motors[mid]
                            mo["target"] = float(target)
                            mo["actual"] = float(actual)
                            mo["speed"] = int(speed)
                            mo["power"] = int(power)
                            mo["ts"] = time.time()
                            mo["hist"].append(
                                [int(t_ms), int(speed), float(target), float(actual)]
                            )
                            if len(mo["hist"]) > HIST_MAX:
                                del mo["hist"][0]
        except Exception as e:
            ser_holder["ser"] = None
            with state_lock:
                log_lines.append(f"[dashboard] serial error: {e}, retry in 2s")
            time.sleep(2)

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>PerlinNoise v5-can Live</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px}
h1{font-size:1.1rem;color:#8cf}
.motor{background:#1c1c1c;border-radius:8px;padding:12px 14px;margin:10px 0}
.motor h2{margin:0 0 8px;font-size:.85rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}
.row{display:flex;gap:16px;font-size:.85rem;color:#ccc;margin-bottom:6px}
.row b{color:#fff}
canvas{width:100%;height:110px;display:block;background:#000;border-radius:6px}
.legend{font-size:.7rem;color:#888;margin-top:2px}
.legend span{margin-right:14px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px}
pre{background:#000;border-radius:8px;padding:10px;height:200px;overflow-y:auto;font-size:.7rem;line-height:1.3;margin-top:16px}
.controls{background:#1c1c1c;border-radius:8px;padding:12px 14px;display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}
.ctl label{display:flex;justify-content:space-between;font-size:.75rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}
.ctl input[type=range]{width:100%}
.ctl b{color:#fff}
</style></head>
<body>
<h1>PerlinNoise v5-can — Live (4 Motoren)</h1>
<div class="controls">
  <div class="ctl"><label>Rampe <b id="accelVal">--</b> dps/s</label>
    <input type="range" id="accel" min="20" max="600" step="1"
      oninput="document.getElementById('accelVal').textContent=this.value"
      onchange="setParam('accel', this.value)"></div>
  <div class="ctl"><label>Speed <b id="speedVal">--</b> dps</label>
    <input type="range" id="speed" min="20" max="400" step="1"
      oninput="document.getElementById('speedVal').textContent=this.value"
      onchange="setParam('speed', this.value)"></div>
  <div class="ctl"><label>Distance <b id="ampVal">--</b> °</label>
    <input type="range" id="amp" min="10" max="400" step="1"
      oninput="document.getElementById('ampVal').textContent=this.value"
      onchange="setParam('amp', this.value)"></div>
  <div class="ctl"><label>Duration (angelegt, noch inaktiv) <b id="durationVal">--</b> ms</label>
    <input type="range" id="duration" min="500" max="8000" step="100"
      oninput="document.getElementById('durationVal').textContent=this.value"
      onchange="setParam('duration', this.value)"></div>
  <div class="ctl"><label>Versatz (zw. Bewegungen je Motor) <b id="staggerVal">--</b> ms</label>
    <input type="range" id="stagger" min="0" max="2000" step="50"
      oninput="document.getElementById('staggerVal').textContent=this.value"
      onchange="setParam('stagger', this.value)"></div>
</div>
<div id="motors"></div>
<pre id="log"></pre>
<script>
const MOTOR_IDS = [1,2,3,4];
const containers = {};
for (const id of MOTOR_IDS) {
  const div = document.createElement('div');
  div.className = 'motor';
  div.innerHTML = `<h2>Motor ${id}</h2>
    <div class="row"><span>Ziel: <b id="target${id}">--</b>°</span>
    <span>Ist: <b id="actual${id}">--</b>°</span>
    <span>Speed: <b id="speed${id}">--</b> dps</span>
    <span>Power: <b id="power${id}">--</b></span>
    <span>Update: <b id="age${id}">--</b></span></div>
    <canvas id="chart${id}" width="800" height="110"></canvas>
    <div class="legend"><span><span class="dot" style="background:#8cf"></span>Speed (dps)</span>
    <span><span class="dot" style="background:#fa5"></span>Ziel (Grad)</span>
    <span><span class="dot" style="background:#5f5"></span>Ist (Grad)</span></div>`;
  document.getElementById('motors').appendChild(div);
  containers[id] = div;
}

const ANGLE_MAX = 400;  // feste Skala (Chris, 2026-08-30), Marker bei 180/360

function drawChart(canvas, hist) {
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (hist.length < 2) return;
  const t0 = hist[0][0], t1 = hist[hist.length-1][0];
  const tSpan = Math.max(1, t1 - t0);
  const speeds = hist.map(p => p[1]);
  const maxAbsSpeed = Math.max(50, ...speeds.map(Math.abs));

  // Mittellinie
  ctx.strokeStyle = '#333'; ctx.beginPath();
  ctx.moveTo(0, h/2); ctx.lineTo(w, h/2); ctx.stroke();

  // Marker bei 180 und 360 Grad (Skala fest auf ANGLE_MAX)
  ctx.strokeStyle = '#444'; ctx.setLineDash([3,3]); ctx.font = '9px system-ui';
  ctx.fillStyle = '#666';
  for (const deg of [180, 360]) {
    for (const sign of [1, -1]) {
      const y = h/2 - (sign*deg/ANGLE_MAX) * (h/2 - 4);
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
      ctx.fillText((sign*deg) + '°', 2, y - 2);
    }
  }
  ctx.setLineDash([]);

  function plot(idx, maxAbs, color) {
    ctx.strokeStyle = color; ctx.lineWidth = 1.5; ctx.beginPath();
    hist.forEach((p, i) => {
      const x = (p[0]-t0)/tSpan * w;
      const y = h/2 - (p[idx]/maxAbs) * (h/2 - 4);
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }
  plot(2, ANGLE_MAX, '#fa5');     // Ziel (Hintergrund), feste Grad-Skala
  plot(3, ANGLE_MAX, '#5f5');     // Ist, gleiche feste Skala wie Ziel
  plot(1, maxAbsSpeed, '#8cf');   // Speed im Vordergrund, eigene Skala (dps)
}

function setParam(key, val) {
  fetch('/set?' + key + '=' + encodeURIComponent(val)).catch(() => {});
}

let paramsSynced = false;
function syncParamSliders(p) {
  if (paramsSynced) return;  // nur einmal beim Laden, danach hat der User die Kontrolle
  for (const key of ['accel', 'speed', 'amp', 'duration', 'stagger']) {
    const el = document.getElementById(key);
    if (el && p[key] !== undefined) {
      el.value = p[key];
      document.getElementById(key + 'Val').textContent = p[key];
    }
  }
  paramsSynced = true;
}

async function tick() {
  try {
    const r = await fetch('/state.json');
    const s = await r.json();
    syncParamSliders(s.params || {});
    for (const id of MOTOR_IDS) {
      const mo = s.motors[id];
      if (!mo) continue;
      document.getElementById('target'+id).textContent = mo.target.toFixed(1);
      document.getElementById('actual'+id).textContent = mo.actual.toFixed(1);
      document.getElementById('speed'+id).textContent = mo.speed;
      document.getElementById('power'+id).textContent = mo.power;
      document.getElementById('age'+id).textContent = mo.age.toFixed(1) + 's her';
      drawChart(document.getElementById('chart'+id), mo.hist);
    }
    const logEl = document.getElementById('log');
    logEl.textContent = s.log.join('\\n');
    logEl.scrollTop = logEl.scrollHeight;
  } catch (e) {}
  setTimeout(tick, 300);
}
tick();
</script>
</body></html>"""

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/state.json"):
            with state_lock:
                now = time.time()
                out_motors = {}
                for mid, mo in motors.items():
                    out_motors[str(mid)] = {
                        "target": mo["target"], "actual": mo["actual"], "speed": mo["speed"],
                        "power": mo["power"], "age": now - mo["ts"], "hist": mo["hist"],
                    }
                payload = {"motors": out_motors, "log": log_lines[-80:], "params": params}
            body = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        elif self.path.startswith("/set"):
            from urllib.parse import urlparse, parse_qs
            qs = parse_qs(urlparse(self.path).query)
            ok = False
            for key in ("accel", "speed", "amp", "duration", "stagger"):
                if key in qs:
                    val = qs[key][0]
                    with state_lock:
                        params[key] = float(val) if key == "amp" else int(float(val))
                    if key == "duration":
                        # Noch nicht an die Firmware gesendet — siehe Kommentar oben bei params.
                        ok = True
                        continue
                    send_command(f"SET {key}={val}")
                    ok = True
            body = b"OK" if ok else b"ignored"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(body)
        else:
            body = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

if __name__ == "__main__":
    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()
    srv = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    print(f"Dashboard on :{HTTP_PORT}")
    srv.serve_forever()
