#pragma once
/*
 * Shared data types passed between tasks via queues.
 *
 * System architecture:
 *
 *   SensorTask  ──[SensorQueue]──►  ControlTask  ──[DisplayQueue]──►  DisplayTask
 *                                        │
 *                                        └──[AlertSemaphore]──────────►  DisplayTask
 *
 * SensorTask   : Generates temperature readings every 500 ms
 * ControlTask  : Reads readings, decides fan state, signals alert if critical
 * DisplayTask  : Formats and prints all status output; handles critical alerts
 */

#include <stdint.h>

/* Fan operating states — mirrors what PWM levels would be on hardware */
typedef enum {
    FAN_OFF    = 0,   /* T <= 30 C  */
    FAN_LOW    = 1,   /* 30 < T <= 50 C */
    FAN_HIGH   = 2,   /* 50 < T <= 75 C */
    FAN_MAX    = 3,   /* T > 75 C  — critical */
} FanState_t;

/* Packet sent from SensorTask → ControlTask via xSensorQueue */
typedef struct {
    float    temperature_c;
    uint32_t tick_ms;
} SensorReading_t;

/* Packet sent from ControlTask → DisplayTask via xDisplayQueue */
typedef struct {
    float      temperature_c;
    FanState_t fan_state;
    uint32_t   tick_ms;
    uint8_t    is_critical;   /* set when T > 75 C */
} DisplayMsg_t;

/* Temperature thresholds (degrees C) */
#define TEMP_LOW_THRESHOLD       30.0f
#define TEMP_HIGH_THRESHOLD      50.0f
#define TEMP_CRITICAL_THRESHOLD  75.0f

/* Queue depths */
#define SENSOR_QUEUE_DEPTH   8
#define DISPLAY_QUEUE_DEPTH  8

/* Task periods (ms) */
#define SENSOR_PERIOD_MS   500
#define CONTROL_PERIOD_MS  50     /* ControlTask polls queue; fast response */
#define DISPLAY_PERIOD_MS  100

/* Task stack sizes (words — unused in simulation but kept for STM32 portability) */
#define SENSOR_STACK_SIZE   256
#define CONTROL_STACK_SIZE  256
#define DISPLAY_STACK_SIZE  256

/* Task priorities */
#define SENSOR_PRIORITY    3
#define CONTROL_PRIORITY   4      /* highest — must respond to temp changes fast */
#define DISPLAY_PRIORITY   2

/* Global stop flag — DisplayTask sets this to 1 to end simulation cleanly */
extern volatile int g_sim_stop;
