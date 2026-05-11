#define _POSIX_C_SOURCE 200809L
#include "sensor_task.h"
#include "FreeRTOS.h"
#include "tasks_common.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

QueueHandle_t xSensorQueue = NULL;

/*
 * Simulates TMP36 temperature readings.
 * Produces a realistic ramp: starts at ~20 C, climbs to ~90 C,
 * with small random noise — mirrors a device heating under load.
 *
 * On STM32 hardware, replace with:
 *   uint32_t raw = HAL_ADC_GetValue(&hadc1);
 *   float voltage = (raw / 4095.0f) * 3.3f;
 *   return (voltage - 0.5f) * 100.0f;
 */
static float simulate_temperature(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double elapsed = ts.tv_sec + ts.tv_nsec / 1e9;

    /* Slow sinusoidal ramp 20 → 90 C over ~40 seconds */
    float base = 20.0f + 35.0f * (1.0f - cosf((float)(elapsed / 40.0) * 3.14159f));

    /* Small noise +/- 1.5 C to simulate ADC jitter */
    float noise = ((float)(ts.tv_nsec % 300) / 100.0f) - 1.5f;
    return base + noise;
}

void SensorTask(void* params) {
    (void)params;
    TickType_t xLastWakeTime;
    xLastWakeTime = 0;

    for (;;) {
        if (g_sim_stop) return;
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SENSOR_PERIOD_MS));

        SensorReading_t reading;
        reading.temperature_c = simulate_temperature();

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        reading.tick_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

        /* Push reading onto queue. Drop if full (non-blocking send with 10ms timeout). */
        if (xQueueSend(xSensorQueue, &reading, pdMS_TO_TICKS(10)) != pdPASS) {
            printf("[SensorTask] WARNING: queue full — reading dropped\n");
        }
    }
}
