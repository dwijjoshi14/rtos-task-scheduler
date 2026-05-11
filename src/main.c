#include "FreeRTOS.h"
#include "tasks_common.h"
#include "sensor_task.h"
#include "control_task.h"
#include "display_task.h"
#include <stdio.h>
#include <unistd.h>

volatile int g_sim_stop = 0;

/*
 * main.c — RTOS Task Scheduler: Temperature Control System
 * ---------------------------------------------------------
 * Creates all shared kernel objects (queues, semaphores, mutex),
 * then spawns three FreeRTOS tasks before handing control to
 * the scheduler.
 *
 * Task data flow:
 *
 *   SensorTask (P3) ──[xSensorQueue]──► ControlTask (P4)
 *                                              │
 *                                    [xDisplayQueue] + [xAlertSemaphore]
 *                                              │               │
 *                                              └───────────────┘
 *                                                      ▼
 *                                              DisplayTask (P2)
 *
 * All tasks share xConsoleMutex to prevent interleaved printf output.
 */

int main(void) {
    printf("\n");
    printf("  ╔═══════════════════════════════════════════════╗\n");
    printf("  ║   FreeRTOS Task Scheduler — POSIX Simulation  ║\n");
    printf("  ║   Temperature Monitoring & Fan Control        ║\n");
    printf("  ║   Dwij Joshi · github.com/dwijjoshi14         ║\n");
    printf("  ╚═══════════════════════════════════════════════╝\n\n");

    /* ── Create kernel objects ────────────────────────── */
    xSensorQueue    = xQueueCreate(SENSOR_QUEUE_DEPTH,  sizeof(SensorReading_t));
    xDisplayQueue   = xQueueCreate(DISPLAY_QUEUE_DEPTH, sizeof(DisplayMsg_t));
    xAlertSemaphore = xSemaphoreCreateBinary();
    xConsoleMutex   = xSemaphoreCreateMutex();

    if (!xSensorQueue || !xDisplayQueue || !xAlertSemaphore || !xConsoleMutex) {
        printf("ERROR: Failed to create kernel objects. Aborting.\n");
        return 1;
    }

    printf("[main] Kernel objects created:\n");
    printf("       xSensorQueue    (depth %d, item %zu B)\n",
           SENSOR_QUEUE_DEPTH, sizeof(SensorReading_t));
    printf("       xDisplayQueue   (depth %d, item %zu B)\n",
           DISPLAY_QUEUE_DEPTH, sizeof(DisplayMsg_t));
    printf("       xAlertSemaphore (binary)\n");
    printf("       xConsoleMutex   (mutex)\n\n");

    /* ── Create tasks ─────────────────────────────────── */
    TaskHandle_t hSensor = NULL, hControl = NULL, hDisplay = NULL;

    xTaskCreate(SensorTask,  "SensorTask",  SENSOR_STACK_SIZE,
                NULL, SENSOR_PRIORITY,  &hSensor);
    xTaskCreate(ControlTask, "ControlTask", CONTROL_STACK_SIZE,
                NULL, CONTROL_PRIORITY, &hControl);
    xTaskCreate(DisplayTask, "DisplayTask", DISPLAY_STACK_SIZE,
                NULL, DISPLAY_PRIORITY, &hDisplay);

    printf("[main] Tasks created:\n");
    printf("       %-14s  priority=%u\n", "SensorTask",  SENSOR_PRIORITY);
    printf("       %-14s  priority=%u  (highest)\n", "ControlTask", CONTROL_PRIORITY);
    printf("       %-14s  priority=%u\n\n", "DisplayTask", DISPLAY_PRIORITY);

    printf("[main] Starting scheduler...\n\n");
    fflush(stdout);
    usleep(50000); /* 50ms — let main finish printing before tasks start */

    /* Hand control to the RTOS scheduler */
    vTaskStartScheduler();

    /* Never reached on hardware; reached here only when simulation ends */
    printf("\n[main] Scheduler returned. All tasks complete.\n");
    return 0;
}
