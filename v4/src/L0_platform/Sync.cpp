#include "Sync.h"

namespace Sync {

portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = nullptr;

void init() {
    if (uartMutex == nullptr) {
        uartMutex = xSemaphoreCreateMutex();
    }
}

} // namespace Sync
