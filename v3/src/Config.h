#pragma once
#include <Arduino.h>

// --- FIRMWARE VERSION ---
#define FW_VERSION "3.7.20"

// --- HARDWARE PINS (FYSETC E4) ---
#define R_SENSE    0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX    21
#define UART_TX    22

#define X_STEP     27
#define X_DIR      26
#define TACHO_PIN  15

// --- MOTOR DEFAULTS ---
#define MOTOR_CURRENT_DEFAULT 800
#define MOTOR_CURRENT_MAX_MA  1200
#define MOTOR_SGTHRS_DEFAULT  100

// --- PARCOUR SETTINGS ---
#define PARCOUR_RPM_MAX   2500
#define PARCOUR_RPM_STEP  100

// --- FREQ SWEEP ---
#define FREQ_MIN_HZ       10.0f
#define FREQ_MAX_HZ       200.0f
#define FREQ_SWEEP_S      15.0f
#define FREQ_ACCEL_MAX    500000UL
#define FREQ_AMP_MIN_STEPS 5

// --- SENSOR ---
#define TACHO_NOISE_FILTER_MS  10

// --- TELEMETRY ---
#define TELEM_INTERVAL_MS  50
#define TELEM_MAX_BYTES    52000
