#include "Logger.h"
#include "Sync.h"

namespace Logger {

static String buffer;
static constexpr size_t MAX_LEN = 6000;
static constexpr size_t TRIM_TO = 3000;

void addLog(const String& msg) {
    portENTER_CRITICAL(&Sync::motorMux);
    buffer += msg;
    buffer += '\n';
    if (buffer.length() > MAX_LEN) {
        buffer = buffer.substring(buffer.length() - TRIM_TO);
    }
    portEXIT_CRITICAL(&Sync::motorMux);
    Serial.println(msg);
}

String getBuffer() {
    String out;
    portENTER_CRITICAL(&Sync::motorMux);
    out = buffer;
    portEXIT_CRITICAL(&Sync::motorMux);
    return out;
}

String drainBuffer() {
    String out;
    portENTER_CRITICAL(&Sync::motorMux);
    out = buffer;
    buffer = "";
    portEXIT_CRITICAL(&Sync::motorMux);
    return out;
}

void clear() {
    portENTER_CRITICAL(&Sync::motorMux);
    buffer = "";
    portEXIT_CRITICAL(&Sync::motorMux);
}

} // namespace Logger
