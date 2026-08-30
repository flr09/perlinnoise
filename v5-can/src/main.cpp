#include <Arduino.h>
#include "driver/twai.h"

// Tool-Bezeichnung + Version (Chris, 2026-08-30): Punkt-zu-Punkt-Pendelbewegung
// mit motoreigener Rampe — als "Sinus"-Bewegungsmuster deklariert (FSD §3.6).
static const char *TOOL_NAME = "v5-can-sinus";
static const char *TOOL_VERSION = "0.1.0";

// T-CAN485 Pinbelegung (verifiziert aus Xinyuan-LilyGO/T-CAN485, pin_config.h)
static const gpio_num_t CAN_TX = GPIO_NUM_27;
static const gpio_num_t CAN_RX = GPIO_NUM_26;
static const int CAN_SPEED_MODE = 23;
static const int ME2107_EN = 16;  // Boost-Versorgung fuer CAN/RS485-Transceiver

// LKMTECH MS3506v2 Protokoll — LK-TECH CAN PROTOCOL V2.35 (verifiziert an echter
// Hardware + unabhaengig bestaetigt durch lkm_m5-Community-Lib)
static const uint8_t MOTOR_IDS[] = {1, 2, 3, 4};
static const int NUM_MOTORS = 4;

struct MotorState {
  float lastTargetDeg = 0;
  float lastActualDeg = 0;
  int lastPower = 0, lastSpeed = 0, lastTemp = 0;
  float lastVoltage = 0;
  int lastErr = -1;
  bool everResponded = false;
  // Punkt-zu-Punkt-Bewegung (Chris, 2026-08-30): EIN Ziel setzen, ankommen
  // lassen (Motor faehrt eigene Rampe durch), dann erst naechstes Ziel senden
  // — statt alle 150ms einen neuen Zwischenpunkt nachzuschieben (das hat die
  // interne Rampe des Motors staendig unterbrochen -> Gehacke/Gestotter).
  bool goingHigh = true;
  unsigned long moveStartMs = 0;
  bool waitingArrival = false;
  // Versatz (Chris, 2026-08-30): nach Ankunft nicht sofort das naechste Ziel
  // senden, sondern motorIndex*g_staggerStepMs warten — durchgehend wirksamer
  // Zeitversatz zwischen den Bewegungsstarts, nicht nur einmalig beim Booten.
  bool readyToSend = false;
  unsigned long pendingSendAtMs = 0;
};
MotorState motors[NUM_MOTORS];

static uint32_t cmdId(uint8_t motorId) { return 0x140 + motorId; }

void sendMotorOn(uint8_t motorId) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x88;
  twai_transmit(&msg, pdMS_TO_TICKS(50));
}

void sendReadStatus1(uint8_t motorId) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x9A;
  twai_transmit(&msg, pdMS_TO_TICKS(50));
}

// 0x9C Read motor state 2 (V2.35 §26) — Temp/TorqueCurrent(iq)/Speed/Encoder.
// Noetig fuer kontinuierliche Speed-Telemetrie waehrend einer Punkt-zu-Punkt-
// Bewegung, seit wir 0xA4 nur noch einmal pro Bewegung senden (Chris,
// 2026-08-30: Rampen-Aenderung zeigt sich nicht im Graphen -> Speed war
// zwischen den Bewegungen eingefroren, keine echten Zwischenwerte).
void sendReadMotorState2(uint8_t motorId) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x9C;
  twai_transmit(&msg, pdMS_TO_TICKS(50));
}

void sendReadMultiAngle(uint8_t motorId) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x92;
  twai_transmit(&msg, pdMS_TO_TICKS(50));
}

// 0xA4 Multi loop angle control command 2 (V2.35 §2.8): Ziel-Winkel + Speed-Limit
void sendPositionMove(uint8_t motorId, int32_t angleHundredthDeg, uint16_t maxSpeedDps) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0xA4;
  msg.data[2] = (uint8_t)(maxSpeedDps);
  msg.data[3] = (uint8_t)(maxSpeedDps >> 8);
  msg.data[4] = (uint8_t)(angleHundredthDeg);
  msg.data[5] = (uint8_t)(angleHundredthDeg >> 8);
  msg.data[6] = (uint8_t)(angleHundredthDeg >> 16);
  msg.data[7] = (uint8_t)(angleHundredthDeg >> 24);
  twai_transmit(&msg, pdMS_TO_TICKS(50));
  int idx = motorId - MOTOR_IDS[0];
  if (idx >= 0 && idx < NUM_MOTORS) motors[idx].lastTargetDeg = angleHundredthDeg / 100.0f;
}

// 0x33 Read acceleration (V2.35 §16) — DataAccel int32, 1 dps/s
void sendReadAccel(uint8_t motorId) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x33;
  twai_transmit(&msg, pdMS_TO_TICKS(50));
}

// 0x34 Write acceleration to RAM (V2.35 §17) — DataAccel int32, 1 dps/s
void sendWriteAccel(uint8_t motorId, int32_t accelDpsPerS) {
  twai_message_t msg = {};
  msg.identifier = cmdId(motorId);
  msg.data_length_code = 8;
  msg.data[0] = 0x34;
  msg.data[4] = (uint8_t)(accelDpsPerS);
  msg.data[5] = (uint8_t)(accelDpsPerS >> 8);
  msg.data[6] = (uint8_t)(accelDpsPerS >> 16);
  msg.data[7] = (uint8_t)(accelDpsPerS >> 24);
  twai_transmit(&msg, pdMS_TO_TICKS(50));
  Serial.printf("-> Motor %u: 0x34 Beschleunigung=%ld dps/s gesendet\n", motorId,
                (long)accelDpsPerS);
}

int motorIndex(uint8_t motorId) {
  for (int i = 0; i < NUM_MOTORS; i++)
    if (MOTOR_IDS[i] == motorId) return i;
  return -1;
}

void printReply(const twai_message_t &msg) {
  uint8_t motorId = (uint8_t)(msg.identifier - 0x140);
  int idx = motorIndex(motorId);
  Serial.printf("<- Motor %u  ID 0x%03X  DATA[0]=0x%02X  ", motorId, (unsigned)msg.identifier,
                msg.data[0]);
  for (int i = 0; i < msg.data_length_code; i++) Serial.printf("%02X ", msg.data[i]);

  if (idx < 0) {
    Serial.println();
    return;
  }
  MotorState &st = motors[idx];
  st.everResponded = true;

  if (msg.data[0] == 0x9A) {
    // V2.35 §2.24: DATA[1]=Temp, DATA[3..4]=Voltage(low,high, 0.1V/LSB), DATA[7]=errorState (1 Byte)
    int8_t temp = (int8_t)msg.data[1];
    uint16_t voltage = (uint16_t)(msg.data[3] | (msg.data[4] << 8));
    uint8_t err = msg.data[7];
    st.lastTemp = temp;
    st.lastVoltage = voltage * 0.1f;
    st.lastErr = err;
    Serial.printf("  [Temp=%d C, Voltage=%.1fV, ErrorState=0x%02X%s]", temp, voltage * 0.1,
                  err, err == 0 ? " OK" : " FEHLER");
  }
  if (msg.data[0] == 0xA4) {
    int8_t temp = (int8_t)msg.data[1];
    int16_t power = (int16_t)(msg.data[2] | (msg.data[3] << 8));
    int16_t speed = (int16_t)(msg.data[4] | (msg.data[5] << 8));
    uint16_t encoder = (uint16_t)(msg.data[6] | (msg.data[7] << 8));
    st.lastTemp = temp;
    st.lastPower = power;
    st.lastSpeed = speed;
    Serial.printf("  [Temp=%d C, Power=%d, Speed=%d dps, Encoder=%u]", temp, power, speed,
                  encoder);
    Serial.printf("\nSTAT t=%lu motor=%u target=%.2f actual=%.2f speed=%d power=%d", millis(),
                  motorId, st.lastTargetDeg, st.lastActualDeg, speed, power);
  }
  if (msg.data[0] == 0x9C) {
    // V2.35 §26: DATA[1]=Temp, DATA[2-3]=TorqueCurrent(iq), DATA[4-5]=Speed, DATA[6-7]=Encoder
    int8_t temp = (int8_t)msg.data[1];
    int16_t iq = (int16_t)(msg.data[2] | (msg.data[3] << 8));
    int16_t speed = (int16_t)(msg.data[4] | (msg.data[5] << 8));
    st.lastTemp = temp;
    st.lastSpeed = speed;
    Serial.printf("  [Temp=%d C, Iq=%.2fA, Speed=%d dps]", temp, iq * 0.01f, speed);
    Serial.printf("\nSTAT t=%lu motor=%u target=%.2f actual=%.2f speed=%d power=%d", millis(),
                  motorId, st.lastTargetDeg, st.lastActualDeg, speed, st.lastPower);
  }
  if (msg.data[0] == 0x92) {
    // V2.35 §21: DATA[1..7] = motorAngle int64 (7 Bytes LE), 0.01 Grad/LSB
    int64_t raw = 0;
    for (int i = 6; i >= 0; i--) raw = (raw << 8) | msg.data[1 + i];
    if (raw & (1LL << 55)) raw -= (1LL << 56);
    st.lastActualDeg = raw / 100.0f;
    Serial.printf("  [Ist-Winkel=%.2f deg]", st.lastActualDeg);
    Serial.printf("\nSTAT t=%lu motor=%u target=%.2f actual=%.2f speed=%d power=%d", millis(),
                  motorId, st.lastTargetDeg, st.lastActualDeg, st.lastSpeed, st.lastPower);
  }
  if (msg.data[0] == 0x33 || msg.data[0] == 0x34) {
    int32_t accel = (int32_t)((uint32_t)msg.data[4] | ((uint32_t)msg.data[5] << 8) |
                               ((uint32_t)msg.data[6] << 16) | ((uint32_t)msg.data[7] << 24));
    Serial.printf("  [Acceleration=%ld dps/s]", (long)accel);
  }
  Serial.println();
}


// Punkt-zu-Punkt-Bewegung (Chris, 2026-08-30): EIN Ziel pro Bewegung, der
// Motor faehrt seine eigene interne Rampe (Beschleunigung/Plateau/Bremsen)
// bis zum Ziel durch. Erst wenn er angekommen ist, wird das naechste
// (gegenueberliegende) Ziel gesendet — kein staendiges Nachschieben mehr.
// [ALS VARIABLEN, 2026-08-30] Nicht mehr const — muessen spaeter fein
// einstellbar sein, live per Serial-Kommando aenderbar (siehe applySetCommand()).
float g_amplitudeDeg = 170.0f;      // "Distance"
uint16_t g_maxSpeedDps = 210;       // "Speed"
int32_t g_accelDpsPerS = 163;       // "Rampe"
unsigned long g_staggerStepMs = 0;  // "Versatz" — motorIndex*Schrittweite, durchgehend wirksam

// [ANGELEGT, NICHT VERDRAHTET — Chris, 2026-08-30] "Duration": Laenge einer
// Amplituden-Bewegung. Beeinflusst Distance/Speed/Rampe wechselseitig (analog
// zur v4-Hyperbel-Kopplung, FSD v4 §0) — Kopplungsformel muss erst gemeinsam
// festgelegt werden, bevor das aktiv motion-relevant wird. Aktuell nur
// gespeichert + Stub-Funktion, wird in sendNextTarget() NICHT verwendet.
unsigned long g_durationMs = 3000;
static float computeSpeedFromDuration(float amplitudeDeg, unsigned long durationMs) {
  // Platzhalter: bei fester Distance und Rampe waere die noetige Cruise-Speed
  // ableitbar (aehnlich der Trapez-Herleitung von vorhin). Noch nicht validiert.
  if (durationMs == 0) return 0.0f;
  return (2.0f * amplitudeDeg) / (durationMs / 1000.0f);
}

static const float ARRIVAL_TOLERANCE_DEG = 5.0f;
static const int ARRIVAL_SPEED_THRESHOLD_DPS = 15;
static const unsigned long ARRIVAL_TIMEOUT_MS = 6000;  // Sicherheitsfallback

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== %s v%s — 4 Motoren, Punkt-zu-Punkt ===\n", TOOL_NAME, TOOL_VERSION);

  pinMode(ME2107_EN, OUTPUT);
  digitalWrite(ME2107_EN, HIGH);
  pinMode(CAN_SPEED_MODE, OUTPUT);
  digitalWrite(CAN_SPEED_MODE, LOW);  // High-Speed-Modus

  twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("FEHLER: TWAI-Treiber-Installation fehlgeschlagen");
    return;
  }
  if (twai_start() != ESP_OK) {
    Serial.println("FEHLER: TWAI-Start fehlgeschlagen");
    return;
  }
  Serial.println("TWAI-Treiber laeuft (1 Mbps, GPIO27=TX, GPIO26=RX)");
  delay(200);

  for (int i = 0; i < NUM_MOTORS; i++) {
    sendMotorOn(MOTOR_IDS[i]);
    delay(50);
  }
  delay(100);
  for (int i = 0; i < NUM_MOTORS; i++) {
    sendReadStatus1(MOTOR_IDS[i]);
    delay(50);
  }

  // Feste Rampe (Chris, 2026-08-30: "acc an die Welle anpassen" galt fuer die
  // kontinuierliche Kurve — die gibt's jetzt nicht mehr, ein Ziel pro Bewegung
  // braucht nur noch eine sinnvolle, feste Beschleunigung).
  Serial.printf("Rampe: %ld dps/s\n", (long)g_accelDpsPerS);
  delay(100);
  for (int i = 0; i < NUM_MOTORS; i++) {
    sendWriteAccel(MOTOR_IDS[i], g_accelDpsPerS);
    delay(80);
  }
}

unsigned long lastStatusPoll = 0;
unsigned long lastAnglePoll = 0;
int pollMotorCursor = 0;
unsigned long lastSpeedPoll = 0;
int speedPollCursor = 0;
bool started[NUM_MOTORS] = {false, false, false, false};

// Live-Tuning per Serial (Chris, 2026-08-30): "SET accel=163", "SET speed=210",
// "SET amp=170", "SET stagger0=0".."SET stagger3=1200" — Dashboard schreibt
// das ueber dieselbe serielle Verbindung, damit Rampe/Speed/Distance/Versatz
// ohne Neuflashen fein einstellbar sind.
void applySetCommand(const String &line) {
  if (!line.startsWith("SET ")) return;
  int eq = line.indexOf('=');
  if (eq < 0) return;
  String key = line.substring(4, eq);
  float val = line.substring(eq + 1).toFloat();
  if (key == "accel") {
    g_accelDpsPerS = (int32_t)val;
    for (int i = 0; i < NUM_MOTORS; i++) {
      sendWriteAccel(MOTOR_IDS[i], g_accelDpsPerS);
      delay(20);
    }
    Serial.printf("OK accel=%ld\n", (long)g_accelDpsPerS);
  } else if (key == "speed") {
    g_maxSpeedDps = (uint16_t)val;
    Serial.printf("OK speed=%u\n", g_maxSpeedDps);
  } else if (key == "amp") {
    g_amplitudeDeg = val;
    Serial.printf("OK amp=%.2f\n", g_amplitudeDeg);
  } else if (key == "stagger") {
    g_staggerStepMs = (unsigned long)val;
    Serial.printf("OK stagger=%lu (pro Motor: motorIndex*Schrittweite)\n", g_staggerStepMs);
  } else if (key == "duration") {
    g_durationMs = (unsigned long)val;
    Serial.printf(
        "OK duration=%lu (gespeichert, NICHT aktiv — computeSpeedFromDuration() waere %.1f dps/s)\n",
        g_durationMs, computeSpeedFromDuration(g_amplitudeDeg, g_durationMs));
  }
}

// Sendet EIN Ziel (aktuelle Richtung) und merkt sich Startzeit fuer Ankunftserkennung.
void sendNextTarget(int i) {
  float targetDeg = motors[i].goingHigh ? g_amplitudeDeg : -g_amplitudeDeg;
  sendPositionMove(MOTOR_IDS[i], (int32_t)(targetDeg * 100), g_maxSpeedDps);
  motors[i].moveStartMs = millis();
  motors[i].waitingArrival = true;
}

void loop() {
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) printReply(rx);

  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) applySetCommand(line);
  }

  // Punkt-zu-Punkt: erstes Ziel gestaffelt starten, danach je Motor erst das
  // naechste (gegenueberliegende) Ziel senden, wenn das aktuelle erreicht ist
  // — mit motorIndex*g_staggerStepMs Wartezeit davor (Versatz, durchgehend
  // wirksam, nicht nur beim Booten).
  for (int i = 0; i < NUM_MOTORS; i++) {
    if (!started[i]) {
      if (millis() > (unsigned long)i * g_staggerStepMs) {
        started[i] = true;
        sendNextTarget(i);
      }
      continue;
    }

    if (motors[i].readyToSend) {
      if (millis() >= motors[i].pendingSendAtMs) {
        motors[i].readyToSend = false;
        sendNextTarget(i);
      }
      continue;
    }

    if (!motors[i].waitingArrival) continue;

    bool closeEnough = fabsf(motors[i].lastActualDeg - motors[i].lastTargetDeg) < ARRIVAL_TOLERANCE_DEG;
    bool slowEnough = abs(motors[i].lastSpeed) < ARRIVAL_SPEED_THRESHOLD_DPS;
    bool timedOut = millis() - motors[i].moveStartMs > ARRIVAL_TIMEOUT_MS;

    if ((closeEnough && slowEnough) || timedOut) {
      Serial.printf("Motor %u: Ziel erreicht (%.2f deg%s) -> naechstes Ziel in %lums\n",
                    MOTOR_IDS[i], motors[i].lastActualDeg, timedOut ? ", TIMEOUT" : "",
                    (unsigned long)i * g_staggerStepMs);
      motors[i].waitingArrival = false;
      motors[i].goingHigh = !motors[i].goingHigh;
      motors[i].readyToSend = true;
      motors[i].pendingSendAtMs = millis() + (unsigned long)i * g_staggerStepMs;
    }
  }

  // Ist-Winkel reihum abfragen (1 Motor pro Tick, alle 100ms)
  if (millis() - lastAnglePoll > 100) {
    sendReadMultiAngle(MOTOR_IDS[pollMotorCursor]);
    pollMotorCursor = (pollMotorCursor + 1) % NUM_MOTORS;
    lastAnglePoll = millis();
  }

  // Speed kontinuierlich reihum abfragen (0x9C), sonst friert die Speed-
  // Telemetrie zwischen Bewegungsstart und -ende ein (Chris, 2026-08-30).
  if (millis() - lastSpeedPoll > 100) {
    sendReadMotorState2(MOTOR_IDS[speedPollCursor]);
    speedPollCursor = (speedPollCursor + 1) % NUM_MOTORS;
    lastSpeedPoll = millis();
  }

  // Status alle 3s pro Motor reihum
  if (millis() - lastStatusPoll > 3000) {
    static int statusCursor = 0;
    sendReadStatus1(MOTOR_IDS[statusCursor]);
    statusCursor = (statusCursor + 1) % NUM_MOTORS;
    lastStatusPoll = millis();
  }
}
