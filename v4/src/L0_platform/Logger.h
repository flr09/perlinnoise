#pragma once
#include <Arduino.h>

// L0 — Logger
//
// addLog() ist Core-übergreifend sicher. Schreibt in Ring-Puffer und auf
// Serial. Web-Layer (L7) liest und leert den Puffer per drainBuffer().

namespace Logger {

void addLog(const String& msg);
String getBuffer();     // gibt Inhalt zurück ohne zu leeren
String drainBuffer();   // gibt Inhalt zurück und leert den Puffer (für /status)
void clear();

} // namespace Logger
