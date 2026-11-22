#pragma once
/*
 * FreeRTOS.h — POSIX simulation layer (API-compatible subset)
 * ------------------------------------------------------------
 * Implements the parts of the FreeRTOS kernel API this project uses on top of
 * pthreads, so application code can be compiled and run on a desktop while
 * staying source-compatible with the real kernel on target hardware.
 *
 * Implemented:
 *   Tasks       xTaskCreate, vTaskDelay, vTaskDelayUntil, vTaskStartScheduler,
 *               xTaskGetTickCount, pcTaskGetName, uxTaskGetNumberOfTasks
 *   Queues      xQueueCreate, vQueueDelete, xQueueSend, xQueueReceive,
 *               xQueuePeek, xQueueReset, uxQueueMessagesWaiting,
 *               uxQueueSpacesAvailable
 *   Semaphores  xSemaphoreCreateBinary, xSemaphoreCreateMutex,
 *               xSemaphoreCreateCounting, vSemaphoreDelete, xSemaphoreGive,
 *               xSemaphoreTake
 *   Heap        pvPortMalloc, vPortFree
 *
 * Deliberate deviations from the kernel, all documented in docs/PORTING.md:
 *   - Ticks are milliseconds (`configTICK_RATE_HZ` is 1000).
 *   - vTaskStartScheduler() joins every created task and returns instead of
 *     never returning; it is the end of the simulated run, not a reboot.
 *   - Mutexes are not recursive, like the kernel's, but a nested take returns
 *     pdFAIL and is counted as a violation instead of asserting.
 *
 * The port also exposes read-only diagnostics (deadline overruns, mutex
 * violations, timed-out waits, live allocations). They cost nothing on target
 * hardware and make scheduling behaviour observable from the test suite.
 */

#include <stdint.h>
#include <stddef.h>

/* ── FreeRTOS type aliases ─────────────────────────────────── */
typedef void*         TaskHandle_t;
typedef void*         QueueHandle_t;
typedef void*         SemaphoreHandle_t;
typedef uint32_t      TickType_t;
typedef int           BaseType_t;
typedef unsigned int  UBaseType_t;
typedef uint32_t      StackType_t;

#define pdTRUE            ((BaseType_t) 1)
#define pdFALSE           ((BaseType_t) 0)
#define pdPASS            ((BaseType_t) 1)
#define pdFAIL            ((BaseType_t) 0)
#define portMAX_DELAY     ((TickType_t) 0xFFFFFFFFu)

/* The simulation runs at 1 kHz: one tick is one millisecond. */
#define configTICK_RATE_HZ        1000u
#define portTICK_PERIOD_MS        1u
#define pdMS_TO_TICKS(ms)         ((TickType_t)(ms))
#define pdTICKS_TO_MS(ticks)      ((uint32_t)(ticks))

/* Task priorities (mirrors the FreeRTOS convention: higher wins) */
#define tskIDLE_PRIORITY          0u
#define configMAX_PRIORITIES      8u

#define configMAX_TASK_NAME_LEN   16
#define configMAX_TASKS           16

/* ── Assertions ────────────────────────────────────────────── */
typedef void (*PortAssertHandler_t)(const char* expr, const char* file, int line);

void vPortAssertFailed(const char* expr, const char* file, int line);
void vPortSetAssertHandler(PortAssertHandler_t handler);

#define configASSERT(x)                                                   \
    do {                                                                  \
        if (!(x)) {                                                       \
            vPortAssertFailed(#x, __FILE__, __LINE__);                    \
        }                                                                 \
    } while (0)

/* ── Task API ──────────────────────────────────────────────── */
typedef void (*TaskFunction_t)(void* params);

BaseType_t  xTaskCreate(
    TaskFunction_t  pvTaskCode,
    const char*     pcName,
    uint16_t        usStackDepth,
    void*           pvParameters,
    UBaseType_t     uxPriority,
    TaskHandle_t*   pxCreatedTask);

void        vTaskDelay(TickType_t xTicksToDelay);
void        vTaskDelayUntil(TickType_t* pxPreviousWakeTime, TickType_t xTimeIncrement);
void        vTaskStartScheduler(void);  /* joins all tasks, then returns */

TickType_t    xTaskGetTickCount(void);
const char*   pcTaskGetName(TaskHandle_t xTask);
UBaseType_t   uxTaskGetNumberOfTasks(void);

/* ── Queue API ─────────────────────────────────────────────── */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
void          vQueueDelete(QueueHandle_t xQueue);

BaseType_t    xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue,
                         TickType_t xTicksToWait);
BaseType_t    xQueueReceive(QueueHandle_t xQueue, void* pvBuffer,
                            TickType_t xTicksToWait);
BaseType_t    xQueuePeek(QueueHandle_t xQueue, void* pvBuffer,
                         TickType_t xTicksToWait);
void          xQueueReset(QueueHandle_t xQueue);

UBaseType_t   uxQueueMessagesWaiting(QueueHandle_t xQueue);
UBaseType_t   uxQueueSpacesAvailable(QueueHandle_t xQueue);

/* ── Semaphore / mutex API ─────────────────────────────────── */
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
                                           UBaseType_t uxInitialCount);
void              vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

BaseType_t        xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);

/* ── Heap API ──────────────────────────────────────────────── */
void*  pvPortMalloc(size_t xWantedSize);
void   vPortFree(void* pv);

/* ── Port diagnostics (simulation extensions) ──────────────── */
typedef struct {
    uint32_t tasks_created;
    uint32_t tasks_running;
    uint32_t delay_calls;
    uint32_t deadline_overruns;    /* vTaskDelayUntil woke after its deadline */
    uint32_t max_overrun_ms;       /* worst lateness observed                    */
    uint32_t mutex_violations;     /* give/take by a non-owner                   */
    uint32_t queue_send_timeouts;
    uint32_t queue_receive_timeouts;
    uint32_t semaphore_take_timeouts;
    size_t   live_allocations;
    size_t   live_bytes;
} PortDiagnostics_t;

void vPortGetDiagnostics(PortDiagnostics_t* pxDiagnostics);
void vPortResetDiagnostics(void);
