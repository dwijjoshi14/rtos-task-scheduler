/*
 * control_task.c — threshold control, fail-safe handling and telemetry.
 *
 * Failure policy: a reading that is implausible or stuck is never used to make
 * a decision. The controller holds its last known-good fan state (fail-safe),
 * reports the fault once per episode, and keeps running so the plant stays
 * protected. A silent sensor is treated the same way after the watchdog window.
 */

#define _POSIX_C_SOURCE 200809L

#include "control_task.h"

#include "control_logic.h"
#include "metrics.h"
#include "sensor_task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

QueueHandle_t     xDisplayQueue   = NULL;
SemaphoreHandle_t xAlertSemaphore = NULL;
SemaphoreHandle_t xConsoleMutex   = NULL;

static void log_transition(SemaphoreHandle_t console, FanState_t from,
                           FanState_t to, float temp_c, uint8_t duty_pct) {
    if (console) xSemaphoreTake(console, portMAX_DELAY);
    printf("[ControlTask] fan %s → %s  (%.1f C, duty %u%%)\n",
           fan_state_name(from), fan_state_name(to), (double)temp_c, duty_pct);
    fflush(stdout);
    if (console) xSemaphoreGive(console);
}

static void publish(const SensorReading_t* reading, FanState_t state,
                    uint8_t duty, int critical, SensorFault_t fault) {
    DisplayMsg_t msg;
    memset(&msg, 0, sizeof msg);
    msg.temperature_c = reading->temperature_c;
    msg.raw_c         = reading->raw_c;
    msg.fan_state     = state;
    msg.duty_pct      = duty;
    msg.is_critical   = (uint8_t)(critical ? 1 : 0);
    msg.fault         = fault;
    msg.tick_ms       = reading->tick_ms;
    msg.sequence      = reading->sequence;

    if (xQueueSend(xDisplayQueue, &msg, pdMS_TO_TICKS(10)) != pdPASS) {
        metrics_record_drop();
    }
}

void ControlTask(void* params) {
    AppConfig_t* cfg = (AppConfig_t*)params;

    ControlPolicy_t policy;
    control_policy_init(&policy, &cfg->thresholds);

    FanState_t    state     = FAN_OFF;
    int           critical  = 0;
    SensorFault_t last_fault = SENSOR_OK;
    uint32_t      last_tick = xTaskGetTickCount();
    uint32_t      window_first = 0;
    uint32_t      last_sequence = 0;
    int           have_sample = 0;

    const TickType_t watchdog = pdMS_TO_TICKS(cfg->watchdog_ms);

    for (;;) {
        if (sim_should_stop()) break;

        SensorReading_t reading;
        memset(&reading, 0, sizeof reading);

        if (xQueueReceive(xSensorQueue, &reading, watchdog) != pdPASS) {
            if (sim_should_stop()) break;

            /* Nothing arrived inside the watchdog window: do not change the
             * actuator state, report the fault and keep watching. */
            metrics_record_fault(SENSOR_TIMEOUT);
            if (last_fault != SENSOR_TIMEOUT) {
                SensorReading_t placeholder;
                memset(&placeholder, 0, sizeof placeholder);
                placeholder.tick_ms  = (uint32_t)xTaskGetTickCount();
                placeholder.sequence = last_sequence;
                publish(&placeholder, state, fan_duty_pct(state),
                        critical, SENSOR_TIMEOUT);
                last_fault = SENSOR_TIMEOUT;
            }
            continue;
        }
        if (sim_should_stop()) break;

        if (have_sample && reading.sequence <= last_sequence) continue;  /* duplicate */
        last_sequence = reading.sequence;

        /* Health checks run on the *raw* signal: the EMA would smooth a stuck
         * sensor into a stream of plausible-looking changes and hide it. The
         * control decision still uses the filtered value. */
        const SensorFault_t fault = control_policy_check(&policy, reading.raw_c);

        if (fault != SENSOR_OK) {
            metrics_record_fault(fault);
            if (last_fault != fault) {
                publish(&reading, state, fan_duty_pct(state),
                        critical, fault);
                last_fault = fault;
            }
            continue;   /* hold the last good actuator state */
        }
        last_fault = SENSOR_OK;

        const uint32_t dt_ms = have_sample
            ? (uint32_t)(reading.tick_ms - last_tick)
            : cfg->sensor_period_ms;

        FanState_t next = fan_next_state(state, reading.temperature_c,
                                         &cfg->thresholds);
        const uint8_t duty = fan_duty_pct(next);
        const int     now_critical = (next == FAN_MAX);

        if (next != state) {
            log_transition(xConsoleMutex, state, next, reading.temperature_c, duty);
            metrics_record_transition(next);
            state = next;
        }

        if (now_critical && !critical) {
            metrics_record_alert();
            xSemaphoreGive(xAlertSemaphore);   /* binary: safe if already set */
        }
        critical = now_critical;

        atomic_store(&g_fan_duty_pct, (int)duty);

        metrics_record_sample(reading.temperature_c, state, dt_ms);
        if (!have_sample) window_first = reading.tick_ms;
        metrics_set_window(window_first, reading.tick_ms);

        publish(&reading, state, duty, critical, SENSOR_OK);

        last_tick    = reading.tick_ms;
        have_sample  = 1;
    }

    /* Leave the actuator in a safe state on shutdown. */
    atomic_store(&g_fan_duty_pct, 0);
}
