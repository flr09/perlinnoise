#include "Sync.h"

namespace Sync {

portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex   = nullptr;
SemaphoreHandle_t loggerMutex = nullptr;  // Bug 77 (v4.4.24)

void init() {
    if (uartMutex == nullptr)   uartMutex   = xSemaphoreCreateMutex();
    if (loggerMutex == nullptr) loggerMutex = xSemaphoreCreateMutex();
}

} // namespace Sync
