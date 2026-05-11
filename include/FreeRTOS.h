#pragma once
/*
 * FreeRTOS POSIX Simulation Layer
 * --------------------------------
 * Implements the FreeRTOS API using pthreads + mutexes + condition variables.
 * Code written against this header is API-compatible with real FreeRTOS
 * running on STM32 — only this file changes between simulation and hardware.
 *
 * Implements:
 *   - Tasks     : xTaskCreate, vTaskDelay, vTaskDelayUntil
 *   - Queues    : xQueueCreate, xQueueSend, xQueueReceive, uxQueueMessagesWaiting
 *   - Semaphores: xSemaphoreCreateBinary, xSemaphoreCreateMutex,
 *                 xSemaphoreGive, xSemaphoreTake
 */

#include <stdint.h>
#include <stddef.h>

/* ── FreeRTOS type aliases ─────────────────────────────── */
typedef void*         TaskHandle_t;
typedef void*         QueueHandle_t;
typedef void*         SemaphoreHandle_t;
typedef uint32_t      TickType_t;
typedef int           BaseType_t;
typedef unsigned int  UBaseType_t;
typedef uint32_t      StackType_t;

#define pdTRUE           ((BaseType_t) 1)
#define pdFALSE          ((BaseType_t) 0)
#define pdPASS           ((BaseType_t) 1)
#define pdFAIL           ((BaseType_t) 0)
#define portMAX_DELAY    ((TickType_t) 0xFFFFFFFF)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

/* Task priorities (mirrors FreeRTOS convention) */
#define tskIDLE_PRIORITY         0U
#define configMAX_PRIORITIES     5U

/* ── Task API ──────────────────────────────────────────── */
typedef void (*TaskFunction_t)(void* params);

BaseType_t xTaskCreate(
    TaskFunction_t  pvTaskCode,
    const char*     pcName,
    uint16_t        usStackDepth,
    void*           pvParameters,
    UBaseType_t     uxPriority,
    TaskHandle_t*   pxCreatedTask
);

void vTaskDelay(TickType_t xTicksToDelay);
void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, TickType_t xTimeIncrement);
void vTaskStartScheduler(void);  /* Blocks until all tasks finish */
const char* pcTaskGetName(TaskHandle_t xTask);

/* ── Queue API ─────────────────────────────────────────── */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
void          vQueueDelete(QueueHandle_t xQueue);

BaseType_t xQueueSend(
    QueueHandle_t xQueue,
    const void*   pvItemToQueue,
    TickType_t    xTicksToWait
);

BaseType_t xQueueReceive(
    QueueHandle_t xQueue,
    void*         pvBuffer,
    TickType_t    xTicksToWait
);

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

/* ── Semaphore / Mutex API ─────────────────────────────── */
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void              vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
