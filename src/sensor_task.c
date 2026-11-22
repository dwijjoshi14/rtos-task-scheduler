/*
 * sensor_task.c — plant sampling, noise injection and filtering.
 */

#define _POSIX_C_SOURCE 200809L

#include "sensor_task.h"

#include "metrics.h"
#include "signal_filter.h"
#include "thermal_model.h"

#include <stdatomic.h>
#include <stdio.h>

QueueHandle_t xSensorQueue = NULL;

/*
 * xorshift32: tiny, deterministic and dependency-free. A seeded generator
 * means a failing run can be reproduced exactly (--seed), which matters for
 * characterising a controller.
 */
static float next_noise(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 0x9E3779B9u;   /* xorshift locks up at zero */
    *state = x;
    /* 24 significant bits mapped to [-1, 1). */
    return (float)(x & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

void SensorTask(void* params) {
    AppConfig_t* cfg = (AppConfig_t*)params;
    const TickType_t period = pdMS_TO_TICKS(cfg->sensor_period_ms);

    ThermalPlant_t plant;
    thermal_plant_init(&plant, cfg);

    EmaFilter_t filter;
    ema_init(&filter, cfg->ema_alpha);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   noise_state = cfg->seed ? cfg->seed : 1u;
    uint32_t   sequence = 0;
    uint32_t   last_tick = xTaskGetTickCount();
    float      last_raw  = plant.temp_c;

    for (;;) {
        vTaskDelayUntil(&last_wake, period);

        if (sim_should_stop() || sim_deadline_reached(cfg->run_duration_ms)) {
            sim_request_stop();
            break;
        }

        const TickType_t now = xTaskGetTickCount();
        const double     dt_s = (double)(now - last_tick) / 1000.0;
        last_tick = now;

        /* The plant reacts to the duty the controller commanded last period. */
        const int   duty = atomic_load(&g_fan_duty_pct);
        const float load = thermal_plant_load(&plant,
                                             (double)sim_elapsed_ms() / 1000.0);
        const float plant_c = thermal_plant_step(&plant, (uint8_t)duty, load, dt_s);

        float raw = plant_c + cfg->noise_amplitude_c * next_noise(&noise_state);

        sequence++;

        if (cfg->stall_at_sample > 0 && sequence >= cfg->stall_at_sample) {
            /* Fault injection: the producer goes quiet. ControlTask's watchdog
             * is what has to notice, so nothing may be published here. */
            if (sim_deadline_reached(cfg->run_duration_ms)) {
                sim_request_stop();
                break;
            }
            continue;
        }

        if (cfg->stuck_at_sample > 0 && sequence >= cfg->stuck_at_sample) {
            /* Fault injection: a latched ADC keeps returning its last value,
             * which is exactly what the stuck-sensor detector must catch. */
            raw = last_raw;
        }
        last_raw = raw;

        SensorReading_t reading;
        reading.raw_c         = raw;
        reading.temperature_c = ema_update(&filter, raw);
        reading.tick_ms       = (uint32_t)now;
        reading.sequence      = sequence;

        if (xQueueSend(xSensorQueue, &reading, pdMS_TO_TICKS(10)) != pdPASS) {
            metrics_record_drop();
        } else {
            /* Backlog seen by the producer: the control task's headroom. */
            metrics_record_queue_depth(uxQueueMessagesWaiting(xSensorQueue));
        }
    }
}
