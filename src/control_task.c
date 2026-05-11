#include "control_task.h"
#include "sensor_task.h"
#include "FreeRTOS.h"
#include "tasks_common.h"
#include <stdio.h>

QueueHandle_t     xDisplayQueue  = NULL;
SemaphoreHandle_t xAlertSemaphore = NULL;
SemaphoreHandle_t xConsoleMutex   = NULL;

/*
 * Maps a temperature reading to a FanState.
 * Mirrors the threshold logic from the Heat Management System,
 * extended with a CRITICAL band above 75 C.
 */
static FanState_t evaluate_fan_state(float temp_c) {
    if (temp_c > TEMP_CRITICAL_THRESHOLD) return FAN_MAX;
    if (temp_c > TEMP_HIGH_THRESHOLD)     return FAN_HIGH;
    if (temp_c > TEMP_LOW_THRESHOLD)      return FAN_LOW;
    return FAN_OFF;
}

/*
 * Maps FanState to a PWM duty cycle percentage.
 * On STM32: duty_pct maps to __HAL_TIM_SET_COMPARE(htim, channel, duty_pct * PERIOD / 100)
 */
static int fan_state_to_duty(FanState_t state) {
    switch (state) {
        case FAN_OFF:  return 0;
        case FAN_LOW:  return 40;
        case FAN_HIGH: return 70;
        case FAN_MAX:  return 100;
        default:       return 0;
    }
}

static const char* fan_state_name(FanState_t state) {
    switch (state) {
        case FAN_OFF:  return "OFF";
        case FAN_LOW:  return "LOW";
        case FAN_HIGH: return "HIGH";
        case FAN_MAX:  return "MAX";
        default:       return "UNKNOWN";
    }
}

void ControlTask(void* params) {
    (void)params;
    SensorReading_t reading;
    FanState_t      prev_state = FAN_OFF;
    int             prev_critical = 0;

    for (;;) {
        if (g_sim_stop) return;
        /* Block until a sensor reading is available */
        if (xQueueReceive(xSensorQueue, &reading, portMAX_DELAY) != pdPASS)
            continue;

        FanState_t new_state = evaluate_fan_state(reading.temperature_c);
        int        is_critical = (new_state == FAN_MAX);

        /* Log state transitions (mutex protects console from interleaving) */
        if (new_state != prev_state) {
            xSemaphoreTake(xConsoleMutex, portMAX_DELAY);
            printf("[ControlTask] Fan state: %s → %s  (%.1f C, PWM %d%%)\n",
                   fan_state_name(prev_state),
                   fan_state_name(new_state),
                   reading.temperature_c,
                   fan_state_to_duty(new_state));
            xSemaphoreGive(xConsoleMutex);
            prev_state = new_state;
        }

        /* Signal alert semaphore on rising edge of critical condition */
        if (is_critical && !prev_critical) {
            xSemaphoreGive(xAlertSemaphore);
        }
        prev_critical = is_critical;

        /* Build display message and push to display queue */
        DisplayMsg_t msg;
        msg.temperature_c = reading.temperature_c;
        msg.fan_state     = new_state;
        msg.tick_ms       = reading.tick_ms;
        msg.is_critical   = (uint8_t)is_critical;

        xQueueSend(xDisplayQueue, &msg, pdMS_TO_TICKS(10));
    }
}
