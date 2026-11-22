/*
 * main.c — boot sequence for the FreeRTOS temperature control simulation.
 *
 *   parse configuration
 *   install SIGINT handler      (Ctrl+C still produces a full report)
 *   create kernel objects       (queues, alert semaphore, console mutex)
 *   create tasks                (sensor, control, display)
 *   vTaskStartScheduler()       (blocks until every task has exited)
 *   emit reports                (summary, JSON, CSV)
 *   delete kernel objects
 *
 * The AppConfig_t is static and lives for the whole process: tasks receive a
 * pointer to it as their pvParameters, exactly as they would receive a
 * driver handle on target hardware.
 */

#define _POSIX_C_SOURCE 200809L

#include "app_config.h"
#include "control_task.h"
#include "display_task.h"
#include "metrics.h"
#include "sensor_task.h"
#include "tasks_common.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

static AppConfig_t g_config;

static void handle_sigint(int signum) {
    (void)signum;
    g_sigint = 1;   /* only an async-signal-safe store in a handler */
}

static int install_signal_handlers(void) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;   /* interrupt sleeps so shutdown is immediate */
    return sigaction(SIGINT, &action, NULL);
}

static void print_banner(void) {
    printf("\n"
           "  +-----------------------------------------------------------+\n"
           "  |  RTOS Task Scheduler — FreeRTOS temperature control loop  |\n"
           "  |  %-8s · 3 tasks · 2 queues · semaphore · mutex       |\n"
           "  +-----------------------------------------------------------+\n\n",
           "v" APP_VERSION);
}

int main(int argc, char** argv) {
    app_config_defaults(&g_config);

    const int parsed = app_config_parse(&g_config, argc, argv);
    if (parsed == APP_CONFIG_EXIT)   return 0;
    if (parsed != APP_CONFIG_OK)     return 1;

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "warning: could not install SIGINT handler\n");
    }

    /* Line buffering keeps interleaved task output readable when redirected. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    print_banner();
    printf("  configuration\n");
    app_config_print_summary(stdout, &g_config);
    printf("\n");

    /* ── Kernel objects ─────────────────────────────────────── */
    xSensorQueue    = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(SensorReading_t));
    xDisplayQueue   = xQueueCreate(DISPLAY_QUEUE_DEPTH, sizeof(DisplayMsg_t));
    xAlertSemaphore = xSemaphoreCreateBinary();
    xConsoleMutex   = xSemaphoreCreateMutex();

    if (!xSensorQueue || !xDisplayQueue || !xAlertSemaphore || !xConsoleMutex) {
        fprintf(stderr, "fatal: failed to create kernel objects\n");
        return 1;
    }

    printf("[main] kernel objects\n");
    printf("       xSensorQueue    depth %d · %zu B/item\n",
           SENSOR_QUEUE_DEPTH, sizeof(SensorReading_t));
    printf("       xDisplayQueue   depth %d · %zu B/item\n",
           DISPLAY_QUEUE_DEPTH, sizeof(DisplayMsg_t));
    printf("       xAlertSemaphore binary\n");
    printf("       xConsoleMutex   mutex\n\n");

    metrics_init();

    /* The run clock must be published *before* the tasks exist: every task
     * measures elapsed time against it from its first iteration. */
    g_sim_start_ms = xTaskGetTickCount();

    if (g_config.csv_path && metrics_csv_open(g_config.csv_path) != 0) {
        fprintf(stderr, "warning: cannot write CSV to '%s', continuing\n",
                g_config.csv_path);
        g_config.csv_path = NULL;
    }

    /* ── Tasks ──────────────────────────────────────────────── */
    TaskHandle_t h_sensor = NULL, h_control = NULL, h_display = NULL;

    if (xTaskCreate(SensorTask, "SensorTask", SENSOR_STACK_SIZE,
                    &g_config, SENSOR_PRIORITY, &h_sensor) != pdPASS ||
        xTaskCreate(ControlTask, "ControlTask", CONTROL_STACK_SIZE,
                    &g_config, CONTROL_PRIORITY, &h_control) != pdPASS ||
        xTaskCreate(DisplayTask, "DisplayTask", DISPLAY_STACK_SIZE,
                    &g_config, DISPLAY_PRIORITY, &h_display) != pdPASS) {
        fprintf(stderr, "fatal: failed to create tasks\n");
        return 1;
    }

    printf("[main] tasks\n");
    printf("       %-12s priority %u  period %u ms\n", "SensorTask",
           SENSOR_PRIORITY, g_config.sensor_period_ms);
    printf("       %-12s priority %u  event driven (watchdog %u ms)\n",
           "ControlTask", CONTROL_PRIORITY, g_config.watchdog_ms);
    printf("       %-12s priority %u  period %u ms\n\n", "DisplayTask",
           DISPLAY_PRIORITY, g_config.display_period_ms);

    printf("[main] scheduler running\n\n");
    fflush(stdout);

    vTaskStartScheduler();

    /* ── Teardown and reporting ─────────────────────────────── */
    PortDiagnostics_t port;
    memset(&port, 0, sizeof port);
    vPortGetDiagnostics(&port);

    if (g_sigint) {
        printf("\n[main] interrupted by SIGINT after %.2f s\n",
               (double)sim_elapsed_ms() / 1000.0);
    }

    metrics_print_report(stdout, &g_config, &port);

    if (g_config.json_path) {
        FILE* json = fopen(g_config.json_path, "w");
        if (!json) {
            fprintf(stderr, "warning: cannot write JSON to '%s'\n",
                    g_config.json_path);
        } else {
            const int ok = metrics_write_json(json, &g_config, &port);
            fclose(json);
            if (ok == 0) printf("  telemetry  json → %s\n", g_config.json_path);
            else         fprintf(stderr, "warning: failed writing '%s'\n",
                                 g_config.json_path);
        }
    }

    const uint32_t csv_rows = metrics_csv_rows();
    metrics_csv_close();
    if (g_config.csv_path) {
        printf("  telemetry  csv  → %s (%u rows)\n", g_config.csv_path, csv_rows);
    }
    printf("\n");

    vQueueDelete(xSensorQueue);
    vQueueDelete(xDisplayQueue);
    vSemaphoreDelete(xAlertSemaphore);
    vSemaphoreDelete(xConsoleMutex);
    xSensorQueue = xDisplayQueue = NULL;
    xAlertSemaphore = xConsoleMutex = NULL;

    return 0;
}
