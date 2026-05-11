/*
 * FreeRTOS POSIX Simulation Layer — Implementation
 * Maps FreeRTOS API to pthreads, mutexes, and condition variables.
 */
#define _POSIX_C_SOURCE 200809L
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include "FreeRTOS.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>

/* ── Internal structures ───────────────────────────────── */

/* Task wrapper — pairs a pthread with its FreeRTOS function */
typedef struct {
    pthread_t       thread;
    TaskFunction_t  fn;
    void*           params;
    char            name[32];
    UBaseType_t     priority;
} Task_t;

#define MAX_TASKS 16
static Task_t  s_tasks[MAX_TASKS];
static int     s_task_count = 0;
static pthread_mutex_t s_task_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Queue — circular buffer protected by mutex + condvar */
typedef struct {
    uint8_t*        buffer;
    UBaseType_t     item_size;
    UBaseType_t     capacity;
    UBaseType_t     count;
    UBaseType_t     head;
    UBaseType_t     tail;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} Queue_t;

/* Semaphore — binary or counting, built on condvar */
typedef struct {
    int             count;       /* 0 or 1 for binary; n for mutex */
    int             is_mutex;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t       owner;       /* used for mutex recursion guard */
} Sem_t;

/* ── Helpers ───────────────────────────────────────────── */

/* Convert FreeRTOS ticks (ms) to timespec for timed waits */
static void ms_to_abstime(uint32_t ms, struct timespec* ts) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

/* ── Task API ──────────────────────────────────────────── */

static void* task_thread_entry(void* arg) {
    Task_t* t = (Task_t*)arg;
    t->fn(t->params);
    return NULL;
}

BaseType_t xTaskCreate(
    TaskFunction_t  pvTaskCode,
    const char*     pcName,
    uint16_t        usStackDepth,
    void*           pvParameters,
    UBaseType_t     uxPriority,
    TaskHandle_t*   pxCreatedTask)
{
    (void)usStackDepth;  /* not used in simulation */

    pthread_mutex_lock(&s_task_mutex);
    if (s_task_count >= MAX_TASKS) {
        pthread_mutex_unlock(&s_task_mutex);
        return pdFAIL;
    }

    Task_t* t = &s_tasks[s_task_count++];
    t->fn       = pvTaskCode;
    t->params   = pvParameters;
    t->priority = uxPriority;
    strncpy(t->name, pcName ? pcName : "task", sizeof(t->name) - 1);

    if (pxCreatedTask) *pxCreatedTask = (TaskHandle_t)t;

    pthread_mutex_unlock(&s_task_mutex);

    /* Launch the thread */
    if (pthread_create(&t->thread, NULL, task_thread_entry, t) != 0)
        return pdFAIL;

    return pdPASS;
}

void vTaskDelay(TickType_t xTicksToDelay) {
    if (xTicksToDelay == 0) return;
    usleep((useconds_t)xTicksToDelay * 1000);  /* ticks == ms in simulation */
}

void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, TickType_t xTimeIncrement) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t now_ms = (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);

    uint32_t next_wake = *pxPreviousWakeTime + xTimeIncrement;
    *pxPreviousWakeTime = next_wake;

    if (now_ms < next_wake) {
        usleep((useconds_t)(next_wake - now_ms) * 1000);
    }
}

void vTaskStartScheduler(void) {
    /* Wait for all tasks to complete (runs simulation to end) */
    pthread_mutex_lock(&s_task_mutex);
    int count = s_task_count;
    pthread_mutex_unlock(&s_task_mutex);

    for (int i = 0; i < count; i++) {
        pthread_join(s_tasks[i].thread, NULL);
    }
}

const char* pcTaskGetName(TaskHandle_t xTask) {
    if (!xTask) return "unknown";
    return ((Task_t*)xTask)->name;
}

/* ── Queue API ─────────────────────────────────────────── */

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    Queue_t* q = calloc(1, sizeof(Queue_t));
    if (!q) return NULL;
    q->buffer    = calloc(uxQueueLength, uxItemSize);
    q->item_size = uxItemSize;
    q->capacity  = uxQueueLength;
    pthread_mutex_init(&q->mutex,     NULL);
    pthread_cond_init(&q->not_empty,  NULL);
    pthread_cond_init(&q->not_full,   NULL);
    return (QueueHandle_t)q;
}

void vQueueDelete(QueueHandle_t xQueue) {
    Queue_t* q = (Queue_t*)xQueue;
    free(q->buffer);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue,
                      TickType_t xTicksToWait) {
    Queue_t* q = (Queue_t*)xQueue;
    pthread_mutex_lock(&q->mutex);

    if (xTicksToWait == portMAX_DELAY) {
        while (q->count >= q->capacity)
            pthread_cond_wait(&q->not_full, &q->mutex);
    } else if (xTicksToWait > 0) {
        struct timespec ts;
        ms_to_abstime(xTicksToWait, &ts);
        while (q->count >= q->capacity) {
            if (pthread_cond_timedwait(&q->not_full, &q->mutex, &ts) != 0) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    } else {
        if (q->count >= q->capacity) {
            pthread_mutex_unlock(&q->mutex);
            return pdFAIL;
        }
    }

    memcpy(q->buffer + q->tail * q->item_size, pvItemToQueue, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer,
                         TickType_t xTicksToWait) {
    Queue_t* q = (Queue_t*)xQueue;
    pthread_mutex_lock(&q->mutex);

    if (xTicksToWait == portMAX_DELAY) {
        while (q->count == 0)
            pthread_cond_wait(&q->not_empty, &q->mutex);
    } else if (xTicksToWait > 0) {
        struct timespec ts;
        ms_to_abstime(xTicksToWait, &ts);
        while (q->count == 0) {
            if (pthread_cond_timedwait(&q->not_empty, &q->mutex, &ts) != 0) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    } else {
        if (q->count == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFAIL;
        }
    }

    memcpy(pvBuffer, q->buffer + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue) {
    Queue_t* q = (Queue_t*)xQueue;
    pthread_mutex_lock(&q->mutex);
    UBaseType_t c = q->count;
    pthread_mutex_unlock(&q->mutex);
    return c;
}

/* ── Semaphore / Mutex API ─────────────────────────────── */

static SemaphoreHandle_t sem_create(int initial_count, int is_mutex) {
    Sem_t* s = calloc(1, sizeof(Sem_t));
    s->count    = initial_count;
    s->is_mutex = is_mutex;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond,   NULL);
    return (SemaphoreHandle_t)s;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return sem_create(0, 0);  /* starts unavailable */
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return sem_create(1, 1);  /* starts available */
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    Sem_t* s = (Sem_t*)xSemaphore;
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    Sem_t* s = (Sem_t*)xSemaphore;
    pthread_mutex_lock(&s->mutex);

    if (s->is_mutex) {
        s->count = 1;
    } else {
        if (s->count >= 1) {  /* binary: can't exceed 1 */
            pthread_mutex_unlock(&s->mutex);
            return pdFAIL;
        }
        s->count = 1;
    }

    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    Sem_t* s = (Sem_t*)xSemaphore;
    pthread_mutex_lock(&s->mutex);

    if (xTicksToWait == portMAX_DELAY) {
        while (s->count <= 0)
            pthread_cond_wait(&s->cond, &s->mutex);
    } else if (xTicksToWait > 0) {
        struct timespec ts;
        ms_to_abstime(xTicksToWait, &ts);
        while (s->count <= 0) {
            if (pthread_cond_timedwait(&s->cond, &s->mutex, &ts) != 0) {
                pthread_mutex_unlock(&s->mutex);
                return pdFAIL;
            }
        }
    } else {
        if (s->count <= 0) {
            pthread_mutex_unlock(&s->mutex);
            return pdFAIL;
        }
    }

    s->count--;
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}
