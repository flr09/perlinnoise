#include "MotorControl.h"
#include "Programs.h"
#include "SensorCalib.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = NULL;

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    if (sys.log.length() > 6000) sys.log = sys.log.substring(3000);
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

void saveCalibration(int i) {
    prefs.begin("cal", false);
    String key = "m" + String(i) + "_stable";
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("cal", true);
    for (int i = 0; i < 4; i++) {
        String key = "m" + String(i) + "_stable";
        if (prefs.getBytesLength(key.c_str()) == sizeof(CalibrationData)) {
            prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
            if (sys.cal[i].nvsVersion != 3619) sys.cal[i].valid = false;
        } else {
            sys.cal[i].valid = false;
        }
    }
    prefs.end();
}

void initMotors() {
    loadCalibration();
    uartMutex = xSemaphoreCreateMutex();
    initSensor();
    initDriver();
}

void homeMotor(int i) {
    runMotorHoming(i);
}

void characterizeSensor(int i) {
    runSensorCalibration(i);
}

void updateMotors() {
    if (sys.pendingStop && stepper) { stepper->stopMove(); sys.pendingStop = false; }
}
