#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TMCStepper.h>
#include <AccelStepper.h>
#include "SimplexNoise.h"
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <AsyncElegantOTA.h>
#include <vector>

// --- VERSION & METADATA ---
// #change: Added version metadata and change log entries for traceable debugging.
// #author: Codex
// #time: 2026-02-05 18:47:48 CET
// #version: 0.2.4
static const char* FW_VERSION = "0.4.9";

// --- WIFI CREDENTIALS ---
const char* ota_ssid = "perlin";
const char* ota_password = "12345678";

// --- DUAL CORE TASKS ---
TaskHandle_t PerlinTask;
void PerlinLoop(void * pvParameters);

// --- HARDWARE PINS ---
#define R_SENSE 0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX 21
#define UART_TX 22

#define TEMP_SENSOR_PIN 36 
#define HOTEND_OUTPUT_PIN 2 // E0/Hotend MOSFET output (LAMP)
#define BED_OUTPUT_PIN 13   // Bed MOSFET output (FAN)
#define LAMP_PIN HOTEND_OUTPUT_PIN
#define FAN_PIN BED_OUTPUT_PIN

#define X_STEP 27
#define X_DIR  26
#define Y_STEP 33
#define Y_DIR  32
#define Z_STEP 14
#define Z_DIR  12
#define E_STEP 16
#define E_DIR  17

#define TACHO_PIN 15 // Z-MIN / PROBE Header

// --- CONFIG & STRUCTURES ---
struct MotorOffset {
  float x;
  float y;
};

struct Config {
  bool running = true;
  int moveType = 0;        // 0=Linear, 1=Circle, 2=Figure8, 3=Sine, 4=Saw, 5=Square
  float speed = 0.12;      // Fluggeschwindigkeit

  float angle = 45.0;      // Richtung (Linear)
  float radius = 50.0;     // Radius (Kreis/Acht)

  float framesize = 0.01;  // "Ausschnitt" (Frequenz/Zoom)
  float contrast = 1.94;   // "Hügelgröße" (Amplitude)
  float zShape = 1.0;      // "Form" (Gamma/Sharpness). 1.0=Normal, >1=Spitz, <0=Invertiert

  float rangeDeg = 300.0;
  float motorSpacingCm = 25.0; 

  MotorOffset motorOffsets[4] = {
    { -37.5, 0.0 }, { -12.5, 0.0 }, { 12.5, 0.0 }, { 37.5, 0.0 }
  };

  String wifi_ssid = "";
  String wifi_password = "";

  int fanSpeed = 0;
  int lampBrightness = 0;
};

Config webCfg;
portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;

struct Fav {
  float motors[4];
  String name;
};
std::vector<Fav> favs;
float favBias = 0.2; 

// --- TMC DRIVERS & STEPPERS ---
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

// --- GLOBAL OBJECTS ---
SimplexNoise sn;
AsyncWebServer server(80);
DNSServer dnsServer;
Preferences preferences;
bool apMode = false;

double flightX = 0;
double flightY = 0;
double timeAccumulator = 0;

// --- HTML CONTENT ---
const char INSTALL_HTML[] PROGMEM = R"rawliteral(
<!-- #change: Added installer version marker and traceable change metadata. -->
<!-- #author: Codex -->
<!-- #time: 2026-02-05 18:45:26 CET -->
<!-- #version: 0.2.3 -->
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
    <div style="margin-top:10px; font-size:0.75em; color:#888;">Installer v0.2.3</div>
  </div>
  <script>
    // #beschreibung: Enhanced Wi-Fi save status feedback for clearer user guidance during connection.
    // #author: Gemini CLI Agent
    // #time: 2026-02-05 14:15:00 (Approximate)
    // #version: 0.3.0
    document.querySelector('form').addEventListener('submit', function(event) {
      event.preventDefault();
      const form = event.target;
      const formData = new FormData(form);
      const urlParams = new URLSearchParams(formData).toString();
      
      const statusDiv = document.getElementById('status');
      statusDiv.className = "status";
      statusDiv.innerText = "Applying settings and restarting board..."; // Loading indicator

      fetch(form.action, {
        method: form.method,
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: urlParams
      })
      .then(response => response.text())
      .then(data => {
        if (data === "OK") {
          statusDiv.className = "status success";
          statusDiv.innerHTML = "Wi-Fi settings saved. Board is restarting to connect. Please connect your device to the configured network (SSID: " + formData.get('ssid') + ") or rejoin the 'E4-SETUP' AP if connection fails.";
        } else {
          statusDiv.className = "status error";
          statusDiv.innerText = "Error saving settings. Board might not have restarted or settings were invalid. Please try again. Error: " + data;
        }
      })
      .catch(error => {
        statusDiv.className = "status error";
        statusDiv.innerText = "Network error: " + error + ". Check if the board is still connected to this AP.";
      });
    });
  </script>
</body>
</html>
)rawliteral";

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
    .container { background-color: #444; padding: 20px; border-radius: 8px; max-width: 1200px; margin: 0 auto; }
    
    .main-layout {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 20px;
    }
    #left-column, #right-column {
      background-color: #3a3a3a;
      padding: 15px;
      border-radius: 8px;
    }

    h2 { color: #ff9800; }
    label { display: block; text-align: left; font-size: 0.8em; color: #bbb; margin-top: 12px; text-transform: uppercase; }
    select { width: 100%; padding: 10px; margin-top: 6px; background-color: #555; color: #eee; border: 1px solid #666; border-radius: 4px; }
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
    .angle-card { background: #2d2d2d; border: 1px solid #555; border-radius: 6px; padding: 6px; }
    .angle-card .label { font-size: 0.75em; color: #bbb; }
    .angle-card .value { font-size: 0.95em; color: #fff; }
    .gauge { width: 48px; height: 48px; border: 1px solid #666; border-radius: 50%; margin: 6px auto 0; position: relative; }
    .needle { position: absolute; left: 50%; top: 50%; width: 2px; height: 18px; background: #ff9800; transform-origin: 50% 100%; }
    .storage { display: grid; grid-template-columns: repeat(10, 1fr); gap: 6px; margin-top: 10px; }
    .slot { background: #2d2d2d; border: 1px solid #555; color: #eee; padding: 8px 0; border-radius: 6px; cursor: pointer; font-size: 0.85em; }
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
    .hidden { display: none; }
    @keyframes blinkRed { 0%, 100% { filter: brightness(1.0); } 50% { filter: brightness(1.8); } }
    @keyframes pulseRed { 0%, 100% { filter: brightness(1.0); } 50% { filter: brightness(1.6); } 100% { filter: brightness(1.0); } }

    @media (max-width: 900px) {
      .main-layout {
        grid-template-columns: 1fr;
      }
      body { margin: 10px; }
      .container { padding: 12px; }
      .angle-grid { grid-template-columns: repeat(2, 1fr); }
      .storage { grid-template-columns: repeat(5, 1fr); }
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>E4 Config Editor</h2>
    <div class="main-layout">
      <div id="left-column">
        <div class="note">Offline-Preview (kein ESP nötig). Framesize = Noise-Zoom, Phasenversatz steuert Welleneffekt.</div>
        <canvas id="noiseCanvas" width="640" height="640"></canvas>
        <div id="angles" class="angles"></div>
        <div class="angle-grid" id="angleGrid">
          <div class="angle-card"><div class="label">M1</div><div class="value" id="angleVal1">0.0°</div><div class="gauge"><div class="needle" id="needle1"></div></div></div>
          <div class="angle-card"><div class="label">M2</div><div class="value" id="angleVal2">0.0°</div><div class="gauge"><div class="needle" id="needle2"></div></div></div>
          <div class="angle-card"><div class="label">M3</div><div class="value" id="angleVal3">0.0°</div><div class="gauge"><div class="needle" id="needle3"></div></div></div>
          <div class="angle-card"><div class="label">M4</div><div class="value" id="angleVal4">0.0°</div><div class="gauge"><div class="needle" id="needle4"></div></div></div>
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
        <div class="stoprow">
          <button id="btn" onclick="toggle()">START SYSTEM</button>
          <button id="btn_zero" onclick="setZero()" style="background:#ff9800; margin-top: 10px;">ALIGN (SET 0°)</button>
          <a href="/update" style="color: #777; font-size: 0.7em; text-decoration: none; margin-top: 15px; display: block;">&bull; Funk-Flash (Web Update) &bull;</a>
        </div>

      </div>
      <div id="right-column">
        <label>Movement Pattern</label>
        <select id="mType" onchange="u('type', this.value); showControls();">
          <option value="0">LINEAR (Wind)</option>
          <option value="1">CIRCLE (Loop)</option>
          <option value="2">FIGURE 8 (Organic)</option>
          <option value="3">SINE (Wave)</option>
          <option value="4">SAWTOOTH</option>
          <option value="5">SQUARE</option>
        </select>
        
        <div class="slider-row">
          <button id="pick-speed" class="pick" onclick="togglePick('speed')">S</button>
          <div>
            <label>Flight Speed <span id="sV" class="val"></span></label>
            <input id="speed" type="range" min="0" max="200" value="50" oninput="u('speed', this.value/100)">
          </div>
        </div>

        <div id="cLinear">
          <div class="slider-row">
            <button id="pick-angle" class="pick" onclick="togglePick('angle')">A</button>
            <div>
              <label>Direction (Angle) / Bar Angle <span id="aV" class="val"></span></label>
              <input id="angle" type="range" min="0" max="360" value="0" oninput="u('angle', this.value)">
            </div>
          </div>
        </div>

        <div id="cRadius" class="hidden">
          <div class="slider-row">
            <button id="pick-rad" class="pick" onclick="togglePick('rad')">R</button>
            <div>
              <label>Path Radius <span id="rV" class="val"></span></label>
              <input id="rad" type="range" min="1" max="500" value="50" oninput="u('rad', this.value)">
            </div>
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
            <label>Motor Spacing (cm) <span id="msV" class="val"></span></label>
            <input id="mspace" type="range" min="5" max="100" value="25" oninput="u('mspace', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-dyn" class="pick" onclick="togglePick('dyn')">D</button>
          <div>
            <label>Drive Dynamics <span id="dynV" class="val"></span></label>
            <input id="dyn" type="range" min="0" max="2" step="1" value="1" oninput="u('dyn', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-frame" class="pick" onclick="togglePick('frame')">F</button>
          <div>
            <label>Framesize / Dicke <span id="fsV" class="val"></span></label>
            <input id="frame" type="range" min="1" max="500" value="20" oninput="u('frame', this.value/1000)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-mapzoom" class="pick" onclick="togglePick('mapzoom')">Z</button>
          <div>
            <label>Map Zoom / Anzahl <span id="mzV" class="val"></span></label>
            <input id="mapzoom" type="range" min="1" max="50" value="10" oninput="u('mapzoom', this.value)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-cont" class="pick" onclick="togglePick('cont')">C</button>
          <div>
            <label>Contrast <span id="cV" class="val"></span></label>
            <input id="cont" type="range" min="0" max="200" value="100" oninput="u('cont', this.value/100)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-shape" class="pick" onclick="togglePick('shape')">Z</button>
          <div>
            <label>Form (Z-Shape) / Kantenschärfe <span id="zV" class="val"></span></label>
            <input id="shape" type="range" min="-50" max="50" value="10" oninput="u('shape', this.value/10)">
          </div>
        </div>

        <div class="slider-row">
          <button id="pick-edgec" class="pick" onclick="togglePick('edgec')">E</button>
          <div>
            <label>Edge Contrast / Form <span id="ecV" class="val"></span></label>
            <input id="edgec" type="range" min="0" max="200" value="100" oninput="u('edgec', this.value/100)">
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
    <div style="margin-top:10px; font-size:0.75em; color:#888;">Editor v0.3.8</div>
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
    dyn: document.getElementById('pick-dyn'),
    frame: document.getElementById('pick-frame'),
    mapzoom: document.getElementById('pick-mapzoom'),
    cont: document.getElementById('pick-cont'),
    shape: document.getElementById('pick-shape'),
    edgec: document.getElementById('pick-edgec'),
    fan: document.getElementById('pick-fan'),
    lamp: document.getElementById('pick-lamp')
  };
  let currentCfg = null;
  let boardOnline = false;
  let animationFrameId = null;
  let lastTimestamp = 0;
  let lastFrameDt = 1.0 / 60.0;
  let flightX = 0;
  let flightY = 0;
  let timeAccumulator = 0;
  const displayedAngles = [0, 0, 0, 0];
  const displayedAngleVel = [0, 0, 0, 0];
  const pathHistory = [];
  const MAX_PATH_HISTORY = 160;
  let saveArmed = false;
  const selected = new Set(['speed','angle','rad','range','mspace','dyn','frame','mapzoom','cont','shape','edgec','fan','lamp']);

  const offlineCfg = {
    running: 1,
    moveType: 2,
    speed: 0.7,
    angle: 45,
    radius: 120,
    framesize: 0.02,
    contrast: 1.0,
    zShape: 1.0,
    rangeDeg: 300,
    motorSpacingCm: 25,
    dynMode: 1,
    fanSpeed: 0,
    lampBrightness: 0
  };

  // 3-step profile that jointly scales speed, acceleration and target dynamics.
  // 0=Langsam, 1=Normal, 2=Rasant
  const dynamicsProfiles = [
    { label: "LANGSAM", speedScale: 0.60, accelScale: 0.55, goalScale: 0.75 },
    { label: "NORMAL",  speedScale: 1.00, accelScale: 1.00, goalScale: 1.00 },
    { label: "RASANT",  speedScale: 1.65, accelScale: 1.85, goalScale: 1.30 }
  ];

  function getDynamicsMode() {
    const dynEl = document.getElementById('dyn');
    if (!dynEl) return 1;
    const mode = parseInt(dynEl.value, 10);
    if (!Number.isFinite(mode)) return 1;
    return Math.max(0, Math.min(2, mode));
  }

  function getDynamicsProfile() {
    return dynamicsProfiles[getDynamicsMode()] || dynamicsProfiles[1];
  }

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

  // Noise shaping: same as main.cpp logic
  function applyShape(n, zShape, edgeC) {
    const nNorm = (n + 1.0) / 2.0;
    let exponent = Math.abs(zShape);
    if (exponent < 0.1) exponent = 0.1;
    let nShaped = Math.pow(nNorm, exponent);
    if (zShape < 0) nShaped = 1.0 - nShaped;
    let finalNoise = (nShaped * 2.0) - 1.0;
    const edge = 1.0 - Math.abs(finalNoise);
    const edgeMix = Math.max(0.0, Math.min(2.0, edgeC));
    const edgeBoost = (edge * 2.0 - 1.0) * edgeMix;
    return finalNoise + edgeBoost;
  }

  // Waveform value: phase → [-1,1]
  // zShape repurposed: Saw=skew/curve, Square=duty cycle (0→50%, ±5→10%/90%)
  // edgeC as softness: higher=sharper (tanh soft-clip, 0=fully soft)
  function waveformValue(mType, phase, zShape, edgeC) {
    let val = 0;
    if (mType === 3) { // Sinus
      val = Math.sin(phase);
    } else if (mType === 4) { // Sägezahn
      const t = ((phase / (2 * Math.PI)) % 1.0 + 1.0) % 1.0; // 0..1
      const exp = Math.max(0.1, Math.exp(zShape * 0.25));
      val = Math.pow(t, exp) * 2.0 - 1.0;
    } else if (mType === 5) { // Rechteck, zShape = duty
      const duty = Math.max(0.05, Math.min(0.95, 0.5 + zShape * 0.08));
      const t = ((phase / (2 * Math.PI)) % 1.0 + 1.0) % 1.0;
      val = t < duty ? 1.0 : -1.0;
    }
    // Soft-clipping via edgeC: 0=hard, 2=silky smooth
    const softness = Math.max(0.0, 2.0 - edgeC);
    if (softness > 0.05) {
      const k = 3.0 / softness;
      val = Math.tanh(val * k) / Math.tanh(k);
    }
    return val;
  }

  function convertToGrayscale(noiseVal, contrast, zShape, edgeC) {
    let nNorm = (noiseVal + 1.0) / 2.0;
    let exponent = Math.abs(zShape);
    if (exponent < 0.1) exponent = 0.1;
    let nShaped = Math.pow(nNorm, exponent);
    if (zShape < 0) nShaped = 1.0 - nShaped;
    let finalNoise = (nShaped * 2.0) - 1.0;
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

  // BUGFIX-NOTE: mapzoom was mixed between two semantics (raw 1..50 and legacy /100 scale),
  // which collapsed motor spacing visually to near-zero at startup.
  function getMapZoomScale() {
    const slider = document.getElementById('mapzoom');
    const raw = parseFloat(slider.value);
    const max = parseFloat(slider.max || "50");
    if (!Number.isFinite(raw)) return 1.0;
    if (max <= 60) {
      return Math.max(0.2, raw / 10.0); // 1..50 -> 0.2x..5.0x
    }
    return Math.max(0.2, raw / 100.0); // legacy 20..300 -> 0.2x..3.0x
  }

  function getMapZoomDisplayText(rawValue) {
    const raw = parseFloat(rawValue);
    const zoom = getMapZoomScale();
    const stripes = Number.isFinite(raw) ? Math.max(1, Math.round(raw)) : 1;
    return `${zoom.toFixed(2)}x / ${stripes}`;
  }

  // BUGFIX-NOTE: keep cm->px conversion independent from current spacing slider value.
  // Otherwise spacing cancels out (`offsetX * worldScale`) and appears to do nothing.
  function getWorldScalePxPerCm() {
    const mapZoom = getMapZoomScale();
    const referenceSpacingCm = 25.0;
    const motorSpanPx = canvas.width * 0.6;
    const baseScale = motorSpanPx / (3.0 * referenceSpacingCm);
    return baseScale * mapZoom;
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
    const edgeC = parseFloat(document.getElementById('edgec').value) / 100.0;
    const worldScale = getWorldScalePxPerCm();
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
    const dyn = getDynamicsProfile();
    const speed = cfg.speed * dyn.speedScale;
    const angle = cfg.angle;
    const radius = cfg.radius;
    const moveType = parseInt(cfg.moveType);

    timeAccumulator += speed * dt; // Always increment time accumulator

    if (moveType === 0) {
      const rad = angle * Math.PI / 180.0;
      flightX += Math.cos(rad) * speed * dt * 10.0;
      flightY += Math.sin(rad) * speed * dt * 10.0;
    } else if (moveType === 1) {
      flightX = radius * Math.cos(timeAccumulator);
      flightY = radius * Math.sin(timeAccumulator);
    } else if (moveType === 2) {
      flightX = radius * Math.cos(timeAccumulator);
      flightY = (radius * 0.5) * Math.sin(timeAccumulator * 2.0);
    } else if (moveType >= 3) {
      // Waveform Modi: nur Zeit läuft, kein Pfad im Noise-Raum
      // flightX/Y werden für den Pfad-Canvas nicht verwendet
    }

    const wrapLimit = 1000000.0;
    flightX = flightX % wrapLimit;
    
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
    const dyn = getDynamicsProfile();
    const simSpeed = cfg.speed * dyn.speedScale;
    for (let i = 0; i < steps; i++) {
      if (cfg.moveType >= 4) continue; // Don't draw future path for simple oscillators
      if (cfg.moveType === 0) {
        const rad = cfg.angle * Math.PI / 180.0;
        fx += Math.cos(rad) * simSpeed * dt * 10.0;
        fy += Math.sin(rad) * simSpeed * dt * 10.0;
      } else {
        t += simSpeed * dt;
        if (cfg.moveType === 1) {
          fx = cfg.radius * Math.cos(t);
          fy = cfg.radius * Math.sin(t);
        } else if (cfg.moveType === 2) {
          fx = cfg.radius * Math.cos(t);
          fy = (cfg.radius * 0.5) * Math.sin(t * 2.0);
        } else if (cfg.moveType === 3) {
          fx += simSpeed * dt * 10.0;
          fy = cfg.radius * Math.sin(t * 2.0);
        }
      }
      pts.push({ x: fx, y: fy });
    }
    return pts;
  }

  function motorAnglesDeg() {
    if (!currentCfg) return [0, 0, 0, 0];
    const dyn = getDynamicsProfile();
    const range = currentCfg.rangeDeg;
    const contrast = currentCfg.contrast;
    const goalScale = dyn.goalScale;
    const zShape = currentCfg.zShape;
    const edgeC = parseFloat(document.getElementById('edgec').value) / 100.0;
    const angles = [];
    if (currentCfg.moveType >= 3) {
      // Waveform Modi: Phasenversatz aus mspace (25=90°)
      const phaseSpread = (currentCfg.motorSpacingCm / 100.0) * 2 * Math.PI;
      for (let i = 0; i < 4; i++) {
        const phase = timeAccumulator + i * phaseSpread;
        const val = waveformValue(currentCfg.moveType, phase, zShape, edgeC);
        angles.push(Math.max(-range, Math.min(range, val * contrast * range * goalScale)));
      }
    } else {
      // Noise Modi
      const framesize = currentCfg.framesize;
      for (let i = 0; i < 4; i++) {
        const offsetX = (i - 1.5) * currentCfg.motorSpacingCm;
        const noiseX = (offsetX + flightX) * framesize;
        const noiseY = flightY * framesize;
        const n = noise2D(noiseX, noiseY);
        const finalNoise = applyShape(n, zShape, edgeC);
        const angle = Math.max(-range, Math.min(range, finalNoise * contrast * range * goalScale));
        angles.push(angle);
      }
    }
    return angles;
  }

  function updateDisplayedAngles(targetAngles, dt) {
    const dyn = getDynamicsProfile();
    const maxSpeed = 720.0 * dyn.speedScale;
    const maxAccel = 1800.0 * dyn.accelScale;
    const safeDt = Math.max(0.001, Math.min(0.04, dt));

    for (let i = 0; i < 4; i++) {
      const target = targetAngles[i];
      const pos = displayedAngles[i];
      const vel = displayedAngleVel[i];
      const err = target - pos;

      // Bremseweg bei aktueller Geschwindigkeit: d = v² / (2a)
      const brakingDist = (vel * vel) / (2.0 * maxAccel);
      let targetVel;
      if (Math.abs(err) < brakingDist + 0.1) {
        // Jetzt bremsen — Zielgeschwindigkeit proportional zu verbleibender Distanz
        targetVel = Math.sign(err) * Math.sqrt(2.0 * maxAccel * Math.max(0, Math.abs(err)));
      } else {
        targetVel = Math.sign(err) * maxSpeed;
      }
      targetVel = Math.max(-maxSpeed, Math.min(maxSpeed, targetVel));
      const dv = Math.max(-maxAccel * safeDt, Math.min(maxAccel * safeDt, targetVel - vel));
      displayedAngleVel[i] += dv;
      displayedAngles[i] += displayedAngleVel[i] * safeDt;

      if (Math.abs(err) < 0.1 && Math.abs(displayedAngleVel[i]) < 1.0) {
        displayedAngles[i] = target;
        displayedAngleVel[i] = 0;
      }
    }
  }

  function drawPathAndMotors() {
    if (!currentCfg) return;
    resizeCanvas();
    const framesize = currentCfg.framesize;
    const cx = canvas.width / 2;
    const cy = canvas.height / 2;
    const worldScale = getWorldScalePxPerCm();
    
    if (currentCfg.moveType < 3) {
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
    }

    const isWaveMode = currentCfg.moveType >= 3;
    if (!isWaveMode) {
      ctx.beginPath();
      ctx.arc(cx - flightX * worldScale, cy - flightY * worldScale, 3, 0, Math.PI * 2);
      ctx.fillStyle = 'red';
      ctx.fill();
    }

    const targetAngles = motorAnglesDeg();
    updateDisplayedAngles(targetAngles, lastFrameDt);
    const ang = displayedAngles;
    for (let i = 0; i < 4; i++) {
      const offsetX = (i - 1.5) * currentCfg.motorSpacingCm;
      const motorX = cx + offsetX * worldScale;
      const motorY = cy;
      if (!isWaveMode) {
        ctx.beginPath();
        ctx.arc(motorX, motorY, 10, 0, Math.PI * 2);
        ctx.fillStyle = `hsl(${i * 90}, 100%, 50%)`;
        ctx.fill();
        ctx.strokeStyle = 'white'; ctx.lineWidth = 1; ctx.stroke();
        ctx.fillStyle = 'white'; ctx.font = '8px Arial';
        ctx.fillText(`M${i + 1}`, motorX - 6, motorY + 2);
        ctx.fillStyle = 'white'; ctx.font = '9px Arial';
        ctx.fillText(`${ang[i].toFixed(1)}°`, motorX - 10, motorY - 8);
      }
    }

    const a = ang;
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

  function drawWaveform() {
    resizeCanvas();
    const W = canvas.width;
    const H = canvas.height;
    const cy = H / 2;
    const amp = H * 0.36;
    const mType = currentCfg ? currentCfg.moveType : 3;
    const contrast = currentCfg ? Math.min(currentCfg.contrast, 1.5) : 1.0;
    const zShape = currentCfg ? currentCfg.zShape : 0;
    const edgeC = parseFloat(document.getElementById('edgec').value) / 100.0;
    const cycles = 3;

    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, W, H);

    // Grid
    ctx.strokeStyle = '#2a2a2a';
    ctx.lineWidth = 1;
    [0.25, 0.5, 0.75].forEach(f => {
      ctx.beginPath(); ctx.moveTo(0, H * f); ctx.lineTo(W, H * f); ctx.stroke();
    });
    ctx.strokeStyle = '#444';
    ctx.setLineDash([6, 6]);
    ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(W, cy); ctx.stroke();
    ctx.setLineDash([]);
    ctx.beginPath(); ctx.moveTo(0, cy - amp); ctx.lineTo(W, cy - amp); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, cy + amp); ctx.lineTo(W, cy + amp); ctx.stroke();

    // Wellenform
    ctx.strokeStyle = '#4CAF50';
    ctx.lineWidth = 2;
    let prevCycle = -1, prevSign = null;
    ctx.beginPath();
    for (let px = 0; px < W; px++) {
      const phase = (px / W) * cycles * 2 * Math.PI;
      const val = waveformValue(mType, phase, zShape, edgeC);
      let jump = false;
      if (mType === 4) {
        const cn = Math.floor(phase / (2 * Math.PI));
        if (cn !== prevCycle && px > 0) jump = true;
        prevCycle = cn;
      } else if (mType === 5) {
        const s = Math.sign(Math.sin(phase));
        if (s !== prevSign && prevSign !== null) jump = true;
        prevSign = s;
      }
      const y = cy - val * amp * contrast;
      if (px === 0 || jump) ctx.moveTo(px, y); else ctx.lineTo(px, y);
    }
    ctx.stroke();

    // Motor-Punkte
    const colors = ['#ff4444', '#44aaff', '#ffaa00', '#aa44ff'];
    const phaseSpread = currentCfg ? (currentCfg.motorSpacingCm / 100.0) * 2 * Math.PI : Math.PI / 2;
    const totalRange = cycles * 2 * Math.PI;
    for (let i = 0; i < 4; i++) {
      const phase = ((timeAccumulator + i * phaseSpread) % totalRange + totalRange) % totalRange;
      const px = (phase / totalRange) * W;
      const val = waveformValue(mType, phase, zShape, edgeC);
      const py = cy - val * amp * contrast;
      ctx.beginPath();
      ctx.arc(px, py, 8, 0, Math.PI * 2);
      ctx.fillStyle = colors[i];
      ctx.fill();
      ctx.strokeStyle = 'white'; ctx.lineWidth = 1; ctx.stroke();
      ctx.fillStyle = 'white'; ctx.font = '10px Arial';
      ctx.fillText(`M${i + 1}`, px - 6, py - 13);
    }
  }

  function render() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const mType = currentCfg ? currentCfg.moveType : 0;
    if (mType >= 3) {
      drawWaveform();
    } else {
      drawNoiseField();
    }
    drawPathAndMotors();
  }

  function animate(timestamp) {
    if (!lastTimestamp) lastTimestamp = timestamp;
    const deltaTime = (timestamp - lastTimestamp) / 1000.0;
    lastTimestamp = timestamp;
    lastFrameDt = Math.max(0.001, Math.min(0.050, deltaTime || (1.0 / 60.0)));
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
    lastFrameDt = 1.0 / 60.0;
    for (let i = 0; i < 4; i++) {
      displayedAngles[i] = 0;
      displayedAngleVel[i] = 0;
    }
    pathHistory.length = 0;
    animate(0);
  }

  function toggle() {
    r = !r;
    const b = document.getElementById('btn');
    b.innerText = r ? "STOP SYSTEM" : "START SYSTEM";
    b.style.background = r ? "#f44336" : "#4CAF50";
    if (boardOnline) {
      fetch("/set?run=" + (r?1:0)).then(ok).catch(err);
    } else {
      const s = document.getElementById('status');
      s.className = "status";
      s.innerText = "Offline preview mode (no device connection).";
    }
    syncCfgFromUI();
    restartAnimation();
  }

  function setZero() {
    if (r) { // if it's running, stop it first
      toggle();
    }
    if (boardOnline) {
      fetch("/setzero").then(ok).catch(err);
      document.getElementById('status').innerText = "Aligning motors to 0...";
    } else {
      const s = document.getElementById('status');
      s.className = "status";
      s.innerText = "Offline preview: SetZero requires board connection.";
    }
  }

  function u(k, v) {
    if(k=='speed') document.getElementById('sV').innerText = v;
    if(k=='angle') document.getElementById('aV').innerText = v + "°";
    if(k=='rad') document.getElementById('rV').innerText = v;
    if(k=='range') document.getElementById('rdV').innerText = v + "°";
    if(k=='mspace') { const wf = parseInt(document.getElementById('mType').value) >= 3; document.getElementById('msV').innerText = wf ? Math.round(v * 3.6) + '°' : v + ' cm'; }
    if(k=='dyn') {
      const mode = Math.max(0, Math.min(2, parseInt(v, 10) || 1));
      const p = dynamicsProfiles[mode];
      document.getElementById('dynV').innerText = `${p.label} (Vx${p.speedScale.toFixed(2)}, Ax${p.accelScale.toFixed(2)}, Gx${p.goalScale.toFixed(2)})`;
    }
    if(k=='frame') document.getElementById('fsV').innerText = v;
    if(k=='mapzoom') document.getElementById('mzV').innerText = getMapZoomDisplayText(v);
    if(k=='cont') document.getElementById('cV').innerText = v;
    if(k=='shape') document.getElementById('zV').innerText = v;
    if(k=='edgec') document.getElementById('ecV').innerText = v;
    if(k=='fan') document.getElementById('fanV').innerText = Math.round(v / 255 * 100) + "%";
    if(k=='lamp') document.getElementById('lampV').innerText = Math.round(v / 255 * 100) + "%";
    if(k=='type') {
      const t = parseInt(v, 10);
      syncCfgFromUI();
      render();
      if (boardOnline && Number.isFinite(t)) {
        fetch("/set?type=" + t).then(ok).catch(err);
      } else if (!boardOnline) {
        const s = document.getElementById('status');
        s.className = "status";
        s.innerText = "Offline preview mode (type not sent).";
      }
      return;
    }
    if(k=='mapzoom' || k=='edgec' || k=='dyn') {
      syncCfgFromUI();
      render();
      return;
    }
    if (boardOnline) {
      fetch("/set?" + k + "=" + v).then(ok).catch(err);
    }
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
      dyn: document.getElementById('dyn').value,
      frame: document.getElementById('frame').value,
      mapzoom: document.getElementById('mapzoom').value,
      cont: document.getElementById('cont').value,
      shape: document.getElementById('shape').value,
      edgec: document.getElementById('edgec').value,
      fan: document.getElementById('fan').value,
      lamp: document.getElementById('lamp').value
    };
    selected.forEach(k => payload[k] = map[k]);
    payload.mType = document.getElementById('mType').value;
    localStorage.setItem(`e4_slot_${idx}`, JSON.stringify(payload));
    markSlot(idx);
  }

  function loadSlot(idx) {
    const raw = localStorage.getItem(`e4_slot_${idx}`);
    if (!raw) return;
    let data;
    try { data = JSON.parse(raw); } catch(e) { localStorage.removeItem(`e4_slot_${idx}`); return; }
    if (data.speed !== undefined) { document.getElementById('speed').value = data.speed; u('speed', data.speed/100); }
    if (data.angle !== undefined) { document.getElementById('angle').value = data.angle; u('angle', data.angle); }
    if (data.rad !== undefined) { document.getElementById('rad').value = data.rad; u('rad', data.rad); }
    if (data.range !== undefined) { document.getElementById('range').value = data.range; u('range', data.range); }
    if (data.mspace !== undefined) { document.getElementById('mspace').value = data.mspace; u('mspace', data.mspace); }
    if (data.dyn !== undefined) { document.getElementById('dyn').value = data.dyn; u('dyn', data.dyn); }
    if (data.frame !== undefined) { document.getElementById('frame').value = data.frame; u('frame', data.frame/1000); }
    if (data.mapzoom !== undefined) {
      const mz = document.getElementById('mapzoom');
      mz.value = data.mapzoom;
      // BUGFIX-NOTE: use effective (possibly clamped) slider value for consistent preview text.
      u('mapzoom', mz.value);
    }
    if (data.cont !== undefined) { document.getElementById('cont').value = data.cont; u('cont', data.cont/100); }
    if (data.shape !== undefined) { document.getElementById('shape').value = data.shape; u('shape', data.shape/10); }
    if (data.edgec !== undefined) { document.getElementById('edgec').value = data.edgec; u('edgec', data.edgec/100); }
    if (data.fan !== undefined) { document.getElementById('fan').value = data.fan; u('fan', data.fan); }
    if (data.lamp !== undefined) { document.getElementById('lamp').value = data.lamp; u('lamp', data.lamp); }
    if (data.mType !== undefined) { document.getElementById('mType').value = data.mType; u('type', data.mType); showControls(); }
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
    const t = parseInt(document.getElementById('mType').value, 10);
    const isNoise = t <= 2;
    const isWave = t >= 3;

    document.getElementById('cLinear').style.display = (t === 0) ? 'block' : 'none';
    document.getElementById('cRadius').style.display = (t >= 1 && t <= 2) ? 'block' : 'none';

    // Noise-only controls ein/ausblenden
    const noiseOnlyIds = ['pick-frame', 'pick-mapzoom'];
    noiseOnlyIds.forEach(id => {
      const el = document.getElementById(id);
      if (el) el.closest('.slider-row').style.display = isNoise ? '' : 'none';
    });

    // mspace: label wechseln
    const msEl = document.getElementById('mspace');
    if (msEl) {
      const msLabel = msEl.closest('.slider-row').querySelector('label');
      const msV = document.getElementById('msV');
      if (isWave) {
        msLabel.childNodes[0].textContent = 'Phasenversatz (°/Motor) ';
        if (msV) msV.textContent = Math.round(parseFloat(msEl.value) * 3.6) + '°';
      } else {
        msLabel.childNodes[0].textContent = 'Motor Spacing (cm) ';
        if (msV) msV.textContent = msEl.value + ' cm';
      }
    }

    // shape: label wechseln je nach Wellenform
    const shapeLabel = document.getElementById('shape') &&
      document.getElementById('shape').closest('.slider-row').querySelector('label');
    if (shapeLabel) {
      if (t === 5) shapeLabel.childNodes[0].textContent = 'Duty Cycle ';
      else if (t === 4) shapeLabel.childNodes[0].textContent = 'Sägezahn-Kurve ';
      else shapeLabel.childNodes[0].textContent = 'Form (Z-Shape) / Kantenschärfe ';
    }

    // edgec: label wechseln
    const edgeLabel = document.getElementById('edgec') &&
      document.getElementById('edgec').closest('.slider-row').querySelector('label');
    if (edgeLabel) {
      edgeLabel.childNodes[0].textContent = isWave ? 'Flankenschärfe ' : 'Edge Contrast / Form ';
    }

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
    document.getElementById('dyn').value = (cfg.dynMode !== undefined) ? cfg.dynMode : 1;
    document.getElementById('frame').value = Math.round(cfg.framesize * 1000);
    // BUGFIX-NOTE: default to 1.0x equivalent to avoid collapsed spacing on startup.
    document.getElementById('mapzoom').value = 10;
    document.getElementById('cont').value = Math.round(cfg.contrast * 100);
    document.getElementById('shape').value = Math.round(cfg.zShape * 10);
    document.getElementById('edgec').value = 100;
    document.getElementById('fan').value = cfg.fanSpeed || 0;
    document.getElementById('lamp').value = cfg.lampBrightness || 0;

    u('speed', cfg.speed);
    u('angle', cfg.angle);
    u('rad', cfg.radius);
    u('range', cfg.rangeDeg);
    u('mspace', cfg.motorSpacingCm);
    u('dyn', (cfg.dynMode !== undefined) ? cfg.dynMode : 1);
    u('frame', cfg.framesize);
    u('mapzoom', 10);
    u('cont', cfg.contrast);
    u('shape', cfg.zShape);
    u('edgec', 1.0);
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

  function enforceStartupDefaults(isOnline) {
    // Autostart: sofort loslegen
    if (!r) {
      toggle();
    } else {
      if (isOnline) fetch("/set?run=1").then(ok).catch(err);
      syncCfgFromUI();
      restartAnimation();
    }
  }

  function init() {
    fetch("/config").then(r => r.json()).then(cfg => {
      boardOnline = true;
      offlineToggle.value = 0;
      offlineVal.innerText = "OFF";
      applyConfig(cfg);
      if (localStorage.getItem('e4_slot_1')) loadSlot(1);
      enforceStartupDefaults(true);
    }).catch(() => {
      boardOnline = false;
      offlineToggle.value = 1;
      offlineVal.innerText = "ON";
      applyConfig(offlineCfg);
      if (localStorage.getItem('e4_slot_1')) loadSlot(1);
      enforceStartupDefaults(false);
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
    currentCfg.dynMode = parseInt(document.getElementById('dyn').value, 10);
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

// --- MOTOR HELPER FUNCTIONS ---
void setSpreadCycle(int i, bool enable) {
  if (i == 0) driverX.en_spreadCycle(enable);
  else if (i == 1) driverY.en_spreadCycle(enable);
  else if (i == 2) driverZ.en_spreadCycle(enable);
  else if (i == 3) driverE.en_spreadCycle(enable);
}

void homeMotor(int i) {
  if (i < 0 || i > 3) return;
  Serial.printf("Homing Motor %d...\n", i);
  steppers[i]->setSpeed(1000);
  while (digitalRead(TACHO_PIN) == HIGH) {
    steppers[i]->runSpeed();
    yield();
  }
  steppers[i]->setCurrentPosition(0);
  steppers[i]->moveTo(200); 
  while (steppers[i]->distanceToGo() != 0) {
    steppers[i]->run();
    yield();
  }
  Serial.printf("Motor %d homed.\n", i);
}

void runMotorTest(int motorIndex) {
  if (motorIndex < 0 || motorIndex > 3) return;
  homeMotor(motorIndex);
  int testAccel = 2000;
  int testSpeed = 4000;
  while (testSpeed <= 12000) {
    Serial.printf("Test: Accel=%d, Speed=%d\n", testAccel, testSpeed);
    steppers[motorIndex]->setAcceleration(testAccel);
    steppers[motorIndex]->setMaxSpeed(testSpeed);
    steppers[motorIndex]->move(1600);
    while (steppers[motorIndex]->distanceToGo() != 0) {
      steppers[motorIndex]->run();
      yield();
    }
    testAccel += 1000;
    testSpeed += 2000;
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// --- CONFIG PERSISTENCE ---
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

void loadConfig() {
  preferences.begin("e4-config", false);
  webCfg.wifi_ssid = preferences.getString("ssid", "");
  webCfg.wifi_password = preferences.getString("pass", "");
  preferences.end();
}

void saveConfig() {
  preferences.begin("e4-config", false);
  preferences.putString("ssid", webCfg.wifi_ssid);
  preferences.putString("pass", webCfg.wifi_password);
  preferences.end();
}

// --- SETUP & LOOP ---
void setupDrivers() {
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LAMP_PIN, OUTPUT);
  auto initTMC = [](TMC2209Stepper &d) {
    d.begin(); d.toff(5); d.rms_current(600); d.microsteps(16);
    d.en_spreadCycle(false); d.pwm_autoscale(true);
  };
  initTMC(driverX); initTMC(driverY); initTMC(driverZ); initTMC(driverE);
  for(int i=0; i<4; i++) {
    steppers[i]->setMaxSpeed(8000);
    steppers[i]->setAcceleration(4000);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- E4 Simplex Shaper v2 Starting ---");
  Serial.print("FW Version: "); Serial.println(FW_VERSION);

  loadConfig();

  WiFi.mode(WIFI_STA);
  if (webCfg.wifi_ssid.length() > 0) {
    WiFi.begin(webCfg.wifi_ssid.c_str(), webCfg.wifi_password.c_str());
  } else {
    WiFi.begin(ota_ssid, ota_password);
  }

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500); Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed. Starting AP 'perlin-setup'...");
    WiFi.softAP("perlin-setup", "12345678");
    dnsServer.start(53, "*", WiFi.softAPIP());
    apMode = true;
  }

  ArduinoOTA.setHostname("perlin-bench");
  ArduinoOTA.setPassword("12345678");
  ArduinoOTA.begin();
  AsyncElegantOTA.begin(&server, "admin", "12345678");
  MDNS.begin("perlin");

  SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);
  pinMode(TACHO_PIN, INPUT_PULLUP);

  setupDrivers();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    if (apMode) req->send_P(200, "text/html", INSTALL_HTML);
    else req->send_P(200, "text/html", index_html);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *req){
    portENTER_CRITICAL(&cfgMux);
    Config cfg = webCfg;
    portEXIT_CRITICAL(&cfgMux);
    String json; json.reserve(512);
    json += "{\"running\":" + String(cfg.running ? 1 : 0);
    json += ",\"moveType\":" + String(cfg.moveType);
    json += ",\"speed\":" + String(cfg.speed, 4);
    json += ",\"angle\":" + String(cfg.angle, 2);
    json += ",\"radius\":" + String(cfg.radius, 2);
    json += ",\"framesize\":" + String(cfg.framesize, 4);
    json += ",\"contrast\":" + String(cfg.contrast, 3);
    json += ",\"zShape\":" + String(cfg.zShape, 3);
    json += ",\"rangeDeg\":" + String(cfg.rangeDeg, 2);
    json += ",\"motorSpacingCm\":" + String(cfg.motorSpacingCm, 2);
    json += ",\"fanSpeed\":" + String(cfg.fanSpeed);
    json += ",\"lampBrightness\":" + String(cfg.lampBrightness);
    json += ",\"motorOffsets\":[";
    for (int i = 0; i < 4; i++) {
      json += "{\"x\":" + String(cfg.motorOffsets[i].x, 2) + ",\"y\":" + String(cfg.motorOffsets[i].y, 2) + "}";
      if (i < 3) json += ",";
    }
    json += "],\"wifi_ssid\":\"" + jsonEscape(cfg.wifi_ssid) + "\"}";
    req->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *req){
    portENTER_CRITICAL(&cfgMux);
    if(req->hasParam("run")) webCfg.running = req->getParam("run")->value().toInt();
    if(req->hasParam("type")) webCfg.moveType = constrain(req->getParam("type")->value().toInt(), 0, 5);
    if(req->hasParam("speed")) webCfg.speed = req->getParam("speed")->value().toFloat();
    if(req->hasParam("angle")) webCfg.angle = req->getParam("angle")->value().toFloat();
    if(req->hasParam("rad")) webCfg.radius = req->getParam("rad")->value().toFloat();
    if(req->hasParam("range")) webCfg.rangeDeg = req->getParam("range")->value().toFloat();
    if(req->hasParam("mspace")) webCfg.motorSpacingCm = req->getParam("mspace")->value().toFloat();
    if(req->hasParam("frame")) webCfg.framesize = req->getParam("frame")->value().toFloat();
    if(req->hasParam("cont")) webCfg.contrast = req->getParam("cont")->value().toFloat();
    if(req->hasParam("shape")) webCfg.zShape = req->getParam("shape")->value().toFloat();
    if(req->hasParam("fan")) webCfg.fanSpeed = req->getParam("fan")->value().toInt();
    if(req->hasParam("lamp")) webCfg.lampBrightness = req->getParam("lamp")->value().toInt();
    for (int i = 0; i < 4; i++) {
      String mx = "m" + String(i + 1) + "x";
      String my = "m" + String(i + 1) + "y";
      if (req->hasParam(mx)) webCfg.motorOffsets[i].x = req->getParam(mx)->value().toFloat();
      if (req->hasParam(my)) webCfg.motorOffsets[i].y = req->getParam(my)->value().toFloat();
    }
    portEXIT_CRITICAL(&cfgMux);
    req->send(200, "text/plain", "OK");
  });

  server.on("/setzero", HTTP_GET, [](AsyncWebServerRequest *req){
    for (int i = 0; i < 4; i++) steppers[i]->setCurrentPosition(0);
    req->send(200, "text/plain", "OK");
  });

  server.on("/wifisave", HTTP_POST, [](AsyncWebServerRequest *req){
    if(req->hasParam("ssid", true)) webCfg.wifi_ssid = req->getParam("ssid", true)->value();
    if(req->hasParam("password", true)) webCfg.wifi_password = req->getParam("password", true)->value();
    saveConfig();
    req->send(200, "text/plain", "OK");
    delay(2000); ESP.restart();
  });

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *req){
    if(req->hasParam("m")) {
      int m = req->getParam("m")->value().toInt();
      runMotorTest(m);
      req->send(200, "text/plain", "Started Test.");
    } else req->send(400, "text/plain", "Missing m.");
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    if (apMode) req->send_P(200, "text/html", INSTALL_HTML);
    else req->redirect("/");
  });

  server.begin();
  xTaskCreatePinnedToCore(PerlinLoop, "PerlinTask", 10000, NULL, 1, &PerlinTask, 1);
}

void loop() {
  if (apMode) dnsServer.processNextRequest();
  ArduinoOTA.handle();

  static unsigned long lastExtrasTime = 0;
  if (millis() - lastExtrasTime > 500) {
    lastExtrasTime = millis();
    portENTER_CRITICAL(&cfgMux);
    int fSpeed = webCfg.fanSpeed;
    int lBright = webCfg.lampBrightness;
    portEXIT_CRITICAL(&cfgMux);
    analogWrite(FAN_PIN, fSpeed);
    analogWrite(LAMP_PIN, lBright);
  }
  vTaskDelay(1);
}

void PerlinLoop(void * pvParameters) {
  for(;;) {
    portENTER_CRITICAL(&cfgMux);
    Config cfg = webCfg;
    portEXIT_CRITICAL(&cfgMux);

    static bool motors_stopped = true;
    if(cfg.running) {
      if (motors_stopped) {
        motors_stopped = false;
        Serial.println("Core 1: Motors running.");
      }
      static unsigned long lastMotionCalcTime = 0;
      const unsigned int motionCalcInterval = 10;
      if (millis() - lastMotionCalcTime >= motionCalcInterval) {
        lastMotionCalcTime = millis();
        float dt = motionCalcInterval / 1000.0f;
        
        if (cfg.moveType == 0) {
          float rad = cfg.angle * PI / 180.0f;
          flightX += cos(rad) * cfg.speed * dt * 10.0;
          flightY += sin(rad) * cfg.speed * dt * 10.0;
          const double wrapLimit = 1000000.0;
          flightX = fmod(flightX, wrapLimit); flightY = fmod(flightY, wrapLimit);
        } else if (cfg.moveType <= 2) {
          timeAccumulator += cfg.speed * dt;
          if (cfg.moveType == 1) {
            flightX = cfg.radius * cos(timeAccumulator);
            flightY = cfg.radius * sin(timeAccumulator);
          } else if (cfg.moveType == 2) {
            flightX = cfg.radius * cos(timeAccumulator);
            flightY = (cfg.radius * 0.5) * sin(timeAccumulator * 2.0);
          }
        } else timeAccumulator += cfg.speed * dt;

        float stepsPerDegree = (200.0f * 16.0f) / 360.0f;
        float maxSteps = cfg.rangeDeg * stepsPerDegree;

        for (int i = 0; i < 4; i++) {
          float val = 0.0f;
          if (cfg.moveType >= 3) {
            float phaseSpread = (cfg.motorSpacingCm / 100.0f) * 2.0f * PI;
            float phase = timeAccumulator + i * phaseSpread;
            if (cfg.moveType == 3) val = sinf(phase);
            else if (cfg.moveType == 4) {
              float t = fmodf(phase / (2.0f * PI), 1.0f); if (t < 0.0f) t += 1.0f;
              float exp = fmaxf(0.1f, expf(cfg.zShape * 0.25f));
              val = powf(t, exp) * 2.0f - 1.0f;
            } else if (cfg.moveType == 5) {
              float t = fmodf(phase / (2.0f * PI), 1.0f); if (t < 0.0f) t += 1.0f;
              float duty = fmaxf(0.05f, fminf(0.95f, 0.5f + cfg.zShape * 0.08f));
              val = t < duty ? 1.0f : -1.0f;
            }
          } else {
            float sampleX = (flightX + cfg.motorOffsets[i].x) * cfg.framesize;
            float sampleY = (flightY + cfg.motorOffsets[i].y) * cfg.framesize;
            float n = sn.noise(sampleX, sampleY);
            float nNorm = (n + 1.0f) / 2.0f;
            float exponent = abs(cfg.zShape); if (exponent < 0.1) exponent = 0.1;
            float nShaped = pow(nNorm, exponent);
            if (cfg.zShape < 0) nShaped = 1.0f - nShaped;
            val = (nShaped * 2.0f) - 1.0f;
          }
          long target = (long)(val * maxSteps * cfg.contrast);
          long maxStepsL = (long)maxSteps;
          if (target > maxStepsL) target = maxStepsL; if (target < -maxStepsL) target = -maxStepsL;
          steppers[i]->moveTo(target);
        }
      }
      for(int i=0; i<4; i++) steppers[i]->run();
    } else {
      if (!motors_stopped) {
        motors_stopped = true;
        for(int i=0; i<4; i++) steppers[i]->stop();
        Serial.println("Core 1: Motors stopped.");
      }
    }
    vTaskDelay(1);
  }
}
