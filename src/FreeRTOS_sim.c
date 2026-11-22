/*
 * FreeRTOS_sim.c — POSIX implementation of the FreeRTOS API subset in FreeRTOS.h
 *
 * Design notes
 * ------------
 * 1. Timed waits use CLOCK_MONOTONIC. Condition variables are created with a
 *    monotonic clock attribute, so absolute deadlines and the clock the kernel
 *    actually measures are the same source. Mixing CLOCK_REALTIME deadlines
 *    with CLOCK_MONOTONIC elapsed time makes waits return early or hang
 *    whenever the wall clock is adjusted.
 * 2. Mutexes are non-recursive and owned. Giving a mutex the caller does not
 *    hold is a bug on target hardware (configASSERT in the kernel); here it is
 *    refused and counted so a test can prove the application never does it.
 * 3. vTaskDelayUntil measures lateness rather than hiding it. A task that
 *    misses its deadline is counted, which is how real firmware qualifies
 *    scheduling jitter instead of silently drifting.
 * 4. All kernel objects are allocated through pvPortMalloc so leak checks in
 *    the test suite observe the same allocator the port uses.
 */

#define _POSIX_C_SOURCE 200809L

#include "FreeRTOS.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Diagnostics ───────────────────────────────────────────── */

static PortDiagnostics_t s_diag;
static pthread_mutex_t   s_diag_mutex = PTHREAD_MUTEX_INITIALIZER;

void vPortGetDiagnostics(PortDiagnostics_t* pxDiagnostics) {
    if (!pxDiagnostics) return;
    pthread_mutex_lock(&s_diag_mutex);
    *pxDiagnostics = s_diag;
    pthread_mutex_unlock(&s_diag_mutex);
}

void vPortResetDiagnostics(void) {
    pthread_mutex_lock(&s_diag_mutex);
    size_t live_allocs = s_diag.live_allocations;
    size_t live_bytes  = s_diag.live_bytes;
    memset(&s_diag, 0, sizeof s_diag);
    s_diag.live_allocations = live_allocs;
    s_diag.live_bytes       = live_bytes;
    pthread_mutex_unlock(&s_diag_mutex);
}

static void diag_add(uint32_t* field, uint32_t amount) {
    pthread_mutex_lock(&s_diag_mutex);
    *field += amount;
    pthread_mutex_unlock(&s_diag_mutex);
}

static void diag_max(uint32_t* field, uint32_t value) {
    pthread_mutex_lock(&s_diag_mutex);
    if (value > *field) *field = value;
    pthread_mutex_unlock(&s_diag_mutex);
}

static void diag_alloc_delta(size_t allocations, size_t bytes) {
    pthread_mutex_lock(&s_diag_mutex);
    s_diag.live_allocations += allocations;
    s_diag.live_bytes       += bytes;
    pthread_mutex_unlock(&s_diag_mutex);
}

/* ── Assertion hook ────────────────────────────────────────── */

static PortAssertHandler_t s_assert_handler = NULL;

void vPortSetAssertHandler(PortAssertHandler_t handler) {
    s_assert_handler = handler;
}

void vPortAssertFailed(const char* expr, const char* file, int line) {
    fprintf(stderr, "[port] configASSERT FAILED: %s (%s:%d)\n",
            expr ? expr : "?", file ? file : "?", line);
    if (s_assert_handler) {
        s_assert_handler(expr, file, line);
        return;
    }
    abort();
}

/* ── Clock helpers ─────────────────────────────────────────── */

static void init_monotonic_cond(pthread_cond_t* cond) {
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) == 0 &&
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0) {
        pthread_cond_init(cond, &attr);
    } else {
        pthread_cond_init(cond, NULL);
    }
    pthread_condattr_destroy(&attr);
}

/* Absolute CLOCK_MONOTONIC deadline `ms` milliseconds from now. */
static struct timespec abstime_after_ms(uint32_t ms) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t nsec = (uint64_t)ts.tv_nsec + (uint64_t)(ms % 1000u) * 1000000ull;
    ts.tv_sec  += (time_t)(ms / 1000u) + (time_t)(nsec / 1000000000ull);
    ts.tv_nsec  = (long)(nsec % 1000000000ull);
    return ts;
}

TickType_t xTaskGetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (TickType_t)((uint64_t)ts.tv_sec * 1000ull +
                        (uint64_t)ts.tv_nsec / 1000000ull);
}

/* ── Heap (ported to pvPortMalloc/vPortFree) ───────────────── */

typedef union {
    struct { size_t size; } meta;
    max_align_t align;
} AllocHeader_t;

void* pvPortMalloc(size_t xWantedSize) {
    if (xWantedSize == 0) return NULL;
    AllocHeader_t* header = malloc(sizeof(AllocHeader_t) + xWantedSize);
    if (!header) return NULL;
    header->meta.size = xWantedSize;
    diag_alloc_delta(1u, xWantedSize);
    return (void*)(header + 1);
}

void vPortFree(void* pv) {
    if (!pv) return;
    AllocHeader_t* header = ((AllocHeader_t*)pv) - 1;
    diag_alloc_delta((size_t)-1, (size_t)0 - header->meta.size);
    free(header);
}

static void* port_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) return NULL;
    if (count > (size_t)-1 / size) return NULL;
    size_t total = count * size;
    void* mem = pvPortMalloc(total);
    if (mem) memset(mem, 0, total);
    return mem;
}

/* ── Tasks ─────────────────────────────────────────────────── */

typedef struct {
    pthread_t      thread;
    TaskFunction_t fn;
    void*          params;
    char           name[configMAX_TASK_NAME_LEN];
    UBaseType_t    priority;
    bool           thread_started;
} Task_t;

static Task_t          s_tasks[configMAX_TASKS];
static int             s_task_count = 0;
static pthread_mutex_t s_task_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* task_thread_entry(void* arg) {
    Task_t* task = (Task_t*)arg;
    pthread_mutex_lock(&s_diag_mutex);
    s_diag.tasks_running++;
    pthread_mutex_unlock(&s_diag_mutex);

    task->fn(task->params);

    pthread_mutex_lock(&s_diag_mutex);
    s_diag.tasks_running--;
    pthread_mutex_unlock(&s_diag_mutex);
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
    (void)usStackDepth;  /* the pthread stack is managed by libc */

    if (!pvTaskCode) return pdFAIL;
    if (!pcName) pcName = "task";
    if (uxPriority >= configMAX_PRIORITIES) return pdFAIL;

    pthread_mutex_lock(&s_task_mutex);
    if (s_task_count >= configMAX_TASKS) {
        pthread_mutex_unlock(&s_task_mutex);
        return pdFAIL;
    }

    Task_t* task = &s_tasks[s_task_count];
    task->fn       = pvTaskCode;
    task->params   = pvParameters;
    task->priority = uxPriority;
    strncpy(task->name, pcName, sizeof task->name - 1);
    task->name[sizeof task->name - 1] = '\0';
    task->thread_started = false;

    if (pthread_create(&task->thread, NULL, task_thread_entry, task) != 0) {
        memset(task, 0, sizeof *task);
        pthread_mutex_unlock(&s_task_mutex);
        return pdFAIL;
    }

    task->thread_started = true;
    s_task_count++;
    pthread_mutex_unlock(&s_task_mutex);

    diag_add(&s_diag.tasks_created, 1u);
    if (pxCreatedTask) *pxCreatedTask = (TaskHandle_t)task;
    return pdPASS;
}

void vTaskDelay(TickType_t xTicksToDelay) {
    diag_add(&s_diag.delay_calls, 1u);
    if (xTicksToDelay == 0) {
        sched_yield();
        return;
    }
    struct timespec remaining;
    remaining.tv_sec  = (time_t)(xTicksToDelay / 1000u);
    remaining.tv_nsec = (long)(xTicksToDelay % 1000u) * 1000000L;
    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
        /* an interrupting signal (e.g. SIGINT) must not shorten the delay */
    }
}

void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, TickType_t xTimeIncrement) {
    configASSERT(pxPreviousWakeTime != NULL);
    if (!pxPreviousWakeTime || xTimeIncrement == 0) return;

    diag_add(&s_diag.delay_calls, 1u);

    const TickType_t wake = *pxPreviousWakeTime + xTimeIncrement;
    *pxPreviousWakeTime = wake;

    const TickType_t now = xTaskGetTickCount();
    const int32_t    late_by = (int32_t)(now - wake);

    if (late_by > 0) {
        /* The previous period overran: report it, never busy-spin over it. */
        diag_add(&s_diag.deadline_overruns, 1u);
        diag_max(&s_diag.max_overrun_ms, (uint32_t)late_by);
        return;
    }
    vTaskDelay(wake - now);
}

void vTaskStartScheduler(void) {
    int count;
    pthread_mutex_lock(&s_task_mutex);
    count = s_task_count;
    pthread_mutex_unlock(&s_task_mutex);

    /* Unlike the kernel, the simulation scheduler returns once every task has
     * finished, which is what lets main() print a report and exit cleanly. */
    for (int i = 0; i < count; i++) {
        if (s_tasks[i].thread_started) {
            pthread_join(s_tasks[i].thread, NULL);
            s_tasks[i].thread_started = false;
        }
    }
}

const char* pcTaskGetName(TaskHandle_t xTask) {
    if (!xTask) return "unknown";
    return ((Task_t*)xTask)->name;
}

UBaseType_t uxTaskGetNumberOfTasks(void) {
    pthread_mutex_lock(&s_task_mutex);
    UBaseType_t count = (UBaseType_t)s_task_count;
    pthread_mutex_unlock(&s_task_mutex);
    return count;
}

/* ── Queues ────────────────────────────────────────────────── */

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

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    /* Invalid arguments are reported, not asserted: on target hardware a bad
     * length is a configuration bug, and killing the task helps nobody. */
    if (uxQueueLength == 0 || uxItemSize == 0) return NULL;

    Queue_t* queue = port_calloc(1, sizeof *queue);
    if (!queue) return NULL;

    queue->buffer = port_calloc(uxQueueLength, uxItemSize);
    if (!queue->buffer) {
        vPortFree(queue);
        return NULL;
    }

    queue->item_size = uxItemSize;
    queue->capacity  = uxQueueLength;
    pthread_mutex_init(&queue->mutex, NULL);
    init_monotonic_cond(&queue->not_empty);
    init_monotonic_cond(&queue->not_full);
    return (QueueHandle_t)queue;
}

void vQueueDelete(QueueHandle_t xQueue) {
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    vPortFree(queue->buffer);
    vPortFree(queue);
}

/* Shared wait helper: block until `ready` is signalled, honouring a timeout.
 * Returns pdTRUE if the caller may proceed, pdFALSE on timeout. */
static BaseType_t queue_wait(Queue_t* queue, pthread_cond_t* cond,
                             int want_full, TickType_t xTicksToWait)
{
    int ready = want_full ? (queue->count < queue->capacity)
                          : (queue->count > 0);
    if (ready) return pdTRUE;

    if (xTicksToWait == 0) return pdFALSE;

    if (xTicksToWait == portMAX_DELAY) {
        while (!ready) {
            pthread_cond_wait(cond, &queue->mutex);
            ready = want_full ? (queue->count < queue->capacity)
                              : (queue->count > 0);
        }
        return pdTRUE;
    }

    struct timespec deadline = abstime_after_ms(xTicksToWait);
    while (!ready) {
        int rc = pthread_cond_timedwait(cond, &queue->mutex, &deadline);
        if (rc == ETIMEDOUT) return pdFALSE;
        ready = want_full ? (queue->count < queue->capacity)
                          : (queue->count > 0);
    }
    return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue,
                      TickType_t xTicksToWait)
{
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue || !pvItemToQueue) return pdFAIL;

    pthread_mutex_lock(&queue->mutex);
    if (queue_wait(queue, &queue->not_full, 1, xTicksToWait) != pdTRUE) {
        pthread_mutex_unlock(&queue->mutex);
        diag_add(&s_diag.queue_send_timeouts, 1u);
        return pdFAIL;
    }

    memcpy(queue->buffer + (size_t)queue->tail * queue->item_size,
           pvItemToQueue, queue->item_size);
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return pdPASS;
}

static BaseType_t queue_read(QueueHandle_t xQueue, void* pvBuffer,
                             TickType_t xTicksToWait, int peek)
{
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue || !pvBuffer) return pdFAIL;

    pthread_mutex_lock(&queue->mutex);
    if (queue_wait(queue, &queue->not_empty, 0, xTicksToWait) != pdTRUE) {
        pthread_mutex_unlock(&queue->mutex);
        diag_add(&s_diag.queue_receive_timeouts, 1u);
        return pdFAIL;
    }

    memcpy(pvBuffer, queue->buffer + (size_t)queue->head * queue->item_size,
           queue->item_size);
    if (!peek) {
        queue->head = (queue->head + 1u) % queue->capacity;
        queue->count--;
        pthread_cond_signal(&queue->not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer,
                         TickType_t xTicksToWait)
{
    return queue_read(xQueue, pvBuffer, xTicksToWait, 0);
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void* pvBuffer,
                      TickType_t xTicksToWait)
{
    return queue_read(xQueue, pvBuffer, xTicksToWait, 1);
}

void xQueueReset(QueueHandle_t xQueue) {
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    queue->head = queue->tail = queue->count = 0;
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue) {
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue) return 0;
    pthread_mutex_lock(&queue->mutex);
    UBaseType_t count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue) {
    Queue_t* queue = (Queue_t*)xQueue;
    if (!queue) return 0;
    pthread_mutex_lock(&queue->mutex);
    UBaseType_t spaces = queue->capacity - queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return spaces;
}

/* ── Semaphores and mutexes ────────────────────────────────── */

typedef struct {
    int             count;
    int             max_count;
    int             is_mutex;
    pthread_t       owner;
    bool            owner_valid;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} Sem_t;

static SemaphoreHandle_t sem_create(int max_count, int initial, int is_mutex) {
    if (max_count <= 0 || initial < 0 || initial > max_count) return NULL;
    Sem_t* sem = port_calloc(1, sizeof *sem);
    if (!sem) return NULL;

    sem->count     = initial;
    sem->max_count = max_count;
    sem->is_mutex  = is_mutex;
    sem->owner_valid = false;
    pthread_mutex_init(&sem->mutex, NULL);
    init_monotonic_cond(&sem->cond);
    return (SemaphoreHandle_t)sem;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return sem_create(1, 0, 0);   /* available only after the first give */
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return sem_create(1, 1, 1);   /* starts available */
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
                                           UBaseType_t uxInitialCount) {
    return sem_create((int)uxMaxCount, (int)uxInitialCount, 0);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    Sem_t* sem = (Sem_t*)xSemaphore;
    if (!sem) return;
    pthread_mutex_destroy(&sem->mutex);
    pthread_cond_destroy(&sem->cond);
    vPortFree(sem);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    Sem_t* sem = (Sem_t*)xSemaphore;
    if (!sem) return pdFAIL;

    pthread_mutex_lock(&sem->mutex);

    if (sem->is_mutex) {
        if (!sem->owner_valid ||
            !pthread_equal(sem->owner, pthread_self())) {
            /* Giving a mutex you do not own is a hard bug on hardware. */
            pthread_mutex_unlock(&sem->mutex);
            diag_add(&s_diag.mutex_violations, 1u);
            return pdFAIL;
        }
        sem->owner_valid = false;
        sem->count = 1;
    } else {
        if (sem->count >= sem->max_count) {
            pthread_mutex_unlock(&sem->mutex);
            return pdFAIL;
        }
        sem->count++;
    }

    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
    return pdPASS;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    Sem_t* sem = (Sem_t*)xSemaphore;
    if (!sem) return pdFAIL;

    pthread_mutex_lock(&sem->mutex);

    if (sem->is_mutex && sem->owner_valid &&
        pthread_equal(sem->owner, pthread_self())) {
        /* Non-recursive by design: a nested take would deadlock on hardware. */
        pthread_mutex_unlock(&sem->mutex);
        diag_add(&s_diag.mutex_violations, 1u);
        return pdFAIL;
    }

    if (sem->count <= 0) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&sem->mutex);
            return pdFAIL;
        }
        if (xTicksToWait == portMAX_DELAY) {
            while (sem->count <= 0) pthread_cond_wait(&sem->cond, &sem->mutex);
        } else {
            struct timespec deadline = abstime_after_ms(xTicksToWait);
            while (sem->count <= 0) {
                if (pthread_cond_timedwait(&sem->cond, &sem->mutex,
                                           &deadline) == ETIMEDOUT) {
                    pthread_mutex_unlock(&sem->mutex);
                    diag_add(&s_diag.semaphore_take_timeouts, 1u);
                    return pdFAIL;
                }
            }
        }
    }

    sem->count--;
    if (sem->is_mutex) {
        sem->owner       = pthread_self();
        sem->owner_valid = true;
    }
    pthread_mutex_unlock(&sem->mutex);
    return pdPASS;
}
