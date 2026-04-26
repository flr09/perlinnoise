#include "Hal_Pcnt.h"
#include "Hal_Pins.h"
#include "soc/io_mux_reg.h"

namespace HalPcnt {

PcntState state[4];

static const pcnt_unit_t units[4] = { PCNT_UNIT_0, PCNT_UNIT_1, PCNT_UNIT_2, PCNT_UNIT_3 };

void init() {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        pcnt_config_t cfg = {};
        cfg.pulse_gpio_num = HalPins::MOTORS[i].step;
        cfg.ctrl_gpio_num  = HalPins::MOTORS[i].dir;
        cfg.lctrl_mode     = PCNT_MODE_REVERSE;
        cfg.hctrl_mode     = PCNT_MODE_KEEP;
        cfg.pos_mode       = PCNT_COUNT_INC;
        cfg.neg_mode       = PCNT_COUNT_DIS;
        cfg.counter_h_lim  = 32767;
        cfg.counter_l_lim  = -32768;
        cfg.unit           = units[i];
        cfg.channel        = PCNT_CHANNEL_0;
        pcnt_unit_config(&cfg);
        pcnt_counter_pause(units[i]);
        pcnt_counter_clear(units[i]);
        pcnt_counter_resume(units[i]);
        state[i].base = 0;
    }
}

void initInputBuffers() {
    // v3 Fix F15: PCNT braucht aktiven Input-Buffer auf STEP-Pin.
    // FastAccelStepper konfiguriert Pin als OUTPUT — wir aktivieren zusätzlich
    // den Input-Pfad per IO_MUX FUN_IE-Bit (kein Konflikt mit RMT-Routing).
    static const uint32_t IO_MUX_REGS[4] = {
        IO_MUX_GPIO27_REG,  // X_STEP
        IO_MUX_GPIO33_REG,  // Y_STEP
        IO_MUX_GPIO14_REG,  // Z_STEP
        IO_MUX_GPIO16_REG,  // E_STEP
    };
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        PIN_INPUT_ENABLE(IO_MUX_REGS[i]);
    }
}

int16_t read(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    int16_t v = 0;
    pcnt_get_counter_value(units[motorIdx], &v);
    return v;
}

void clear(uint8_t motorIdx) {
    if (motorIdx >= 4) return;
    pcnt_counter_pause(units[motorIdx]);
    pcnt_counter_clear(units[motorIdx]);
    pcnt_counter_resume(units[motorIdx]);
}

void pause(uint8_t motorIdx)  { if (motorIdx < 4) pcnt_counter_pause(units[motorIdx]); }
void resume(uint8_t motorIdx) { if (motorIdx < 4) pcnt_counter_resume(units[motorIdx]); }

} // namespace HalPcnt
