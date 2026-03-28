#pragma once
#include <Arduino.h>
#include "Config.h"

// Telemetrie-Modul (v3.7.26 calib final)
// Verwaltet Datensammlung auf Core 0 und CSV-Logging.

void initTelemetry();               // Startet Task auf Core 0
void resetTelemetryBuffer();        // Leert CSV
void recordDataPoint(const char* phase, float val); // Snapshot speichern

String getTelemetryCSV();           // Für Web-UI

// Zugriff auf TMC-Status (Live)
uint16_t getLatestSgResult();
uint8_t  getLatestCsActual();
bool     isMotorStalled();
