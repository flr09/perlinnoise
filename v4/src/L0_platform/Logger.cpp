#include "Logger.h"
#include "Sync.h"

namespace Logger {

static String buffer;
static constexpr size_t MAX_LEN = 6000;
static constexpr size_t TRIM_TO = 3000;

void addLog(const String& msg) {
    if (xSemaphoreTake(Sync::loggerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        buffer += msg;
        buffer += '\n';
        if (buffer.length() > MAX_LEN) {
            buffer = buffer.substring(buffer.length() - TRIM_TO);
        }
        xSemaphoreGive(Sync::loggerMutex);
    }
    Serial.println(msg);
}

String getBuffer() {
    String out;
    if (xSemaphoreTake(Sync::loggerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = buffer;
        xSemaphoreGive(Sync::loggerMutex);
    }
    return out;
}

String drainBuffer() {
    String out;
    if (xSemaphoreTake(Sync::loggerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = buffer;
        buffer = "";
        xSemaphoreGive(Sync::loggerMutex);
    }
    return out;
}

void clear() {
    if (xSemaphoreTake(Sync::loggerMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        buffer = "";
        xSemaphoreGive(Sync::loggerMutex);
    }
}

} // namespace Logger
