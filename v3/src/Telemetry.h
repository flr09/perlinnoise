#pragma once
#include <Arduino.h>
#include "Config.h"

extern String        telemCSV;
extern unsigned long telemStart;

void     clearTelemetry();
void     recordTelemetry(const char* phase, float val);
void     updateTelemCache();
uint16_t telemCacheSG();
uint8_t  telemCacheCS();
