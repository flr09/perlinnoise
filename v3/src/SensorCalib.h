#pragma once
#include <Arduino.h>
#include "Types.h"
#include "Driver.h"
#include "Sensor.h"

// Das "calib final" Modul für Sensor-Vermessung und Nullpunkt-Findung
// Einmal kalibriert, bleibt diese Logik unangetastet.

void runSensorCalibration(int i);
void runMotorHoming(int i);

// Interne Hilfsfunktion für die Kalibrierung (wird exportiert falls CalibTest sie braucht)
long touchSensorEdge(bool state, int dir, uint32_t speed, int samples = 3);
