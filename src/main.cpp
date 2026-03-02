#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TMCStepper.h>
#include <AccelStepper.h>
#include "SimplexNoise.h"
#include <Preferences.h> // For saving/loading Wi-Fi credentials

// #change: Added version metadata and change log entries for traceable debugging.
// #author: Codex
// #time: 2026-02-05 18:47:48 CET
// #version: 0.2.4
static const char* FW_VERSION = "0.2.4";

// #aus
// Usability Suggestion:
// Externalizing HTML to LittleFS for easier updates is generally a good practice for larger projects.
// However, for initial setup, embedding it as PROGMEM is simpler and ensures it's always available.
// #aus
const char INSTALL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>E4 Wi-Fi Setup</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 20px; background-color: #333; color: #eee; }
    .container { background-color: #444; padding: 20px; border-radius: 8px; max-width: 400px; margin: 0 auto; }
    h2 { color: #ff9800; }
    input[type=text], input[type=password] {
      width: calc(100% - 22px); padding: 10px; margin: 8px 0; display: inline-block;
      border: 1px solid #555; border-radius: 4px; box-sizing: border-box; background-color: #555; color: #eee;
    }
    button {
      background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0;
      border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px;
    }
    button:hover { background-color: #45a049; }
    .status { margin-top: 15px; font-weight: bold; }
    .error { color: #f44336; }
    .success { color: #4CAF50; }
  </style>
</head>
<body>
  <div class="container">
    <h2>E4 Wi-Fi Setup</h2>
    <p>Connect your E4 board to your local Wi-Fi network.</p>
    <form action="/wifisave" method="post">
      <label for="ssid">SSID:</label>
      <input type="text" id="ssid" name="ssid" required><br>
      <label for="pass">Password:</label>
      <input type="password" id="pass" name="password"><br>
      <button type="submit">Save & Connect</button>
    </form>
    <div id="status" class="status"></div>
  </div>
  <script>
    document.querySelector('form').addEventListener('submit', function(event) {
      event.preventDefault();
      const form = event.target;
      const formData = new FormData(form);
      const urlParams = new URLSearchParams(formData).toString();
      
      fetch(form.action, {
        method: form.method,
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: urlParams
      })
      .then(response => response.text())
      .then(data => {
        const statusDiv = document.getElementById('status');
        if (data === "OK") {
          statusDiv.className = "status success";
          statusDiv.innerText = "Wi-Fi settings saved. Board will attempt to connect. Please reboot board manually if connection is not established.";
        } else {
          statusDiv.className = "status error";
          statusDiv.innerText = "Error saving settings: " + data;
        }
      })
      .catch(error => {
        const statusDiv = document.getElementById('status');
        statusDiv.className = "status error";
        statusDiv.innerText = "Network error: " + error;
      });
    });
  </script>
</body>
</html>
)rawliteral";


// --- HARDWARE ---
#define R_SENSE 0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX 21
#define UART_TX 22

// #beschreibung: Zusätzliche Pins für Temperatursensor, Lüfter und Lampe definiert.
// #author: Gemini CLI Agent
// #time: 2026-02-05 15:20:00 (Approximate)
// #version: 0.4.2
#define TEMP_SENSOR_PIN 36 // Analog-fähiger Pin für den Temperatursensor (TE0)
// #change: Machine output mapping clarified as requested:
// Lamp -> HOTEND connector, Fan -> BED connector.
// #author: Codex
// #time: 2026-02-10 22:30:00 CET
// #version: 0.4.8
#define HOTEND_OUTPUT_PIN 2 // E0/Hotend MOSFET output
#define BED_OUTPUT_PIN 13   // Bed MOSFET output
#define LAMP_PIN HOTEND_OUTPUT_PIN
#define FAN_PIN BED_OUTPUT_PIN

// Motor Pins
#define X_STEP 27
#define X_DIR  26
#define Y_STEP 33
#define Y_DIR  32
#define Z_STEP 14
#define Z_DIR  12
#define E_STEP 16
#define E_DIR  17

// --- TMC ADRESSEN (E4 Layout: Z=0, X=1, E=2, Y=3) ---
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

SimplexNoise sn;
AsyncWebServer server(80);
DNSServer dnsServer;
bool apMode = false;

// --- CONFIG ---

Preferences preferences; // Global Preferences object for NVS

struct MotorOffset {
  float x;
  float y;
};

struct Config {
  bool running = true;
  int moveType = 0;        // 0=Linear, 1=Circle, 2=Figure8
  float speed = 0.12;      // Fluggeschwindigkeit

  // Pfad Parameter
  float angle = 45.0;      // Richtung (Linear)
  float radius = 50.0;     // Radius (Kreis/Acht)

  // DIE 3 NEUEN NOISE REGLER
  float framesize = 0.01;  // "Ausschnitt" (Frequenz/Zoom)
  float contrast = 1.94;   // "Hügelgröße" (Amplitude)
  float zShape = 1.0;      // "Form" (Gamma/Sharpness). 1.0=Normal, >1=Spitz, <0=Invertiert

  // Max Hub der Motoren in Grad (UI-regelbar)
  // #change: Range and spacing are now user-adjustable at runtime.
  // #author: Codex
  // #time: 2026-02-05 18:47:48 CET
  // #version: 0.2.4
  float rangeDeg = 300.0;
  float motorSpacingCm = 25.0; // This will now be a "preset" for the individual offsets

  // #beschreibung: Individuelle X/Y-Offsets pro Motor zur feineren Steuerung der Noise-Abtastung hinzugefügt.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 16:20:00 (Approximate)
  // #version: 0.4.6
  MotorOffset motorOffsets[4] = {
    { -37.5, 0.0 }, { -12.5, 0.0 }, { 12.5, 0.0 }, { 37.5, 0.0 }
  };

  // #aus
  // Usability Suggestion:
  // Store Wi-Fi credentials in the Config struct for easier management and persistence.
  // Using String for these is acceptable for small projects, but char arrays might be more memory efficient
  // for very constrained environments or if security is a higher concern.
  // #aus
  String wifi_ssid = "";
  String wifi_password = "";

  // #beschreibung: Konfigurationsvariablen für Lüftergeschwindigkeit und Lampenhelligkeit hinzugefügt.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 15:30:00 (Approximate)
  // #version: 0.4.2
  int fanSpeed = 0;
  int lampBrightness = 0;
};

Config webCfg;
portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;

// Function to load configuration from NVS
void loadConfig() {
  preferences.begin("e4-config", false); // Open Preferences with namespace "e4-config"
  webCfg.wifi_ssid = preferences.getString("ssid", "");
  webCfg.wifi_password = preferences.getString("pass", "");
  preferences.end();
  Serial.println("Config loaded.");
  Serial.print("SSID: ");
  Serial.println(webCfg.wifi_ssid);
}

// Function to save configuration to NVS
void saveConfig() {
  preferences.begin("e4-config", false);
  preferences.putString("ssid", webCfg.wifi_ssid);
  preferences.putString("pass", webCfg.wifi_password);
  preferences.end();
  Serial.println("Config saved.");
}

// Globale Flugbahn-Variablen
double flightX = 0;
double flightY = 0;
double timeAccumulator = 0;

// --- HTML ---
// #aus
// Usability Suggestion:
// Consider externalizing the HTML into a separate .html file and serving it using LittleFS (SPIFFS).
// This makes the HTML easier to edit, maintain, and allows for more complex web interfaces without recompiling the C++ code.
// #aus
// #change: Added UI controls and /config-driven state sync for runtime tuning.
// #author: Codex
// #time: 2026-02-05 18:47:48 CET
// #version: 0.2.4
const char index_html[] PROGMEM = R"rawliteral(
<!-- #change: Added edit UI for runtime config viewing and control. -->
<!-- #author: Codex -->
<!-- #time: 2026-02-05 20:14:14 CET -->
<!-- #version: 0.3.3 -->
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>E4 Config Editor</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 20px; background-color: #333; color: #eee; }
    .container { background-color: #444; padding: 20px; border-radius: 8px; max-width: 1000px; margin: 0 auto; }
    .two-col { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; align-items: start; }
    .col-right { text-align: left; }
    h2 { color: #ff9800; }
    label { display: block; text-align: left; font-size: 0.8em; color: #bbb; margin-top: 12px; text-transform: uppercase; }
    input[type=range] { width: 100%; margin: 6px 0; }
    .val { float: right; color: #4CAF50; font-weight: bold; }
    button { background-color: #4CAF50; color: white; padding: 12px 18px; margin: 10px 0; border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
    .status { margin-top: 10px; font-weight: bold; }
    .error { color: #f44336; }
    .ok { color: #4CAF50; }
    canvas { border: 1px solid #555; background-color: #333; display: block; margin: 12px auto; width: 100%; height: auto; }
    .note { font-size: 0.85em; color: #aaa; max-width: 520px; margin: 0 auto 8px; }
    .angles { text-align: left; font-size: 0.85em; color: #bbb; margin-top: 8px; }
    .angle-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; margin-top: 8px; }
    .angle-card { background: #3a3a3a; border: 1px solid #555; border-radius: 6px; padding: 6px; }
    .angle-card .label { font-size: 0.75em; color: #bbb; }
    .angle-card .value { font-size: 0.95em; color: #fff; }
    .gauge { width: 48px; height: 48px; border: 1px solid #666; border-radius: 50%; margin: 6px auto 0; position: relative; }
    .needle { position: absolute; left: 50%; top: 50%; width: 2px; height: 18px; background: #ff9800; transform-origin: 50% 100%; }
    .storage { display: grid; grid-template-columns: repeat(10, 1fr); gap: 6px; margin-top: 10px; }
    .slot { background: #3a3a3a; border: 1px solid #555; color: #eee; padding: 8px 0; border-radius: 6px; cursor: pointer; font-size: 0.85em; }
    .slot.active { outline: 2px solid #4CAF50; }
    .savebtn { background: #5a0000; color: #fff; padding: 8px 0; border: 1px solid #7a0000; border-radius: 6px; cursor: pointer; font-weight: bold; }
    .savebtn.armed { background: #8a0000; }
    .savebtn.blink { animation: blinkRed 0.2s steps(1, end) 1; }
    .savebtn.pulse { animation: pulseRed 0.5s ease-in-out 2; }
    .selectbtn { background: #555; color: #eee; padding: 8px 0; border: 1px solid #666; border-radius: 6px; cursor: pointer; }
    .slider-row { display: grid; grid-template-columns: 28px 1fr; gap: 8px; align-items: center; }
    .pick { width: 26px; height: 26px; border-radius: 6px; border: 1px solid #666; background: #333; color: #eee; cursor: pointer; }
    .pick.on { background: #4CAF50; color: #111; border-color: #4CAF50; }
    .stoprow { margin-top: 10px; }
    @keyframes blinkRed { 0%, 100% { filter: brightness(1.0); } 50% { filter: brightness(1.8); } }
    @keyframes pulseRed { 0% { filter: brightness(1.0); } 50% { filter: brightness(1.6); } 100% { filter: brightness(1.0); } }
    @media (max-width: 720px) {
      body { margin: 10px; }
      .container { padding: 12px; }
      .two-col { grid-template-columns: 1fr; }
      .angle-grid { grid-template-columns: repeat(2, 1fr); }
      .storage { grid-template-columns: repeat(5, 1fr); }
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>E4 Config Editor</h2>
    <div class="two-col">
      <div class="col-left">
        <canvas id="noiseCanvas" width="640" height="640"></canvas>
        <div id="angles" class="angles"></div>
        <div class="angle-grid" id="angleGrid">
          <div class="angle-card"><div class="label">M1</div><div class="value" id="angleVal1">0.0°</div><div class="gauge"><div class="needle" id="needle1"></div></div></div>
          <div class="angle-card"><div class="label">M2</div><div class="value" id="angleVal2">0.0°</div><div class="gauge"><div class="needle" id="needle2"></div></div></div>
          <div class="angle-card"><div class="label">M3</div><div class="value" id="angleVal3">0.0°</div><div class="gauge"><div class="needle" id="needle3"></div></div></div>
          <div class="angle-card"><div class="label">M4</div><div class="value" id="angleVal4">0.0°</div><div class="gauge"><div class="needle" id="needle4"></div></div></div>
        </div>
        <div class="stoprow">
          <button id="btn" onclick="toggle()">START SYSTEM</button>
        </div>
        <div class="storage" id="storageRow">
          <button id="selectAll" class="selectbtn" onclick="toggleAll()">ALL</button>
          <button class="slot" onclick="slotClick(1)">1</button>
          <button class="slot" onclick="slotClick(2)">2</button>
          <button class="slot" onclick="slotClick(3)">3</button>
          <button class="slot" onclick="slotClick(4)">4</button>
          <button class="slot" onclick="slotClick(5)">5</button>
          <button class="slot" onclick="slotClick(6)">6</button>
          <button class="slot" onclick="slotClick(7)">7</button>
          <button class="slot" onclick="slotClick(8)">8</button>
          <button id="saveMode" class="savebtn" onclick="toggleSaveMode()">SPEICHERN</button>
        </div>
      </div>
      <div class="col-right">
        <label>Movement Pattern</label>
        <select id="mType" onchange="u('type', this.value); showControls();" style="width:100%; padding:10px; margin-top:6px;">
          <option value="0">LINEAR (Wind)</option>
          <option value="1">CIRCLE (Loop)</option>
          <option value="2">FIGURE 8 (Organic)</option>
          <option value="3">SINUS</option>
          <option value="4">SAW (Sägezahn)</option>
          <option value="5">RECT (Rechteck)</option>
        </select>

        <div class="slider-row">
          <button id="pick-speed" class="pick" onclick="togglePick('speed')">S</button>
          <div>
            <label>Flight Speed <span id="sV" class="val"></span></label>
            <input id="speed" type="range" min="0" max="200" value="12" oninput="u('speed', this.value/100)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-angle" class="pick" onclick="togglePick('angle')">A</button>
          <div>
            <label>Direction (Angle) <span id="aV" class="val"></span></label>
            <input id="angle" type="range" min="0" max="360" value="45" oninput="u('angle', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-rad" class="pick" onclick="togglePick('rad')">R</button>
          <div>
            <label>Path Radius <span id="rV" class="val"></span></label>
            <input id="rad" type="range" min="1" max="500" value="50" oninput="u('rad', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-frame" class="pick" onclick="togglePick('frame')">F</button>
          <div>
            <label>Framesize <span id="fsV" class="val"></span></label>
            <input id="frame" type="range" min="1" max="500" value="10" oninput="u('frame', this.value/1000)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-cont" class="pick" onclick="togglePick('cont')">C</button>
          <div>
            <label>Contrast <span id="cV" class="val"></span></label>
            <input id="cont" type="range" min="0" max="200" value="194" oninput="u('cont', this.value/100)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-edgec" class="pick" onclick="togglePick('edgec')">E</button>
          <div>
            <label>Edge Contrast <span id="ecV" class="val"></span></label>
            <input id="edgec" type="range" min="0" max="200" value="29" oninput="u('edgec', this.value/100)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-shape" class="pick" onclick="togglePick('shape')">Z</button>
          <div>
            <label>Form (Z-Shape) <span id="zV" class="val"></span></label>
            <input id="shape" type="range" min="-50" max="50" value="10" oninput="u('shape', this.value/10)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-range" class="pick" onclick="togglePick('range')">G</button>
          <div>
            <label>Max Range (deg) <span id="rdV" class="val"></span></label>
            <input id="range" type="range" min="30" max="360" value="300" oninput="u('range', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-mspace" class="pick" onclick="togglePick('mspace')">M</button>
          <div>
            <label><span id="msLabel">Motor Spacing (cm)</span> <span id="msV" class="val"></span></label>
            <input id="mspace" type="range" min="5" max="100" value="25" oninput="u('mspace', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-mapzoom" class="pick" onclick="togglePick('mapzoom')">Z</button>
          <div>
            <label>Map Zoom <span id="mzV" class="val"></span></label>
            <input id="mapzoom" type="range" min="20" max="300" value="100" oninput="u('mapzoom', this.value/100)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-fan" class="pick" onclick="togglePick('fan')">F</button>
          <div>
            <label>PWM Fan <span id="fanV" class="val"></span></label>
            <input id="fan" type="range" min="0" max="255" value="0" oninput="u('fan', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-lamp" class="pick" onclick="togglePick('lamp')">L</button>
          <div>
            <label>Lamp <span id="lampV" class="val"></span></label>
            <input id="lamp" type="range" min="0" max="255" value="0" oninput="u('lamp', this.value)">
          </div>
        </div>

        <label>Offline Preview: <span id="offlineVal">ON</span></label>
        <input id="offlineToggle" type="range" min="0" max="1" step="1" value="1" oninput="toggleOffline()">
      </div>
    </div>

    <div id="status" class="status"></div>
    <div style="margin-top:10px; font-size:0.75em; color:#888;">Editor v0.3.3</div>
  </div>

  <script>
  let r = false;
  const canvas = document.getElementById('noiseCanvas');
  const ctx = canvas.getContext('2d');
  const offlineToggle = document.getElementById('offlineToggle');
  const offlineVal = document.getElementById('offlineVal');
  const saveBtn = document.getElementById('saveMode');
  const selectAllBtn = document.getElementById('selectAll');
  const pickButtons = {
    speed: document.getElementById('pick-speed'),
    angle: document.getElementById('pick-angle'),
    rad: document.getElementById('pick-rad'),
    range: document.getElementById('pick-range'),
    mspace: document.getElementById('pick-mspace'),
    frame: document.getElementById('pick-frame'),
    mapzoom: document.getElementById('pick-mapzoom'),
    cont: document.getElementById('pick-cont'),
    shape: document.getElementById('pick-shape'),
    edgec: document.getElementById('pick-edgec'),
    fan: document.getElementById('pick-fan'),
    lamp: document.getElementById('pick-lamp')
  };
  let currentCfg = null;
  let animationFrameId = null;
  let lastTimestamp = 0;
  let flightX = 0;
  let flightY = 0;
  let timeAccumulator = 0;
  const pathHistory = [];
  const MAX_PATH_HISTORY = 160;
  let saveArmed = false;
  const selected = new Set(['speed','angle','rad','range','mspace','frame','mapzoom','cont','shape','edgec','fan','lamp']);

  const offlineCfg = {
    running: 1,
    moveType: 0,
    speed: 0.12,
    angle: 45,
    radius: 50,
    framesize: 0.01,
    contrast: 1.94,
    zShape: 1.0,
    rangeDeg: 300,
    motorSpacingCm: 25,
    fanSpeed: 0,
    lampBrightness: 0
  };

  // --- Simplex Noise (same style as perlin_visualizer.html) ---
  function SimplexNoise2D() {
    const F2 = 0.5 * (Math.sqrt(3.0) - 1.0);
    const G2 = (3.0 - Math.sqrt(3.0)) / 6.0;
    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = Math.floor(Math.random() * 256);
    const perm = new Uint8Array(512);
    const permMod12 = new Uint8Array(512);
    for (let i = 0; i < 512; i++) {
      perm[i] = p[i & 255];
      permMod12[i] = perm[i] % 12;
    }
    function grad(hash, x, y) {
      const h = hash % 6;
      const u = h < 4 ? x : y;
      const v = h < 4 ? y : x;
      return ((h & 1) === 0 ? u : -u) + ((h & 2) === 0 ? v : -v);
    }
    return function(x, y) {
      let s = (x + y) * F2;
      let i = Math.floor(x + s);
      let j = Math.floor(y + s);
      let t = (i + j) * G2;
      let X0 = i - t;
      let Y0 = j - t;
      let x0 = x - X0;
      let y0 = y - Y0;
      let i1, j1;
      if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
      let x1 = x0 - i1 + G2;
      let y1 = y0 - j1 + G2;
      let x2 = x0 - 1.0 + 2.0 * G2;
      let y2 = y0 - 1.0 + 2.0 * G2;
      const ii = i & 255;
      const jj = j & 255;
      let t0 = 0.5 - x0 * x0 - y0 * y0;
      let n0 = 0.0;
      if (t0 >= 0) {
        t0 *= t0;
        n0 = t0 * t0 * grad(permMod12[ii + perm[jj]], x0, y0);
      }
      let t1 = 0.5 - x1 * x1 - y1 * y1;
      let n1 = 0.0;
      if (t1 >= 0) {
        t1 *= t1;
        n1 = t1 * t1 * grad(permMod12[ii + i1 + perm[jj + j1]], x1, y1);
      }
      let t2 = 0.5 - x2 * x2 - y2 * y2;
      let n2 = 0.0;
      if (t2 >= 0) {
        t2 *= t2;
        n2 = t2 * t2 * grad(permMod12[ii + 1 + perm[jj + 1]], x2, y2);
      }
      return 70.0 * (n0 + n1 + n2);
    };
  }
  const noise2D = SimplexNoise2D();

  function convertToGrayscale(noiseVal, contrast, zShape, edgeC) {
    let nNorm = (noiseVal + 1.0) / 2.0;
    let exponent = Math.abs(zShape);
    if (exponent < 0.1) exponent = 0.1;
    let nShaped = Math.pow(nNorm, exponent);
    if (zShape < 0) nShaped = 1.0 - nShaped;
    let finalNoise = (nShaped * 2.0) - 1.0;
    // Edge contrast: emphasize zero-crossings (ridged look)
    const edge = 1.0 - Math.abs(finalNoise);
    const edgeMix = Math.max(0.0, Math.min(2.0, edgeC));
    const edgeBoost = (edge * 2.0 - 1.0) * edgeMix;
    let scaledNoise = (finalNoise + edgeBoost) * contrast;
    let gray = Math.floor(((scaledNoise + 1.0) / 2.0) * 255);
    return Math.max(0, Math.min(255, gray));
  }

  function resizeCanvas() {
    const cssWidth = Math.min(canvas.parentElement.clientWidth, 640);
    const cssHeight = cssWidth;
    const dpr = window.devicePixelRatio || 1;
    const targetW = Math.max(1, Math.floor(cssWidth * dpr));
    const targetH = Math.max(1, Math.floor(cssHeight * dpr));
    if (canvas.width !== targetW || canvas.height !== targetH) {
      canvas.width = targetW;
      canvas.height = targetH;
    }
  }

  function drawNoiseField() {
    if (!currentCfg) return;
    resizeCanvas();
    const imageData = ctx.createImageData(canvas.width, canvas.height);
    const data = imageData.data;
    const framesize = currentCfg.framesize;
    const contrast = currentCfg.contrast;
    const zShape = currentCfg.zShape;
    const mapOffsetX = flightX;
    const mapOffsetY = flightY;
    const mapZoom = parseFloat(document.getElementById('mapzoom').value) / 100.0;
    const edgeC = parseFloat(document.getElementById('edgec').value) / 100.0;
    const motorSpanPx = canvas.width * 0.6;
    const worldScale = (motorSpanPx / (3.0 * Math.max(1, currentCfg.motorSpacingCm))) * mapZoom;
    for (let y = 0; y < canvas.height; y++) {
      for (let x = 0; x < canvas.width; x++) {
        const wx = (x - canvas.width / 2) / worldScale;
        const wy = (y - canvas.height / 2) / worldScale;
        const noiseX = (wx + mapOffsetX) * framesize;
        const noiseY = (wy + mapOffsetY) * framesize;
        const noiseVal = noise2D(noiseX, noiseY);
        const gray = convertToGrayscale(noiseVal, contrast, zShape, edgeC);
        const index = (y * canvas.width + x) * 4;
        data[index] = gray;
        data[index + 1] = gray;
        data[index + 2] = gray;
        data[index + 3] = 255;
      }
    }
    ctx.putImageData(imageData, 0, 0);
  }

  function updateSimulation(dt) {
    if (!currentCfg || !currentCfg.running) return;
    const cfg = currentCfg;
    if (cfg.moveType >= 3) {
      timeAccumulator += cfg.speed * dt;
      return;
    }
    const speed = cfg.speed;
    const angle = cfg.angle;
    const radius = cfg.radius;
    const moveType = cfg.moveType;
    if (moveType === 0) {
      const rad = angle * Math.PI / 180.0;
      flightX += Math.cos(rad) * speed * dt * 10.0;
      flightY += Math.sin(rad) * speed * dt * 10.0;
      const wrapLimit = 1000000.0;
      flightX = flightX % wrapLimit;
      flightY = flightY % wrapLimit;
    } else {
      timeAccumulator += speed * dt;
      if (moveType === 1) {
        flightX = radius * Math.cos(timeAccumulator);
        flightY = radius * Math.sin(timeAccumulator);
      } else if (moveType === 2) {
        flightX = radius * Math.cos(timeAccumulator);
        flightY = (radius * 0.5) * Math.sin(timeAccumulator * 2.0);
      }
    }
    pathHistory.push({ x: flightX, y: flightY });
    if (pathHistory.length > MAX_PATH_HISTORY) pathHistory.shift();
  }

  function simulateFuturePath(steps, dt) {
    if (!currentCfg) return [];
    let fx = flightX;
    let fy = flightY;
    let t = timeAccumulator;
    const pts = [];
    const cfg = currentCfg;
    for (let i = 0; i < steps; i++) {
      if (cfg.moveType === 0) {
        const rad = cfg.angle * Math.PI / 180.0;
        fx += Math.cos(rad) * cfg.speed * dt * 10.0;
        fy += Math.sin(rad) * cfg.speed * dt * 10.0;
        const wrapLimit = 1000000.0;
        fx = fx % wrapLimit;
        fy = fy % wrapLimit;
      } else {
        t += cfg.speed * dt;
        if (cfg.moveType === 1) {
          fx = cfg.radius * Math.cos(t);
          fy = cfg.radius * Math.sin(t);
        } else if (cfg.moveType === 2) {
          fx = cfg.radius * Math.cos(t);
          fy = (cfg.radius * 0.5) * Math.sin(t * 2.0);
        }
      }
      pts.push({ x: fx, y: fy });
    }
    return pts;
  }

  function motorAnglesDeg() {
    if (!currentCfg) return [0, 0, 0, 0];
    const range = currentCfg.rangeDeg;
    const contrast = currentCfg.contrast;
    const angles = [];

    if (currentCfg.moveType >= 3) {
      // Waveform Modi — Phasenversatz aus mspace: 25 = 90°
      const phaseSpread = (currentCfg.motorSpacingCm / 100.0) * 2 * Math.PI;
      for (let i = 0; i < 4; i++) {
        const phase = timeAccumulator + i * phaseSpread;
        let val = 0;
        if (currentCfg.moveType === 3) {
          val = Math.sin(phase);
        } else if (currentCfg.moveType === 4) {
          val = 2.0 * (((phase / (2 * Math.PI)) % 1.0 + 1.0) % 1.0) - 1.0;
        } else if (currentCfg.moveType === 5) {
          val = Math.sin(phase) >= 0 ? 1.0 : -1.0;
        }
        angles.push(Math.max(-range, Math.min(range, val * contrast * range)));
      }
    } else {
      // Noise Modi
      const framesize = currentCfg.framesize;
      const zShape = currentCfg.zShape;
      for (let i = 0; i < 4; i++) {
        const offsetX = (i - 1.5) * currentCfg.motorSpacingCm;
        const noiseX = (offsetX + flightX) * framesize;
        const noiseY = (0 + flightY) * framesize;
        const n = noise2D(noiseX, noiseY);
        const nNorm = (n + 1.0) / 2.0;
        let exponent = Math.abs(zShape);
        if (exponent < 0.1) exponent = 0.1;
        let nShaped = Math.pow(nNorm, exponent);
        if (zShape < 0) nShaped = 1.0 - nShaped;
        const finalNoise = (nShaped * 2.0) - 1.0;
        const val = finalNoise * contrast;
        angles.push(Math.max(-range, Math.min(range, val * range)));
      }
    }
    return angles;
  }

  function updateAngleCards() {
    const a = motorAnglesDeg();
    const el = document.getElementById('angles');
    el.textContent = `Angles: M1 ${a[0].toFixed(1)}°, M2 ${a[1].toFixed(1)}°, M3 ${a[2].toFixed(1)}°, M4 ${a[3].toFixed(1)}°`;
    document.getElementById('angleVal1').textContent = `${a[0].toFixed(1)}°`;
    document.getElementById('angleVal2').textContent = `${a[1].toFixed(1)}°`;
    document.getElementById('angleVal3').textContent = `${a[2].toFixed(1)}°`;
    document.getElementById('angleVal4').textContent = `${a[3].toFixed(1)}°`;
    document.getElementById('needle1').style.transform = `translate(-50%, -100%) rotate(${a[0]}deg)`;
    document.getElementById('needle2').style.transform = `translate(-50%, -100%) rotate(${a[1]}deg)`;
    document.getElementById('needle3').style.transform = `translate(-50%, -100%) rotate(${a[2]}deg)`;
    document.getElementById('needle4').style.transform = `translate(-50%, -100%) rotate(${a[3]}deg)`;
  }

  function drawPathAndMotors() {
    if (!currentCfg) return;
    resizeCanvas();
    const framesize = currentCfg.framesize;
    const cx = canvas.width / 2;
    const cy = canvas.height / 2;
    const mapZoom = parseFloat(document.getElementById('mapzoom').value) / 100.0;
    const motorSpanPx = canvas.width * 0.6;
    const worldScale = (motorSpanPx / (3.0 * Math.max(1, currentCfg.motorSpacingCm))) * mapZoom;
    // Past path (red dashed, thick)
    ctx.beginPath();
    ctx.lineWidth = 8;
    ctx.setLineDash([10, 10]);
    pathHistory.forEach((p, i) => {
      const alpha = i / pathHistory.length;
      ctx.strokeStyle = `rgba(255, 0, 0, ${alpha * 0.6})`;
      const px = cx - p.x * worldScale;
      const py = cy - p.y * worldScale;
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    });
    ctx.stroke();
    ctx.setLineDash([]);

    // Future path (green solid, thick)
    const future = simulateFuturePath(120, 0.016);
    ctx.beginPath();
    ctx.lineWidth = 8;
    ctx.strokeStyle = 'rgba(0, 255, 0, 0.7)';
    future.forEach((p, i) => {
      const px = cx - p.x * worldScale;
      const py = cy - p.y * worldScale;
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    });
    ctx.stroke();

    // Flight point (map offset)
    ctx.beginPath();
    ctx.arc(cx - flightX * worldScale, cy - flightY * worldScale, 3, 0, Math.PI * 2);
    ctx.fillStyle = 'red';
    ctx.fill();

    const ang = motorAnglesDeg();
    for (let i = 0; i < 4; i++) {
      const offsetX = (i - 1.5) * currentCfg.motorSpacingCm;
      const motorX = cx + offsetX * worldScale;
      const motorY = cy;
      ctx.beginPath();
      ctx.arc(motorX, motorY, 5, 0, Math.PI * 2);
      ctx.fillStyle = `hsl(${i * 90}, 100%, 50%)`;
      ctx.fill();
      ctx.strokeStyle = 'white';
      ctx.lineWidth = 1;
      ctx.stroke();
      ctx.fillStyle = 'white';
      ctx.font = '8px Arial';
      ctx.fillText(`M${i + 1}`, motorX - 6, motorY + 2);
      ctx.fillStyle = 'white';
      ctx.font = '9px Arial';
      ctx.fillText(`${ang[i].toFixed(1)}°`, motorX - 10, motorY - 8);
    }

    updateAngleCards();
  }

  function drawWaveform() {
    resizeCanvas();
    const W = canvas.width;
    const H = canvas.height;
    const cy = H / 2;
    const amp = H * 0.36;
    const contrast = currentCfg ? currentCfg.contrast : 1.0;
    const mType = currentCfg ? currentCfg.moveType : 3;
    const cycles = 3;

    // Hintergrund
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, W, H);

    // Mittelachse
    ctx.strokeStyle = '#444';
    ctx.lineWidth = 1;
    ctx.setLineDash([6, 6]);
    ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(W, cy); ctx.stroke();
    ctx.setLineDash([]);

    // Amplitudengrenzen
    ctx.strokeStyle = '#333';
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, cy - amp); ctx.lineTo(W, cy - amp); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, cy + amp); ctx.lineTo(W, cy + amp); ctx.stroke();

    // Wellenform zeichnen
    ctx.strokeStyle = '#4CAF50';
    ctx.lineWidth = 2;
    // Säge und Rechteck brauchen saubere Sprünge (moveTo statt lineTo an Unstetigkeitsstellen)
    let prevCycle = -1;
    let prevSquareSign = null;
    ctx.beginPath();
    for (let px = 0; px < W; px++) {
      const phase = (px / W) * cycles * 2 * Math.PI;
      let val = 0;
      let jump = false;
      if (mType === 3) {
        val = Math.sin(phase);
      } else if (mType === 4) {
        const cycleNum = Math.floor(phase / (2 * Math.PI));
        if (cycleNum !== prevCycle && px > 0) jump = true;
        prevCycle = cycleNum;
        val = 2.0 * ((phase / (2 * Math.PI)) % 1.0) - 1.0;
      } else if (mType === 5) {
        const s = Math.sin(phase) >= 0 ? 1 : -1;
        if (s !== prevSquareSign && prevSquareSign !== null) jump = true;
        prevSquareSign = s;
        val = s;
      }
      const y = cy - val * amp * Math.min(contrast, 1.5);
      if (px === 0 || jump) ctx.moveTo(px, y); else ctx.lineTo(px, y);
    }
    ctx.stroke();

    // Motor-Punkte auf der Welle
    const colors = ['#ff4444', '#44aaff', '#ffaa00', '#aa44ff'];
    const phaseSpread = currentCfg ? (currentCfg.motorSpacingCm / 100.0) * 2 * Math.PI : Math.PI / 2.0;
    const totalRange = cycles * 2 * Math.PI;
    for (let i = 0; i < 4; i++) {
      const phase = ((timeAccumulator + i * phaseSpread) % totalRange + totalRange) % totalRange;
      const px = (phase / totalRange) * W;
      let val = 0;
      if (mType === 3) val = Math.sin(phase);
      else if (mType === 4) val = 2.0 * ((phase / (2 * Math.PI)) % 1.0) - 1.0;
      else if (mType === 5) val = Math.sin(phase) >= 0 ? 1.0 : -1.0;
      const py = cy - val * amp * Math.min(contrast, 1.5);
      ctx.beginPath();
      ctx.arc(px, py, 7, 0, Math.PI * 2);
      ctx.fillStyle = colors[i];
      ctx.fill();
      ctx.strokeStyle = 'white'; ctx.lineWidth = 1; ctx.stroke();
      ctx.fillStyle = 'white'; ctx.font = '10px Arial';
      ctx.fillText(`M${i + 1}`, px - 6, py - 12);
    }
  }

  function render() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (currentCfg && currentCfg.moveType >= 3) {
      drawWaveform();
      updateAngleCards();
    } else {
      drawNoiseField();
      drawPathAndMotors();
    }
  }

  function animate(timestamp) {
    if (!lastTimestamp) lastTimestamp = timestamp;
    const deltaTime = (timestamp - lastTimestamp) / 1000.0;
    lastTimestamp = timestamp;
    updateSimulation(deltaTime);
    render();
    animationFrameId = requestAnimationFrame(animate);
  }

  function restartAnimation() {
    if (animationFrameId) cancelAnimationFrame(animationFrameId);
    animationFrameId = null;
    lastTimestamp = 0;
    flightX = 0;
    flightY = 0;
    timeAccumulator = 0;
    pathHistory.length = 0;
    animate(0);
  }
  function toggle() {
    r = !r;
    const b = document.getElementById('btn');
    b.innerText = r ? "STOP SYSTEM" : "START SYSTEM";
    b.style.background = r ? "#f44336" : "#4CAF50";
    fetch("/set?run=" + (r?1:0)).then(ok).catch(err);
    syncCfgFromUI();
    restartAnimation();
  }
  function u(k, v) {
    if(k=='speed') document.getElementById('sV').innerText = v;
    if(k=='angle') document.getElementById('aV').innerText = v + "°";
    if(k=='rad') document.getElementById('rV').innerText = v;
    if(k=='range') document.getElementById('rdV').innerText = v + "°";
    if(k=='mspace') { const wf = parseInt(document.getElementById('mType').value) >= 3; document.getElementById('msV').innerText = wf ? Math.round(v * 3.6) + '°' : v + ' cm'; }
    if(k=='frame') document.getElementById('fsV').innerText = v;
    if(k=='mapzoom') document.getElementById('mzV').innerText = v + "x";
    if(k=='cont') document.getElementById('cV').innerText = v;
    if(k=='shape') document.getElementById('zV').innerText = v;
    if(k=='edgec') document.getElementById('ecV').innerText = v;
    if(k=='fan') document.getElementById('fanV').innerText = Math.round(v / 255 * 100) + "%";
    if(k=='lamp') document.getElementById('lampV').innerText = Math.round(v / 255 * 100) + "%";
    if(k=='mapzoom' || k=='edgec') {
      syncCfgFromUI();
      render();
      return;
    }
    fetch("/set?" + k + "=" + v).then(ok).catch(err);
    syncCfgFromUI();
    render();
  }

  function togglePick(key) {
    if (selected.has(key)) selected.delete(key);
    else selected.add(key);
    updatePickUI();
  }

  function toggleAll() {
    if (selected.size === Object.keys(pickButtons).length) {
      selected.clear();
    } else {
      Object.keys(pickButtons).forEach(k => selected.add(k));
    }
    updatePickUI();
  }

  function updatePickUI() {
    Object.keys(pickButtons).forEach(k => {
      if (selected.has(k)) pickButtons[k].classList.add('on');
      else pickButtons[k].classList.remove('on');
    });
    selectAllBtn.textContent = (selected.size === Object.keys(pickButtons).length) ? 'ALL' : 'ALL';
  }

  function toggleSaveMode() {
    saveArmed = !saveArmed;
    saveBtn.classList.remove('blink', 'pulse');
    saveBtn.classList.toggle('armed', saveArmed);
  }

  function slotClick(idx) {
    if (saveArmed) {
      flashSavePress();
      saveSlot(idx);
      pulseSaveConfirm();
    } else {
      loadSlot(idx);
    }
  }

  function saveSlot(idx) {
    syncCfgFromUI();
    const payload = {};
    const map = {
      speed: document.getElementById('speed').value,
      angle: document.getElementById('angle').value,
      rad: document.getElementById('rad').value,
      range: document.getElementById('range').value,
      mspace: document.getElementById('mspace').value,
      frame: document.getElementById('frame').value,
      mapzoom: document.getElementById('mapzoom').value,
      cont: document.getElementById('cont').value,
      shape: document.getElementById('shape').value,
      edgec: document.getElementById('edgec').value,
      fan: document.getElementById('fan').value,
      lamp: document.getElementById('lamp').value
    };
    selected.forEach(k => payload[k] = map[k]);
    localStorage.setItem(`e4_slot_${idx}`, JSON.stringify(payload));
    markSlot(idx);
  }

  function loadSlot(idx) {
    const raw = localStorage.getItem(`e4_slot_${idx}`);
    if (!raw) return;
    const data = JSON.parse(raw);
    if (data.speed !== undefined) { document.getElementById('speed').value = data.speed; u('speed', data.speed/100); }
    if (data.angle !== undefined) { document.getElementById('angle').value = data.angle; u('angle', data.angle); }
    if (data.rad !== undefined) { document.getElementById('rad').value = data.rad; u('rad', data.rad); }
    if (data.range !== undefined) { document.getElementById('range').value = data.range; u('range', data.range); }
    if (data.mspace !== undefined) { document.getElementById('mspace').value = data.mspace; u('mspace', data.mspace); }
    if (data.frame !== undefined) { document.getElementById('frame').value = data.frame; u('frame', data.frame/1000); }
    if (data.mapzoom !== undefined) { document.getElementById('mapzoom').value = data.mapzoom; u('mapzoom', data.mapzoom/100); }
    if (data.cont !== undefined) { document.getElementById('cont').value = data.cont; u('cont', data.cont/100); }
    if (data.shape !== undefined) { document.getElementById('shape').value = data.shape; u('shape', data.shape/10); }
    if (data.edgec !== undefined) { document.getElementById('edgec').value = data.edgec; u('edgec', data.edgec/100); }
    if (data.fan !== undefined) { document.getElementById('fan').value = data.fan; u('fan', data.fan); }
    if (data.lamp !== undefined) { document.getElementById('lamp').value = data.lamp; u('lamp', data.lamp); }
    markSlot(idx);
  }

  function markSlot(idx) {
    const row = document.getElementById('storageRow');
    const slots = row.querySelectorAll('.slot');
    slots.forEach((b, i) => {
      if (i === idx - 1) b.classList.add('active');
      else b.classList.remove('active');
    });
  }

  function flashSavePress() {
    saveBtn.classList.remove('blink');
    void saveBtn.offsetWidth;
    saveBtn.classList.add('blink');
  }

  function pulseSaveConfirm() {
    saveBtn.classList.remove('pulse');
    void saveBtn.offsetWidth;
    saveBtn.classList.add('pulse');
  }
  function showControls() {
    const t = parseInt(document.getElementById('mType').value);
    const isNoise = t <= 2;
    document.getElementById('angle').closest('.slider-row').style.display = (t === 0) ? '' : 'none';
    document.getElementById('rad').closest('.slider-row').style.display = (t >= 1 && t <= 2) ? '' : 'none';
    document.getElementById('frame').closest('.slider-row').style.display = isNoise ? '' : 'none';
    document.getElementById('shape').closest('.slider-row').style.display = isNoise ? '' : 'none';
    document.getElementById('mapzoom').closest('.slider-row').style.display = isNoise ? '' : 'none';
    // mspace bleibt immer sichtbar, aber Label wechselt je nach Modus
    document.getElementById('msLabel').textContent = isNoise ? 'Motor Spacing (cm)' : 'Phasenversatz (°/Motor)';
    const msVal = document.getElementById('mspace').value;
    document.getElementById('msV').innerText = isNoise ? msVal + ' cm' : Math.round(msVal * 3.6) + '°';
  }
  function applyConfig(cfg) {
    currentCfg = { ...cfg };
    r = !!cfg.running;
    const b = document.getElementById('btn');
    b.innerText = r ? "STOP SYSTEM" : "START SYSTEM";
    b.style.background = r ? "#f44336" : "#4CAF50";

    document.getElementById('mType').value = cfg.moveType;
    document.getElementById('speed').value = Math.round(cfg.speed * 100);
    document.getElementById('angle').value = Math.round(cfg.angle);
    document.getElementById('rad').value = Math.round(cfg.radius);
    document.getElementById('range').value = Math.round(cfg.rangeDeg);
    document.getElementById('mspace').value = Math.round(cfg.motorSpacingCm);
    document.getElementById('frame').value = Math.round(cfg.framesize * 1000);
    document.getElementById('mapzoom').value = 100;
    document.getElementById('cont').value = Math.round(cfg.contrast * 100);
    document.getElementById('shape').value = Math.round(cfg.zShape * 10);
    document.getElementById('edgec').value = 29;
    document.getElementById('fan').value = cfg.fanSpeed || 0;
    document.getElementById('lamp').value = cfg.lampBrightness || 0;

    u('speed', cfg.speed);
    u('angle', cfg.angle);
    u('rad', cfg.radius);
    u('range', cfg.rangeDeg);
    u('mspace', cfg.motorSpacingCm);
    u('frame', cfg.framesize);
    u('mapzoom', 1.0);
    u('cont', cfg.contrast);
    u('shape', cfg.zShape);
    u('edgec', 0.29);
    u('fan', cfg.fanSpeed || 0);
    u('lamp', cfg.lampBrightness || 0);

    showControls();
    restartAnimation();
  }
  function ok() {
    const s = document.getElementById('status');
    s.className = "status ok";
    s.innerText = "OK";
  }
  function err(e) {
    const s = document.getElementById('status');
    s.className = "status error";
    s.innerText = "Error: " + e;
  }
  function init() {
    fetch("/config").then(r => r.json()).then(cfg => {
      offlineToggle.value = 0;
      offlineVal.innerText = "OFF";
      applyConfig(cfg);
    }).catch(() => {
      offlineToggle.value = 1;
      offlineVal.innerText = "ON";
      applyConfig(offlineCfg);
    });
    updatePickUI();
  }
  document.addEventListener('DOMContentLoaded', init);
  window.addEventListener('resize', () => {
    render();
  });

  function syncCfgFromUI() {
    if (!currentCfg) currentCfg = { ...offlineCfg };
    currentCfg.moveType = parseInt(document.getElementById('mType').value, 10);
    currentCfg.speed = parseFloat(document.getElementById('speed').value) / 100.0;
    currentCfg.angle = parseFloat(document.getElementById('angle').value);
    currentCfg.radius = parseFloat(document.getElementById('rad').value);
    currentCfg.rangeDeg = parseFloat(document.getElementById('range').value);
    currentCfg.motorSpacingCm = parseFloat(document.getElementById('mspace').value);
    currentCfg.framesize = parseFloat(document.getElementById('frame').value) / 1000.0;
    currentCfg.contrast = parseFloat(document.getElementById('cont').value) / 100.0;
    currentCfg.zShape = parseFloat(document.getElementById('shape').value) / 10.0;
    currentCfg.fanSpeed = parseInt(document.getElementById('fan').value, 10);
    currentCfg.lampBrightness = parseInt(document.getElementById('lamp').value, 10);
    currentCfg.running = r ? 1 : 0;
  }

  function toggleOffline() {
    const on = offlineToggle.value === "1";
    offlineVal.innerText = on ? "ON" : "OFF";
    if (on) {
      applyConfig(offlineCfg);
    } else {
      init();
    }
  }
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- E4 Simplex Shaper Starting ---");
  Serial.print("FW Version: ");
  Serial.println(FW_VERSION);

  // Load configuration from NVS
  loadConfig();

  // #aus
  // Usability Suggestion:
  // Add more detailed Serial output during setup to inform the user about the initialization progress,
  // e.g., "Initializing TMC drivers...", "Setting up WiFi Access Point...", "Web server started."
  // This is crucial for debugging and understanding the system's state during startup.
  // #aus
  SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);

  // #beschreibung: Initialisierung der Pins für Lüfter und Lampe im Setup.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 15:50:00 (Approximate)
  // #version: 0.4.2
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LAMP_PIN, OUTPUT);
  Serial.printf("Output mapping: LAMP->HOTEND(GPIO %d), FAN->BED(GPIO %d)\n", LAMP_PIN, FAN_PIN);
  // Optional: Initialize PWM channels for fan and lamp if not using simple digitalWrite
  // ledcSetup(0, 5000, 8); // channel 0, 5kHz, 8-bit resolution for fan
  // ledcAttachPin(FAN_PIN, 0);
  // ledcSetup(1, 5000, 8); // channel 1, 5kHz, 8-bit resolution for lamp
  // ledcAttachPin(LAMP_PIN, 1);

  auto initTMC = [](TMC2209Stepper &d) {
    d.begin(); d.toff(5); d.rms_current(600); d.microsteps(16);
    d.en_spreadCycle(false); d.pwm_autoscale(true);
  };
  initTMC(driverX); initTMC(driverY); initTMC(driverZ); initTMC(driverE);

  for(int i=0; i<4; i++) {
    steppers[i]->setMaxSpeed(8000);
    steppers[i]->setAcceleration(4000);
    // #aus
    // Usability Suggestion:
    // Consider adding a default homing routine for the motors here.
    // This ensures a consistent starting position and prevents unexpected movements on power-up,
    // which is a significant safety and usability concern for physical systems.
    // #aus
  }

  // --- Wi-Fi Setup Logic ---
  // Try to connect to saved Wi-Fi or start AP if none/fail
  if (webCfg.wifi_ssid != "") {
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(webCfg.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(webCfg.wifi_ssid.c_str(), webCfg.wifi_password.c_str());
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) { // Try for ~10 seconds
      delay(500);
      Serial.print(".");
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to Wi-Fi!");
      Serial.print("IP address: http://");
      Serial.println(WiFi.localIP());
      MDNS.begin("e4");
      Serial.println("mDNS aktiv: http://e4.local");
    } else {
      Serial.println("\nFailed to connect to Wi-Fi. Starting AP.");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("E4-Setup");
      apMode = true;
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.print("AP IP: http://");
      Serial.println(WiFi.softAPIP());
      Serial.println("Verbinde mit WLAN 'E4-Setup', dann Browser oeffnen.");
    }
  } else {
    Serial.println("Keine WLAN-Zugangsdaten. Starte AP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("E4-Setup");
    apMode = true;
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.print("AP IP: http://");
    Serial.println(WiFi.softAPIP());
    Serial.println("Verbinde mit WLAN 'E4-Setup', dann Browser oeffnen.");
  }

  // #aus
  // Usability Suggestion:
  // Print the IP address of the ESP32 to Serial after starting the softAP.
  // This makes it easy for the user to know which IP to connect to from their browser.
  // This was moved into the STA/AP logic above for better context.
  // #aus

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", index_html);
  });
  
  // #aus
  // Usability Suggestion:
  // Add a direct link/redirect in the web UI for easy access to the /install page.
  // For example, an "Einstellungen" or "Wi-Fi Setup" button.
  // #aus
  server.on("/install", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INSTALL_HTML);
  });

  server.on("/wifisave", HTTP_POST, [](AsyncWebServerRequest *req){
    String ssid = "";
    String password = "";
    if (req->hasParam("ssid", true)) { // true for POST
      ssid = req->getParam("ssid", true)->value();
    }
    if (req->hasParam("password", true)) {
      password = req->getParam("password", true)->value();
    }

    Serial.print("Received Wi-Fi credentials for SSID: ");
    Serial.println(ssid);

    portENTER_CRITICAL(&cfgMux);
    webCfg.wifi_ssid = ssid;
    webCfg.wifi_password = password;
    portEXIT_CRITICAL(&cfgMux);
    
    saveConfig(); // Save to NVS

    // Attempt to connect immediately (optional, or require reboot)
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(webCfg.wifi_ssid.c_str(), webCfg.wifi_password.c_str());

    req->send(200, "text/plain", "OK"); // Send OK response to browser
    Serial.println("Wi-Fi settings updated and saved. Board will attempt to connect.");
    Serial.println("Rebooting in 5 seconds to apply new Wi-Fi settings...");
    delay(5000);
    ESP.restart(); // Reboot to ensure clean Wi-Fi connection
  });

  // #beschreibung: Endpunkt für die "Set Zero"-Funktion hinzugefügt, um die aktuelle Motorposition als Nullpunkt zu setzen.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 15:10:00 (Approximate)
  // #version: 0.4.1
  server.on("/setzero", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Setting current motor positions to ZERO.");
    for (int i = 0; i < 4; i++) {
      steppers[i]->setCurrentPosition(0);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *req){
    portENTER_CRITICAL(&cfgMux);
    if(req->hasParam("run")) webCfg.running = req->getParam("run")->value().toInt();
    if(req->hasParam("type")) webCfg.moveType = req->getParam("type")->value().toInt();
    if(req->hasParam("speed")) webCfg.speed = req->getParam("speed")->value().toFloat();
    if(req->hasParam("angle")) webCfg.angle = req->getParam("angle")->value().toFloat();
    if(req->hasParam("rad")) webCfg.radius = req->getParam("rad")->value().toFloat();
    if(req->hasParam("range")) webCfg.rangeDeg = req->getParam("range")->value().toFloat();
    // #change: Allow runtime adjustment of motor spacing from the UI.
    // #author: Codex
    // #time: 2026-02-05 18:47:48 CET
    // #version: 0.2.4
    if(req->hasParam("mspace")) webCfg.motorSpacingCm = req->getParam("mspace")->value().toFloat();
    
    // Die 3 neuen Parameter
    if(req->hasParam("frame")) webCfg.framesize = req->getParam("frame")->value().toFloat();
    if(req->hasParam("cont")) webCfg.contrast = req->getParam("cont")->value().toFloat();
    if(req->hasParam("shape")) webCfg.zShape = req->getParam("shape")->value().toFloat();

    // #beschreibung: Handler für Lüfter- und Lampen-Parameter im /set-Endpunkt hinzugefügt.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 15:40:00 (Approximate)
    // #version: 0.4.2
    if(req->hasParam("fan")) webCfg.fanSpeed = req->getParam("fan")->value().toInt();
    if(req->hasParam("lamp")) webCfg.lampBrightness = req->getParam("lamp")->value().toInt();

    // #beschreibung: Handler für die individuellen Motor-Offset-Parameter im /set-Endpunkt hinzugefügt.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 16:35:00 (Approximate)
    // #version: 0.4.6
    for (int i = 0; i < 4; i++) {
      String mx = "m" + String(i + 1) + "x";
      String my = "m" + String(i + 1) + "y";
      if (req->hasParam(mx)) {
        webCfg.motorOffsets[i].x = req->getParam(mx)->value().toFloat();
      }
      if (req->hasParam(my)) {
        webCfg.motorOffsets[i].y = req->getParam(my)->value().toFloat();
      }
    }
    
    portEXIT_CRITICAL(&cfgMux);
    req->send(200, "text/plain", "OK");
  });

  // #change: Added /config endpoint so the UI can initialize from actual device state.
  // #author: Codex
  // #time: 2026-02-05 18:15:30 CET
  // #version: 0.2.2
  // #beschreibung: Enhanced /config endpoint to include `wifi_ssid` for debugging network state from UI.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 14:00:00 (Approximate)
  // #version: 0.3.0
  // #beschreibung: Kommentar zur Task-Pinning-Optimierung hinzugefügt.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 16:50:00 (Approximate)
  // #version: 0.4.7
  // Für eine weitergehende Optimierung könnte die Motorsteuerungslogik in einen
  // eigenen FreeRTOS-Task ausgelagert werden, der auf Core 1 läuft, während der
  // Webserver und das Wi-Fi auf Core 0 bleiben.
  // Beispiel:
  // xTaskCreatePinnedToCore(
  //   motorLoop,          /* Function to implement the task */
  //   "MotorControl",     /* Name of the task */
  //   10000,              /* Stack size in words */
  //   NULL,               /* Task input parameter */
  //   1,                  /* Priority of the task */
  //   &motorTaskHandle,   /* Task handle. */
  //   1);                 /* Core where the task should run */
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *req){
    portENTER_CRITICAL(&cfgMux);
    Config cfg = webCfg;
    portEXIT_CRITICAL(&cfgMux);

    String json;
    json.reserve(256);
    json += "{";
    json += "\"running\":" + String(cfg.running ? 1 : 0);
    json += ",\"moveType\":" + String(cfg.moveType);
    json += ",\"speed\":" + String(cfg.speed, 4);
    json += ",\"angle\":" + String(cfg.angle, 2);
    json += ",\"radius\":" + String(cfg.radius, 2);
    json += ",\"framesize\":" + String(cfg.framesize, 4);
    json += ",\"contrast\":" + String(cfg.contrast, 3);
    json += ",\"zShape\":" + String(cfg.zShape, 3);
    json += ",\"rangeDeg\":" + String(cfg.rangeDeg, 2);
    // #change: Expose motor spacing in /config for UI initialization.
    // #author: Codex
    // #time: 2026-02-05 18:47:48 CET
    // #version: 0.2.4
    json += ",\"motorSpacingCm\":" + String(cfg.motorSpacingCm, 2);
    // #beschreibung: Lüfter- und Lampen-Werte zum /config-Endpunkt hinzugefügt.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 15:45:00 (Approximate)
    // #version: 0.4.2
    json += ",\"fanSpeed\":" + String(cfg.fanSpeed);
    json += ",\"lampBrightness\":" + String(cfg.lampBrightness);
    
    // #beschreibung: Serialisierung der individuellen Motor-Offsets zum /config-Endpunkt hinzugefügt.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 16:40:00 (Approximate)
    // #version: 0.4.6
    json += ",\"motorOffsets\":[";
    for (int i = 0; i < 4; i++) {
      json += "{\"x\":" + String(cfg.motorOffsets[i].x, 2) + ",\"y\":" + String(cfg.motorOffsets[i].y, 2) + "}";
      if (i < 3) {
        json += ",";
      }
    }
    json += "]";

    json += ",\"wifi_ssid\":\"" + cfg.wifi_ssid + "\""; // Add Wi-Fi SSID for debugging
    json += "}";
    req->send(200, "application/json", json);
  });
  // Captive Portal: alle unbekannten URLs zur Startseite weiterleiten
  server.onNotFound([](AsyncWebServerRequest *req){
    req->redirect("/");
  });

  server.begin();
  Serial.println("Webserver gestartet.");
}

void loop() {
  if (apMode) dnsServer.processNextRequest();

  portENTER_CRITICAL(&cfgMux);
  Config cfg = webCfg;
  portEXIT_CRITICAL(&cfgMux);

  static bool motors_stopped = true; // Start in stopped state

  if(cfg.running) {
    if (motors_stopped) {
      motors_stopped = false;
      Serial.println("System started.");
    }
    
    // #beschreibung: Feste Update-Rate für die Noise-Berechnung zur Verbesserung der Bewegungsqualität implementiert.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 16:00:00 (Approximate)
    // #version: 0.4.3
    static unsigned long lastMotionCalcTime = 0;
    const unsigned int motionCalcInterval = 10; // 10ms = 100Hz

    if (millis() - lastMotionCalcTime > motionCalcInterval) {
      lastMotionCalcTime = millis();
      float dt = motionCalcInterval / 1000.0f; // Use fixed delta-time

      // --- 1. PFAD GENERATOR (Wo ist die Drohne?) ---
      // Dies bestimmt flightX und flightY
      
      if(cfg.moveType == 0) { // Linear
        float rad = cfg.angle * PI / 180.0f;
        flightX += cos(rad) * cfg.speed * dt * 10.0; 
        flightY += sin(rad) * cfg.speed * dt * 10.0;

        // #beschreibung: Drift im linearen Pfad begrenzt, um Stabilität bei langer Laufzeit zu gewährleisten.
        // #author: Gemini CLI Agent
        // #time: 2026-02-05 16:15:00 (Approximate)
        // #version: 0.4.5
        const double wrapLimit = 1000000.0;
        flightX = fmod(flightX, wrapLimit);
        flightY = fmod(flightY, wrapLimit);
      }
      else if(cfg.moveType <= 2) { // Loop / Lissajous
        timeAccumulator += cfg.speed * dt;
        if(cfg.moveType == 1) { // Kreis
          flightX = cfg.radius * cos(timeAccumulator);
          flightY = cfg.radius * sin(timeAccumulator);
        }
        else if(cfg.moveType == 2) { // Acht
          flightX = cfg.radius * cos(timeAccumulator);
          flightY = (cfg.radius * 0.5) * sin(timeAccumulator * 2.0);
        }
      }
      else { // Waveform Modi (3=Sinus, 4=Saw, 5=Rect)
        timeAccumulator += cfg.speed * dt;
      }

      // --- 2. NOISE PROCESSING ---
      float stepsPerDegree = (200.0 * 16.0) / 360.0; 
      float maxSteps = cfg.rangeDeg * stepsPerDegree;
      long maxStepsL = (long)maxSteps;

      for(int i=0; i<4; i++) {
        long target = 0;

        if (cfg.moveType >= 3) {
          // --- WAVEFORM MODI (3=Sinus, 4=Säge, 5=Rechteck) ---
          // Phasenversatz aus mspace: Wert 25 -> 90°, Wert 50 -> 180° usw.
          float phaseSpread = (cfg.motorSpacingCm / 100.0f) * 2.0f * PI;
          float phase = timeAccumulator + i * phaseSpread;
          float val = 0.0f;

          if (cfg.moveType == 3) { // Sinus
            val = sinf(phase);
          } else if (cfg.moveType == 4) { // Sägezahn
            val = 2.0f * fmodf(phase / (2.0f * PI), 1.0f) - 1.0f;
          } else if (cfg.moveType == 5) { // Rechteck
            val = sinf(phase) >= 0.0f ? 1.0f : -1.0f;
          }
          target = (long)(val * maxSteps * cfg.contrast);

        } else {
          // --- NOISE MODI (0=Linear, 1=Kreis, 2=Acht) ---
          float sampleX = (flightX + cfg.motorOffsets[i].x) * cfg.framesize;
          float sampleY = (flightY + cfg.motorOffsets[i].y) * cfg.framesize;
          float n = sn.noise(sampleX, sampleY);
          float nNorm = (n + 1.0f) / 2.0f;
          float exponent = abs(cfg.zShape);
          if (exponent < 0.1) exponent = 0.1;
          float nShaped = pow(nNorm, exponent);
          if (cfg.zShape < 0) nShaped = 1.0f - nShaped;
          float finalNoise = (nShaped * 2.0f) - 1.0f;
          target = (long)(finalNoise * maxSteps * cfg.contrast);
        }

        if (target > maxStepsL) target = maxStepsL;
        if (target < -maxStepsL) target = -maxStepsL;
        steppers[i]->moveTo(target);
      }
    }
  } else {
    // #beschreibung: Logik zum Anhalten der Motoren hinzugefügt, wenn das System gestoppt wird.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 16:10:00 (Approximate)
    // #version: 0.4.4
    if (!motors_stopped) {
      for (int i = 0; i < 4; i++) {
        // Option 1: Stop with deceleration (smoother)
        steppers[i]->stop(); 
        // Option 2: Disable drivers immediately (motors lose holding torque)
        // steppers[i]->disableOutputs(); 
      }
      motors_stopped = true;
      Serial.println("System stopped. Motors commanded to stop.");
    }
  }

  for(int i=0; i<4; i++) steppers[i]->run();

  // #beschreibung: Logik zum Auslesen des Temperatursensors und zur Steuerung von Lüfter und Lampe hinzugefügt.
  // #author: Gemini CLI Agent
  // #time: 2026-02-05 15:55:00 (Approximate)
  // #version: 0.4.2
  static unsigned long lastExtrasTime = 0;
  if (millis() - lastExtrasTime > 1000) { // Update extras once per second
    lastExtrasTime = millis();

    // --- 3. EXTRAS (Temp, Fan, Lamp) ---
    // Read temperature (placeholder logic for a typical thermistor)
    // int tempReading = analogRead(TEMP_SENSOR_PIN);
    // float tempC = convertAnalogToCelsius(tempReading); // Requires a conversion function based on your thermistor
    // You would then add tempC to the /config endpoint payload

    // Control Fan and Lamp (using PWM).
    // Mapping: fan slider drives BED connector, lamp slider drives HOTEND connector.
    analogWrite(FAN_PIN, cfg.fanSpeed);
    analogWrite(LAMP_PIN, cfg.lampBrightness);
    // For simple ON/OFF, you could use:
    // digitalWrite(FAN_PIN, cfg.fanSpeed > 0 ? HIGH : LOW);
    // digitalWrite(LAMP_PIN, cfg.lampBrightness > 0 ? HIGH : LOW);
  }
}
// #aus
// Usability Suggestion:
// Consider adding a watchdog timer reset within the loop if complex calculations
// or blocking operations are introduced, to prevent unexpected ESP32 resets.
// However, `steppers[i]->run()` should be called frequently, which helps.
// #aus
