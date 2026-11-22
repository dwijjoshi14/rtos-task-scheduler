#pragma once
/*
 * metrics.h — run telemetry.
 *
 * Every task feeds the same aggregator under one mutex, so the summary is a
 * consistent snapshot of the run rather than a best-effort print. Metrics are
 * emitted three ways: a human report, a per-sample CSV for plotting, and a JSON
 * summary for CI to assert on.
 */

#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "app_config.h"
#include "tasks_common.h"

typedef struct {
    uint32_t samples;
    uint32_t transitions;
    uint32_t alerts;
    uint32_t critical_samples;
    uint32_t sensor_timeouts;
    uint32_t sensor_faults;        /* stuck / out-of-range readings */
    uint32_t dropped_readings;
    uint32_t queue_high_water;

    float    temp_min_c;
    float    temp_max_c;
    double   temp_sum_c;
    uint32_t temp_buckets[8];      /* <30, 30-40, ... , 90+ */

    uint32_t dwell_ms[FAN_STATE_COUNT];
    uint32_t entries[FAN_STATE_COUNT];

    uint32_t latency_samples;
    uint64_t latency_sum_ms;
    uint32_t latency_max_ms;

    uint32_t first_tick_ms;
    uint32_t last_tick_ms;
} MetricsSnapshot_t;

void metrics_init(void);
void metrics_reset(void);

void metrics_record_sample(float temp_c, FanState_t state, uint32_t dt_ms);
void metrics_record_transition(FanState_t state);
void metrics_record_alert(void);
void metrics_record_fault(SensorFault_t fault);
void metrics_record_drop(void);
void metrics_record_queue_depth(UBaseType_t depth);
void metrics_record_latency(uint32_t latency_ms);

void metrics_snapshot(MetricsSnapshot_t* out);

/* Records the first/last sample timestamps so the report can state a window. */
void metrics_set_window(uint32_t first_tick_ms, uint32_t last_tick_ms);

/* Human-readable report (ASCII only; safe for CI logs). */
void metrics_print_report(FILE* out, const AppConfig_t* cfg,
                          const PortDiagnostics_t* port);

/* Machine-readable summary. Returns 0 on success, -1 on I/O error. */
int  metrics_write_json(FILE* out, const AppConfig_t* cfg,
                        const PortDiagnostics_t* port);

/* Streaming per-sample CSV. */
int  metrics_csv_open(const char* path);
void metrics_csv_row(uint32_t sequence, uint32_t elapsed_ms, float raw_c,
                     float filtered_c, FanState_t state, uint8_t duty_pct,
                     int critical, SensorFault_t fault);
void metrics_csv_close(void);
uint32_t metrics_csv_rows(void);
