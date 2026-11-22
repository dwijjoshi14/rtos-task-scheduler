/*
 * test_metrics.c — telemetry aggregation and export formats.
 */

#define _POSIX_C_SOURCE 200809L

#include "app_config.h"
#include "metrics.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static AppConfig_t metrics_config(void) {
    AppConfig_t cfg;
    app_config_defaults(&cfg);
    cfg.csv_path  = NULL;
    cfg.json_path = NULL;
    return cfg;
}

TEST_CASE(metrics, snapshot_starts_empty) {
    metrics_init();
    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);

    CHECK_INT(snapshot.samples, 0);
    CHECK_INT(snapshot.transitions, 0);
    CHECK_INT(snapshot.alerts, 0);
    CHECK_NEAR(snapshot.temp_sum_c, 0.0, 1e-9);
}

TEST_CASE(metrics, aggregates_temperature) {
    metrics_init();
    metrics_record_sample(20.0f, FAN_OFF, 200);
    metrics_record_sample(40.0f, FAN_LOW, 200);
    metrics_record_sample(90.0f, FAN_MAX, 200);

    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);

    CHECK_INT(snapshot.samples, 3);
    CHECK_NEAR(snapshot.temp_min_c, 20.0, 1e-6);
    CHECK_NEAR(snapshot.temp_max_c, 90.0, 1e-6);
    CHECK_NEAR(snapshot.temp_sum_c / snapshot.samples, 50.0, 1e-4);
    CHECK_INT(snapshot.critical_samples, 1);
}

TEST_CASE(metrics, buckets_cover_the_range) {
    metrics_init();
    metrics_record_sample(25.0f, FAN_OFF, 100);   /* <30   */
    metrics_record_sample(35.0f, FAN_LOW, 100);   /* 30-40 */
    metrics_record_sample(95.0f, FAN_MAX, 100);   /* 90+   */

    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);
    CHECK_INT(snapshot.temp_buckets[0], 1);
    CHECK_INT(snapshot.temp_buckets[1], 1);
    CHECK_INT(snapshot.temp_buckets[7], 1);
}

TEST_CASE(metrics, dwell_and_transitions) {
    metrics_init();
    metrics_record_transition(FAN_LOW);
    metrics_record_transition(FAN_HIGH);
    metrics_record_sample(45.0f, FAN_LOW, 500);
    metrics_record_sample(55.0f, FAN_HIGH, 300);

    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);

    CHECK_INT(snapshot.transitions, 2);
    CHECK_INT(snapshot.dwell_ms[FAN_LOW], 500);
    CHECK_INT(snapshot.dwell_ms[FAN_HIGH], 300);
    CHECK_INT(snapshot.entries[FAN_LOW], 1);
}

TEST_CASE(metrics, faults_drops_and_latency) {
    metrics_init();
    metrics_record_fault(SENSOR_STUCK);
    metrics_record_fault(SENSOR_OUT_OF_RANGE);
    metrics_record_fault(SENSOR_TIMEOUT);
    metrics_record_fault(SENSOR_OK);          /* no-op */
    metrics_record_drop();
    metrics_record_drop();
    metrics_record_latency(4);
    metrics_record_latency(1);
    metrics_record_queue_depth(2);
    metrics_record_queue_depth(1);            /* high-water keeps the peak */

    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);

    CHECK_INT(snapshot.sensor_faults, 2);
    CHECK_INT(snapshot.sensor_timeouts, 1);
    CHECK_INT(snapshot.dropped_readings, 2);
    CHECK_INT(snapshot.latency_samples, 2);
    CHECK_INT(snapshot.latency_sum_ms, 5);
    CHECK_INT(snapshot.latency_max_ms, 4);
    CHECK_INT(snapshot.queue_high_water, 2);
}

TEST_CASE(metrics, alerts_are_counted) {
    metrics_init();
    metrics_record_alert();
    metrics_record_alert();

    MetricsSnapshot_t snapshot;
    metrics_snapshot(&snapshot);
    CHECK_INT(snapshot.alerts, 2);
}

TEST_CASE(metrics, json_export_is_well_formed) {
    metrics_init();
    metrics_record_sample(46.0f, FAN_LOW, 200);
    metrics_record_sample(52.0f, FAN_HIGH, 200);
    metrics_record_transition(FAN_LOW);
    metrics_record_transition(FAN_HIGH);
    metrics_record_alert();
    metrics_set_window(200, 400);

    AppConfig_t cfg = metrics_config();
    PortDiagnostics_t port;
    memset(&port, 0, sizeof port);
    port.tasks_created = 3;

    FILE* file = tmpfile();
    CHECK(file != NULL);
    if (!file) return;

    CHECK_INT(metrics_write_json(file, &cfg, &port), 0);

    rewind(file);
    char buffer[4096];
    size_t read = fread(buffer, 1, sizeof buffer - 1, file);
    buffer[read] = '\0';
    fclose(file);

    CHECK(strstr(buffer, "\"schema\": 1") != NULL);
    CHECK(strstr(buffer, APP_NAME) != NULL);
    CHECK(strstr(buffer, "\"samples\": 2") != NULL);
    CHECK(strstr(buffer, "\"alerts\": 1") != NULL);
    CHECK(strstr(buffer, "\"duty_pct\"") == NULL);   /* only in CSV */

    int braces = 0;
    for (size_t i = 0; i < read; i++) {
        if (buffer[i] == '{') braces++;
        if (buffer[i] == '}') braces--;
    }
    CHECK_INT(braces, 0);
}

TEST_CASE(metrics, csv_export_roundtrip) {
    metrics_init();

    char path[128];
    snprintf(path, sizeof path, "/tmp/rtos_metrics_test_%d.csv", (int)getpid());

    CHECK_INT(metrics_csv_open(path), 0);
    metrics_csv_row(1, 200, 25.5f, 25.4f, FAN_OFF, 0, 0, SENSOR_OK);
    metrics_csv_row(2, 400, 81.0f, 79.5f, FAN_MAX, 100, 1, SENSOR_STUCK);
    CHECK_INT(metrics_csv_rows(), 2);
    metrics_csv_close();

    FILE* file = fopen(path, "r");
    CHECK(file != NULL);
    if (!file) return;

    char line[256];
    CHECK(fgets(line, sizeof line, file) != NULL);
    CHECK(strstr(line, "sequence,elapsed_ms,raw_c,filtered_c,fan_state") != NULL);

    CHECK(fgets(line, sizeof line, file) != NULL);
    CHECK(strstr(line, "1,200,25.500,25.400,OFF,0,0,OK") != NULL);

    CHECK(fgets(line, sizeof line, file) != NULL);
    CHECK(strstr(line, "2,400,81.000,79.500,MAX,100,1,STUCK") != NULL);

    CHECK(fgets(line, sizeof line, file) == NULL);   /* exactly two rows */
    fclose(file);
    remove(path);
}

TEST_CASE(metrics, csv_open_rejects_bad_path) {
    CHECK_INT(metrics_csv_open(NULL), -1);
    CHECK_INT(metrics_csv_open("/nonexistent-directory/out.csv"), -1);
    metrics_csv_close();   /* safe when nothing is open */
}

TEST_CASE(metrics, report_renders_without_a_run) {
    metrics_init();
    AppConfig_t cfg = metrics_config();
    PortDiagnostics_t port;
    memset(&port, 0, sizeof port);

    FILE* file = tmpfile();
    CHECK(file != NULL);
    if (!file) return;

    metrics_print_report(file, &cfg, &port);
    rewind(file);

    char buffer[4096];
    size_t read = fread(buffer, 1, sizeof buffer - 1, file);
    buffer[read] = '\0';
    fclose(file);

    CHECK(strstr(buffer, "run summary") != NULL);
    CHECK(strstr(buffer, "scheduler") != NULL);
}
