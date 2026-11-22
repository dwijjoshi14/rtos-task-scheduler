/*
 * metrics.c — thread-safe run telemetry, reports and exporters.
 *
 * Aggregation happens under one mutex so the report is a consistent snapshot
 * even if it were printed while tasks were still running.
 */

#define _POSIX_C_SOURCE 200809L

#include "metrics.h"

#include "control_logic.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t   s_metrics_mutex = PTHREAD_MUTEX_INITIALIZER;
static MetricsSnapshot_t s_metrics;
static FILE*             s_csv = NULL;
static uint32_t          s_csv_rows = 0;

void metrics_init(void) {
    pthread_mutex_lock(&s_metrics_mutex);
    memset(&s_metrics, 0, sizeof s_metrics);
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_reset(void) {
    metrics_init();
}

static uint32_t temperature_bucket(float temp_c) {
    if (temp_c < 30.0f) return 0;
    int index = 1 + (int)((temp_c - 30.0f) / 10.0f);
    if (index > 7) index = 7;
    return (uint32_t)index;
}

void metrics_record_sample(float temp_c, FanState_t state, uint32_t dt_ms) {
    pthread_mutex_lock(&s_metrics_mutex);

    if (s_metrics.samples == 0) {
        s_metrics.temp_min_c = temp_c;
        s_metrics.temp_max_c = temp_c;
    } else {
        if (temp_c < s_metrics.temp_min_c) s_metrics.temp_min_c = temp_c;
        if (temp_c > s_metrics.temp_max_c) s_metrics.temp_max_c = temp_c;
    }
    s_metrics.samples++;
    s_metrics.temp_sum_c += (double)temp_c;
    s_metrics.temp_buckets[temperature_bucket(temp_c)]++;

    if (state < FAN_STATE_COUNT) s_metrics.dwell_ms[state] += dt_ms;
    if (state == FAN_MAX)        s_metrics.critical_samples++;

    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_transition(FanState_t state) {
    pthread_mutex_lock(&s_metrics_mutex);
    s_metrics.transitions++;
    if (state < FAN_STATE_COUNT) s_metrics.entries[state]++;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_alert(void) {
    pthread_mutex_lock(&s_metrics_mutex);
    s_metrics.alerts++;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_fault(SensorFault_t fault) {
    if (fault == SENSOR_OK) return;
    pthread_mutex_lock(&s_metrics_mutex);
    if (fault == SENSOR_TIMEOUT) s_metrics.sensor_timeouts++;
    else                          s_metrics.sensor_faults++;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_drop(void) {
    pthread_mutex_lock(&s_metrics_mutex);
    s_metrics.dropped_readings++;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_queue_depth(UBaseType_t depth) {
    pthread_mutex_lock(&s_metrics_mutex);
    if ((uint32_t)depth > s_metrics.queue_high_water) {
        s_metrics.queue_high_water = (uint32_t)depth;
    }
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_record_latency(uint32_t latency_ms) {
    pthread_mutex_lock(&s_metrics_mutex);
    s_metrics.latency_samples++;
    s_metrics.latency_sum_ms += latency_ms;
    if (latency_ms > s_metrics.latency_max_ms) s_metrics.latency_max_ms = latency_ms;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_set_window(uint32_t first_tick_ms, uint32_t last_tick_ms) {
    pthread_mutex_lock(&s_metrics_mutex);
    s_metrics.first_tick_ms = first_tick_ms;
    s_metrics.last_tick_ms  = last_tick_ms;
    pthread_mutex_unlock(&s_metrics_mutex);
}

void metrics_snapshot(MetricsSnapshot_t* out) {
    if (!out) return;
    pthread_mutex_lock(&s_metrics_mutex);
    *out = s_metrics;
    pthread_mutex_unlock(&s_metrics_mutex);
}

/* ── Human-readable report ────────────────────────────────── */

static const char* const k_bucket_labels[8] = {
    "<30", "30-40", "40-50", "50-60", "60-70", "70-80", "80-90", "90+"
};

static double percent(uint32_t part, uint32_t whole) {
    if (whole == 0) return 0.0;
    return 100.0 * (double)part / (double)whole;
}

static void print_histogram(FILE* out, const MetricsSnapshot_t* m) {
    uint32_t peak = 0;
    for (int i = 0; i < 8; i++) {
        if (m->temp_buckets[i] > peak) peak = m->temp_buckets[i];
    }
    if (peak == 0) return;

    fprintf(out, "  temperature distribution\n");
    for (int i = 0; i < 8; i++) {
        uint32_t count = m->temp_buckets[i];
        int bars = (int)((double)count * 32.0 / (double)peak + 0.5);
        char bar[33];
        for (int b = 0; b < bars && b < 32; b++) bar[b] = '#';
        bar[bars < 32 ? bars : 32] = '\0';
        fprintf(out, "   %-5s C  %-32s %3.0f%%\n", k_bucket_labels[i], bar,
                percent(count, m->samples));
    }
}

void metrics_print_report(FILE* out, const AppConfig_t* cfg,
                          const PortDiagnostics_t* port) {
    if (!out) return;

    MetricsSnapshot_t m;
    metrics_snapshot(&m);

    uint32_t total_dwell = 0;
    for (int i = 0; i < FAN_STATE_COUNT; i++) total_dwell += m.dwell_ms[i];

    double mean_c = m.samples ? m.temp_sum_c / (double)m.samples : 0.0;
    double window_s = (double)(m.last_tick_ms - m.first_tick_ms) / 1000.0;
    if (window_s < 0.0) window_s = 0.0;
    double mean_latency = m.latency_samples
        ? (double)m.latency_sum_ms / (double)m.latency_samples
        : 0.0;

    fprintf(out, "\n");
    fprintf(out, "  run summary\n");
    fprintf(out, "  ───────────\n");
    fprintf(out, "  window          %6.2f s  (%u samples, %u ms cadence)\n",
            window_s, m.samples, cfg->sensor_period_ms);
    fprintf(out, "  temperature     min %5.1f C · mean %5.1f C · max %5.1f C\n",
            (double)m.temp_min_c, mean_c, (double)m.temp_max_c);
    fprintf(out, "  fan             %u transitions · %u alert(s) · %.0f%% of samples critical\n",
            m.transitions, m.alerts, percent(m.critical_samples, m.samples));
    fprintf(out, "  dwell           ");
    for (int i = 0; i < FAN_STATE_COUNT; i++) {
        fprintf(out, "%s %.0f%%%s", fan_state_name((FanState_t)i),
                percent(m.dwell_ms[i], total_dwell),
                i + 1 < FAN_STATE_COUNT ? " · " : "\n");
    }
    fprintf(out, "  health          %u fault(s) · %u timeout(s) · %u reading(s) dropped · queue high-water %u/%d\n",
            m.sensor_faults, m.sensor_timeouts, m.dropped_readings,
            m.queue_high_water, SENSOR_QUEUE_DEPTH);
    fprintf(out, "  latency         mean %.1f ms · max %u ms (sample → display)\n",
            mean_latency, m.latency_max_ms);

    if (port) {
        fprintf(out, "  scheduler       %u deadline overrun(s) · worst %u ms · %u mutex violation(s) · %u task(s)\n",
                port->deadline_overruns, port->max_overrun_ms,
                port->mutex_violations, port->tasks_created);
    }
    fprintf(out, "\n");
    print_histogram(out, &m);
    fprintf(out, "\n");
}

/* ── JSON export ──────────────────────────────────────────── */

static void json_string(FILE* out, const char* text) {
    if (!text) {
        fputs("null", out);
        return;
    }
    fputc('"', out);
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:
                if (*p < 0x20) fprintf(out, "\\u%04x", *p);
                else           fputc((char)*p, out);
        }
    }
    fputc('"', out);
}

int metrics_write_json(FILE* out, const AppConfig_t* cfg,
                       const PortDiagnostics_t* port) {
    if (!out || !cfg) return -1;

    MetricsSnapshot_t m;
    metrics_snapshot(&m);

    uint32_t total_dwell = 0;
    for (int i = 0; i < FAN_STATE_COUNT; i++) total_dwell += m.dwell_ms[i];

    double mean_c = m.samples ? m.temp_sum_c / (double)m.samples : 0.0;
    double window_s = (double)(m.last_tick_ms - m.first_tick_ms) / 1000.0;

    int failed = 0;
    failed |= fprintf(out,
        "{\n"
        "  \"schema\": 1,\n"
        "  \"app\": {\"name\": \"%s\", \"version\": \"%s\"},\n"
        "  \"config\": {\n"
        "    \"sensor_period_ms\": %u,\n"
        "    \"display_period_ms\": %u,\n"
        "    \"watchdog_ms\": %u,\n"
        "    \"run_duration_ms\": %u,\n"
        "    \"seed\": %u,\n"
        "    \"thresholds_c\": {\"low\": %.3f, \"high\": %.3f, \"critical\": %.3f, \"hysteresis\": %.3f},\n"
        "    \"ema_alpha\": %.4f,\n"
        "    \"noise_amplitude_c\": %.4f,\n"
        "    \"ambient_c\": %.3f,\n"
        "    \"heater_power_w\": %.3f,\n"
        "    \"heat_capacity_j_per_k\": %.3f,\n"
        "    \"conductance_w_per_k\": %.4f,\n"
        "    \"fan_gain_w_per_k\": %.4f,\n"
        "    \"burst_multiplier\": %.3f\n"
        "  },\n"
        "  \"metrics\": {\n"
        "    \"window_s\": %.3f,\n"
        "    \"samples\": %u,\n"
        "    \"transitions\": %u,\n"
        "    \"alerts\": %u,\n"
        "    \"critical_samples\": %u,\n"
        "    \"sensor_timeouts\": %u,\n"
        "    \"sensor_faults\": %u,\n"
        "    \"dropped_readings\": %u,\n"
        "    \"queue_high_water\": %u,\n"
        "    \"temperature_c\": {\"min\": %.3f, \"mean\": %.3f, \"max\": %.3f},\n"
        "    \"latency_ms\": {\"mean\": %.3f, \"max\": %u},\n"
        "    \"dwell_pct\": {\"off\": %.2f, \"low\": %.2f, \"high\": %.2f, \"max\": %.2f}\n"
        "  },\n"
        "  \"diagnostics\": {\n"
        "    \"tasks_created\": %u,\n"
        "    \"deadline_overruns\": %u,\n"
        "    \"max_overrun_ms\": %u,\n"
        "    \"mutex_violations\": %u\n"
        "  },\n"
        "  \"output\": {\"csv\": ",
        APP_NAME, APP_VERSION,
        cfg->sensor_period_ms, cfg->display_period_ms, cfg->watchdog_ms,
        cfg->run_duration_ms, cfg->seed,
        (double)cfg->thresholds.low_c, (double)cfg->thresholds.high_c,
        (double)cfg->thresholds.critical_c,
        (double)cfg->thresholds.hysteresis_c,
        (double)cfg->ema_alpha, (double)cfg->noise_amplitude_c,
        (double)cfg->ambient_c, (double)cfg->heater_power_w,
        (double)cfg->heat_capacity_j_per_k, (double)cfg->conductance_w_per_k,
        (double)cfg->fan_gain_w_per_k, (double)cfg->burst_multiplier,
        window_s, m.samples, m.transitions, m.alerts, m.critical_samples,
        m.sensor_timeouts, m.sensor_faults, m.dropped_readings,
        m.queue_high_water,
        (double)m.temp_min_c, mean_c, (double)m.temp_max_c,
        m.latency_samples ? (double)m.latency_sum_ms / (double)m.latency_samples : 0.0,
        m.latency_max_ms,
        percent(m.dwell_ms[FAN_OFF], total_dwell),
        percent(m.dwell_ms[FAN_LOW], total_dwell),
        percent(m.dwell_ms[FAN_HIGH], total_dwell),
        percent(m.dwell_ms[FAN_MAX], total_dwell),
        port ? port->tasks_created : 0u,
        port ? port->deadline_overruns : 0u,
        port ? port->max_overrun_ms : 0u,
        port ? port->mutex_violations : 0u);

    if (failed < 0) return -1;

    json_string(out, cfg->csv_path);
    fputs(", \"json\": ", out);
    json_string(out, cfg->json_path);
    fputs("}\n}\n", out);

    return ferror(out) ? -1 : 0;
}

/* ── CSV export ──────────────────────────────────────────── */

int metrics_csv_open(const char* path) {
    if (!path) return -1;
    metrics_csv_close();
    s_csv = fopen(path, "w");
    if (!s_csv) return -1;
    s_csv_rows = 0;
    if (fprintf(s_csv,
                "sequence,elapsed_ms,raw_c,filtered_c,fan_state,duty_pct,critical,fault\n") < 0) {
        metrics_csv_close();
        return -1;
    }
    return 0;
}

void metrics_csv_row(uint32_t sequence, uint32_t elapsed_ms, float raw_c,
                     float filtered_c, FanState_t state, uint8_t duty_pct,
                     int critical, SensorFault_t fault) {
    if (!s_csv) return;
    fprintf(s_csv, "%u,%u,%.3f,%.3f,%s,%u,%d,%s\n",
            sequence, elapsed_ms, (double)raw_c, (double)filtered_c,
            fan_state_name(state), duty_pct, critical ? 1 : 0,
            sensor_fault_name(fault));
    s_csv_rows++;
}

void metrics_csv_close(void) {
    if (!s_csv) return;
    fclose(s_csv);
    s_csv = NULL;
    s_csv_rows = 0;
}

uint32_t metrics_csv_rows(void) {
    return s_csv_rows;
}
