#pragma once
/*
 * Shared data types and the runtime handshake between tasks.
 *
 *   SensorTask (P3) ──[xSensorQueue]──► ControlTask (P4) ──[xDisplayQueue]──► DisplayTask (P2)
 *                                             │
 *                                             └──[xAlertSemaphore]────────► DisplayTask (P2)
 *
 *   ControlTask ──[g_fan_duty_pct]──► SensorTask   (fan duty closes the thermal loop)
 *
 * All tasks share xConsoleMutex to keep console output from interleaving, and
 * observe g_sim_stop / g_sigint so Ctrl+C ends the run cleanly with a report.
 */

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>

#include "FreeRTOS.h"

/* ── Fan operating states ──────────────────────────────────── */
typedef enum {
    FAN_OFF  = 0,   /* T <= low threshold            */
    FAN_LOW  = 1,
    FAN_HIGH = 2,
    FAN_MAX  = 3,   /* T > critical threshold        */
    FAN_STATE_COUNT = 4,
} FanState_t;

/* ── Sensor health ─────────────────────────────────────────── */
typedef enum {
    SENSOR_OK = 0,
    SENSOR_STUCK,        /* value did not move between samples   */
    SENSOR_OUT_OF_RANGE, /* implausible reading                  */
    SENSOR_TIMEOUT,      /* no reading arrived inside the window */
} SensorFault_t;

/* SensorTask → ControlTask packet */
typedef struct {
    float    raw_c;          /* unfiltered plant output     */
    float    temperature_c;  /* EMA-filtered value          */
    uint32_t tick_ms;        /* sample timestamp (ticks)    */
    uint32_t sequence;       /* monotonic sample counter    */
} SensorReading_t;

/* ControlTask → DisplayTask packet */
typedef struct {
    float       temperature_c;  /* filtered value driving the decision */
    float       raw_c;
    FanState_t  fan_state;
    uint8_t     duty_pct;
    uint8_t     is_critical;
    SensorFault_t fault;        /* SENSOR_OK for a normal sample */
    uint32_t    tick_ms;
    uint32_t    sequence;
} DisplayMsg_t;

/* ── Queue depths ──────────────────────────────────────────── */
#define SENSOR_QUEUE_DEPTH   8
#define DISPLAY_QUEUE_DEPTH  8

/* ── Stack sizes (words; unused by the simulation, needed on target) ── */
#define SENSOR_STACK_SIZE   256
#define CONTROL_STACK_SIZE  256
#define DISPLAY_STACK_SIZE  256

/* ── Task priorities ───────────────────────────────────────── */
#define SENSOR_PRIORITY     3
#define CONTROL_PRIORITY    4   /* highest: the control loop must never lag */
#define DISPLAY_PRIORITY    2

/* ── Run control ───────────────────────────────────────────── */
extern atomic_int         g_sim_stop;     /* any task may end the run        */
extern volatile sig_atomic_t g_sigint;    /* set from the SIGINT handler     */
extern atomic_int         g_fan_duty_pct; /* ControlTask → SensorTask        */
extern _Atomic(uint32_t)  g_sim_start_ms; /* tick at the first task launch   */

int  sim_should_stop(void);
void sim_request_stop(void);
uint32_t sim_elapsed_ms(void);
int  sim_deadline_reached(uint32_t duration_ms);
