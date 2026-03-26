#pragma once
#include <Arduino.h>

// --- FIRMWARE VERSION ---
#define FW_VERSION "3.7.1"

// --- HARDWARE PINS (FYSETC E4) ---
#define R_SENSE    0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX    21
#define UART_TX    22
#define TACHO_PIN  15

#define X_STEP 27
#define X_DIR  26
#define Y_STEP 33
#define Y_DIR  32
#define Z_STEP 14
#define Z_DIR  12
#define E_STEP 16
#define E_DIR  17

// --- MOTOR LIMITS ---
#define MOTOR_CURRENT_MIN_MA   300
#define MOTOR_CURRENT_MAX_MA   900
#define MOTOR_CURRENT_DEFAULT  650
#define MOTOR_SGTHRS_DEFAULT    50

// --- PARCOUR RPM RANGE ---
#ifndef PARCOUR_RPM_MAX
#define PARCOUR_RPM_START   300.0f
#define PARCOUR_RPM_MAX    2500.0f
#define PARCOUR_RPM_STEP    100.0f
#define PARCOUR_RPM_FINE     10.0f
#endif

// --- FREQUENCY SWEEP (continuous chirp) ---
#define FREQ_MIN_HZ        10.0f    // sweep start (Hz)
#define FREQ_MAX_HZ       200.0f    // sweep end   (Hz)
#define FREQ_SWEEP_S       30.0f    // total sweep duration (seconds)
#define FREQ_ACCEL_MAX   500000UL   // steps/s² — max accel / no ramp
#define FREQ_AMP_MIN_STEPS    3     // abort when amplitude falls below this

// --- TELEMETRY ---
#define TELEM_INTERVAL_MS  50
#define TELEM_MAX_BYTES    52000
