#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TMCStepper.h>
#include <AccelStepper.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ElegantOTA.h>
#include <vector>
#include "wifi_settings.h"

// --- CONFIG & PINS ---
#define FW_VERSION "3.0.0-TESTSUITE"
#define R_SENSE 0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX 21
#define UART_TX 22
#define TACHO_PIN 15

#define X_STEP 27
#define X_DIR  26
#define Y_STEP 33
#define Y_DIR  32
#define Z_STEP 14
#define Z_DIR  12
#define E_STEP 16
#define E_DIR  17

#define LAMP_PIN 2
#define FAN_PIN 13

// --- GLOBALS ---
AsyncWebServer server(80);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

struct TestState {
  bool active = false;
  int motorIdx = -1;
  float currentMaxSpd = 4000;
  float currentAccel = 2000;
  bool enabled[4] = {true, true, true, true};
  
  // Bug #11 & #5/6/7 Fix: Homing & Test Flags
  volatile int pendingHome = -1; // -1: none, 0-3: motor, 4: all
  volatile int pendingTest = -1; // -1: none, 0-3: motor
  volatile bool pendingStop = false;
  String logBuffer = "";
};
TestState ts;
portMUX_TYPE stepperMux = portMUX_INITIALIZER_UNLOCKED;

// --- UI CONTENT ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>V3 Motor Test Suite</title>
  <style>
    body { font-family: sans-serif; background: #121212; color: #e0e0e0; margin: 0; padding: 20px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; }
    .card { background: #1e1e1e; padding: 15px; border-radius: 8px; border-left: 5px solid #ff9800; }
    .motor-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
    .led { width: 12px; height: 12px; border-radius: 50%; background: #333; }
    .led.hit { background: #f44336; box-shadow: 0 0 8px #f44336; }
    .val { font-family: monospace; color: #00ff00; font-size: 1.2em; }
    .btn { background: #333; color: #fff; border: 1px solid #444; padding: 8px; border-radius: 4px; cursor: pointer; width: 100%; margin: 4px 0; }
    .btn.on { background: #4CAF50; border-color: #4CAF50; }
    .btn.test { background: #9c27b0; }
    .btn.stop { background: #f44336; padding: 15px; font-weight: bold; }
    .controls { background: #1e1e1e; padding: 20px; border-radius: 8px; margin-top: 20px; }
    input[type=range] { width: 100%; margin: 10px 0; }
    #log { background: #000; color: #0f0; padding: 10px; border-radius: 4px; height: 150px; overflow-y: auto; margin-top: 15px; font-size: 0.8em; text-align: left; }
  </style>
</head>
<body>
  <h1>E4 V3 Laboratory</h1>
  <div class="grid">
    <template id="motor-tpl">
      <div class="card">
        <div class="motor-header">
          <span class="name">MOTOR</span>
          <div class="led" id="led"></div>
        </div>
        <div>Winkel: <span class="val" id="pos">0.0</span>°</div>
        <div>Speed: <span class="val" id="spd">0</span> sps</div>
        <div style="margin-top:10px;">
          <button class="btn on" id="pwrBtn">POWER ON</button>
          <button class="btn" id="homeBtn">HOME MOTOR</button>
          <button class="btn test" id="testBtn">START PARKOUR</button>
        </div>
      </div>
    </template>
  </div>
  <div id="motor-container" class="grid"></div>

  <div class="controls">
    <div class="grid">
      <div>
        <label>MAX SPEED: <span id="maxSpdV">4000</span></label>
        <input type="range" min="100" max="15000" value="4000" oninput="set('speed', this.value)">
        <label>ACCEL: <span id="accV">2000</span></label>
        <input type="range" min="100" max="10000" value="2000" oninput="set('accel', this.value)">
      </div>
      <div>
        <button class="btn" onclick="cmd('goto', 90)">GOTO 90°</button>
        <button class="btn" onclick="cmd('goto', 180)">GOTO 180°</button>
        <button class="btn" onclick="cmd('homeall', 0)">HOME ALL</button>
        <button class="btn stop" onclick="cmd('stop', 0)">EMERGENCY STOP</button>
      </div>
    </div>
    <div id="log">Lade System...</div>
  </div>

  <script>
    const motors = ['X','Y','Z','E'];
    const container = document.getElementById('motor-container');
    const tpl = document.getElementById('motor-tpl');

    motors.forEach((name, i) => {
      const clone = tpl.content.cloneNode(true);
      clone.querySelector('.name').innerText = 'MOTOR ' + name;
      clone.querySelector('#led').id = 'led' + i;
      clone.querySelector('#pos').id = 'pos' + i;
      clone.querySelector('#spd').id = 'spd' + i;
      clone.querySelector('#pwrBtn').onclick = () => cmd('pwr', i);
      clone.querySelector('#pwrBtn').id = 'pwr' + i;
      clone.querySelector('#homeBtn').onclick = () => cmd('home', i);
      clone.querySelector('#testBtn').onclick = () => cmd('test', i);
      container.appendChild(clone);
    });

    function cmd(a, m) { fetch(`/cmd?a=${a}&m=${m}`); }
    function set(k, v) { 
      if(k=='speed') document.getElementById('maxSpdV').innerText = v;
      if(k=='accel') document.getElementById('accV').innerText = v;
      fetch(`/set?k=${k}&v=${v}`); 
    }

    function addLog(msg) {
      const log = document.getElementById('log');
      const div = document.createElement('div');
      div.innerText = `[${new Date().toLocaleTimeString()}] ${msg}`;
      log.prepend(div);
    }

    setInterval(() => {
      fetch('/status').then(r => r.json()).then(s => {
        s.m.forEach((motor, i) => {
          document.getElementById('pos'+i).innerText = motor.p.toFixed(1);
          document.getElementById('spd'+i).innerText = Math.round(motor.s);
          document.getElementById('led'+i).className = s.hit ? 'led hit' : 'led';
          const pBtn = document.getElementById('pwr'+i);
          pBtn.innerText = motor.e ? 'POWER ON' : 'POWER OFF';
          pBtn.className = motor.e ? 'btn on' : 'btn';
        });
        if(s.log) addLog(s.log);
      });
    }, 200);
  </script>
</body>
</html>
)rawliteral";

// --- MOTOR LOGIC ---
void setSpreadCycle(int i, bool enable) {
  if (i == 0) driverX.en_spreadCycle(enable);
  else if (i == 1) driverY.en_spreadCycle(enable);
  else if (i == 2) driverZ.en_spreadCycle(enable);
  else if (i == 3) driverE.en_spreadCycle(enable);
}

void homeMotor(int i) {
  if (i < 0 || i > 3) return;
  ts.logBuffer += "Homing Motor " + String(i) + "...\\n";
  setSpreadCycle(i, true);
  
  portENTER_CRITICAL(&stepperMux);
  steppers[i]->setSpeed(1500);
  portEXIT_CRITICAL(&stepperMux);

  long start = millis();
  const long maxSteps = 3200 * 10; // 10 Revs max
  long taken = 0;

  while (digitalRead(TACHO_PIN) == HIGH && (millis() - start < 15000) && taken < maxSteps) {
    portENTER_CRITICAL(&stepperMux);
    steppers[i]->runSpeed();
    portEXIT_CRITICAL(&stepperMux);
    taken++;
    yield();
  }
  
  portENTER_CRITICAL(&stepperMux);
  steppers[i]->setCurrentPosition(0);
  // Park at 180 deg (1600 steps at 3200 steps/rev)
  steppers[i]->moveTo(1600); 
  portEXIT_CRITICAL(&stepperMux);

  while(true) {
    portENTER_CRITICAL(&stepperMux);
    bool done = (steppers[i]->distanceToGo() == 0);
    steppers[i]->run();
    portEXIT_CRITICAL(&stepperMux);
    if(done) break;
    yield();
  }
  setSpreadCycle(i, false);
  ts.logBuffer += "Motor " + String(i) + " homed and parked.\\n";
}

void runMotorParkour(int i) {
  if (i < 0 || i > 3) return;
  ts.logBuffer += "Starting Parkour for Motor " + String(i) + "...\\n";
  homeMotor(i);
  
  // Example Stress Test: Rapid 360 back and forth
  for(int cycle=1; cycle<=3; cycle++) {
    ts.logBuffer += "Cycle " + String(cycle) + "...\\n";
    portENTER_CRITICAL(&stepperMux);
    steppers[i]->moveTo(0); // Go back to trigger
    portEXIT_CRITICAL(&stepperMux);
    while(steppers[i]->distanceToGo() != 0) { steppers[i]->run(); yield(); }
    
    portENTER_CRITICAL(&stepperMux);
    steppers[i]->moveTo(3200); // Full 360
    portEXIT_CRITICAL(&stepperMux);
    while(steppers[i]->distanceToGo() != 0) { steppers[i]->run(); yield(); }
  }
  ts.logBuffer += "Parkour complete.\\n";
}

// --- TASKS ---
void TaskCore1(void * pvParameters) {
  for(;;) {
    // 1. Handle Homing Request
    if (ts.pendingHome != -1) {
      int m = ts.pendingHome;
      ts.pendingHome = -1;
      if (m == 4) { // Home All
        for (int i=0; i<4; i++) homeMotor(i);
      } else {
        homeMotor(m);
      }
    }

    // 2. Handle Stress Test Request
    if (ts.pendingTest != -1) {
      int m = ts.pendingTest;
      ts.pendingTest = -1;
      runMotorParkour(m);
    }

    // 3. Standard Run
    portENTER_CRITICAL(&stepperMux);
    if (ts.pendingStop) {
      for(int i=0; i<4; i++) steppers[i]->stop();
      ts.pendingStop = false;
    }
    for(int i=0; i<4; i++) {
      if(ts.enabled[i]) steppers[i]->run();
    }
    portEXIT_CRITICAL(&stepperMux);
    
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW);
  pinMode(TACHO_PIN, INPUT_PULLUP);
  
  WiFi.setHostname("perlin-v3");
  WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) { 
    delay(500); Serial.print("."); 
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi Failed. Starting AP 'perlin-v3-setup'...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("perlin-v3-setup", "12345678");
  } else {
    Serial.println("\nWiFi Connected. IP: " + WiFi.localIP().toString());
    MDNS.begin("perlin-v3");
  }

  ArduinoOTA.setHostname("perlin-v3");
  ArduinoOTA.setPassword("12345678");
  ArduinoOTA.begin();
  
  SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  auto initTMC = [](TMC2209Stepper &d) { d.begin(); d.toff(5); d.rms_current(600); d.microsteps(16); d.pwm_autoscale(true); };
  initTMC(driverX); initTMC(driverY); initTMC(driverZ); initTMC(driverE);

  server.on("/", [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });
  server.on("/status", [](AsyncWebServerRequest *r){
    String j = "{\"hit\":" + String(digitalRead(TACHO_PIN)==LOW?"true":"false");
    j += ",\"log\":\"" + ts.logBuffer + "\",\"m\":[";
    ts.logBuffer = ""; // Clear buffer after sending
    for(int i=0; i<4; i++) {
      portENTER_CRITICAL(&stepperMux);
      long pos = steppers[i]->currentPosition();
      float spd = steppers[i]->speed();
      portEXIT_CRITICAL(&stepperMux);
      float deg = (pos % 3200) * 360.0 / 3200.0;
      j += "{\"p\":"+String(deg,1)+",\"s\":"+String(spd)+",\"e\":"+String(ts.enabled[i]?"true":"false")+"}";
      if(i<3) j+=",";
    }
    j += "]}";
    r->send(200, "application/json", j);
  });

  server.on("/cmd", [](AsyncWebServerRequest *r){
    if(!r->hasParam("a")) { r->send(400, "text/plain", "Missing a"); return; }
    String a = r->getParam("a")->value();
    int m = r->hasParam("m") ? r->getParam("m")->value().toInt() : 0;
    
    if(a=="home") ts.pendingHome = m;
    if(a=="homeall") ts.pendingHome = 4;
    if(a=="test") ts.pendingTest = m;
    if(a=="pwr") { ts.enabled[m] = !ts.enabled[m]; }
    if(a=="stop") { ts.pendingStop = true; }
    if(a=="goto") { 
      portENTER_CRITICAL(&stepperMux);
      steppers[0]->moveTo(m * 3200 / 360); 
      steppers[1]->moveTo(m * 3200 / 360); 
      steppers[2]->moveTo(m * 3200 / 360); 
      steppers[3]->moveTo(m * 3200 / 360); 
      portEXIT_CRITICAL(&stepperMux);
    }
    r->send(200, "text/plain", "OK");
  });

  server.on("/set", [](AsyncWebServerRequest *r){
    if(!r->hasParam("k") || !r->hasParam("v")) { r->send(400, "text/plain", "Missing k/v"); return; }
    String k = r->getParam("k")->value();
    float v = r->getParam("v")->value().toFloat();
    portENTER_CRITICAL(&stepperMux);
    if(k=="speed") { ts.currentMaxSpd = v; for(int i=0; i<4; i++) steppers[i]->setMaxSpeed(v); }
    if(k=="accel") { ts.currentAccel = v; for(int i=0; i<4; i++) steppers[i]->setAcceleration(v); }
    portEXIT_CRITICAL(&stepperMux);
    r->send(200, "text/plain", "OK");
  });

  ElegantOTA.begin(&server, "admin", "12345678");
  server.begin();
  xTaskCreatePinnedToCore(TaskCore1, "MotorTask", 10000, NULL, 1, NULL, 1);
}

void loop() { ArduinoOTA.handle(); }
