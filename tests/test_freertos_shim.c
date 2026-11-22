/*
 * test_freertos_shim.c — kernel-object semantics of the POSIX port.
 *
 * These tests are what makes the port trustworthy: every task in the
 * application depends on FIFO ordering, bounded capacity, blocking semantics
 * and non-recursive mutexes behaving exactly like the kernel's.
 */

#define _POSIX_C_SOURCE 200809L

#include "FreeRTOS.h"
#include "test_framework.h"

#include <pthread.h>
#include <stdatomic.h>

TEST_CASE(freertos, tick_count_is_monotonic) {
    TickType_t first = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(15));
    TickType_t second = xTaskGetTickCount();
    CHECK(second >= first);
    CHECK(second - first >= 10u);
}

TEST_CASE(freertos, delay_honours_approximate_duration) {
    TickType_t start = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(50));
    uint32_t elapsed = xTaskGetTickCount() - start;
    CHECK(elapsed >= 45u);
    CHECK(elapsed < 250u);
}

TEST_CASE(freertos, delay_until_keeps_period) {
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t start = last_wake;
    for (int i = 0; i < 3; i++) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(30));
    }
    uint32_t elapsed = xTaskGetTickCount() - start;
    CHECK(elapsed >= 85u);
    CHECK(elapsed < 300u);
}

TEST_CASE(freertos, delay_until_advances_wake_time) {
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t before = last_wake;
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    CHECK(last_wake == before + pdMS_TO_TICKS(10));
}

TEST_CASE(freertos, queue_preserves_fifo_order) {
    QueueHandle_t queue = xQueueCreate(4, sizeof(int));
    CHECK(queue != NULL);

    for (int i = 1; i <= 3; i++) {
        CHECK_INT(xQueueSend(queue, &i, 0), pdPASS);
    }

    for (int expected = 1; expected <= 3; expected++) {
        int value = 0;
        CHECK_INT(xQueueReceive(queue, &value, 0), pdPASS);
        CHECK_INT(value, expected);
    }
    CHECK_INT(uxQueueMessagesWaiting(queue), 0);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_rejects_when_full) {
    QueueHandle_t queue = xQueueCreate(2, sizeof(int));
    CHECK(queue != NULL);

    int values[3] = { 1, 2, 3 };
    CHECK_INT(xQueueSend(queue, &values[0], 0), pdPASS);
    CHECK_INT(xQueueSend(queue, &values[1], 0), pdPASS);
    CHECK_INT(xQueueSend(queue, &values[2], 0), pdFAIL);

    CHECK_INT(uxQueueMessagesWaiting(queue), 2);
    CHECK_INT(uxQueueSpacesAvailable(queue), 0);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_receive_times_out) {
    QueueHandle_t queue = xQueueCreate(1, sizeof(int));
    CHECK(queue != NULL);

    int value = 0;
    TickType_t start = xTaskGetTickCount();
    CHECK_INT(xQueueReceive(queue, &value, pdMS_TO_TICKS(20)), pdFAIL);
    uint32_t elapsed = xTaskGetTickCount() - start;

    CHECK(elapsed >= 15u);
    CHECK(elapsed < 200u);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_peek_leaves_item_in_place) {
    QueueHandle_t queue = xQueueCreate(2, sizeof(int));
    CHECK(queue != NULL);

    int sent = 42;
    CHECK_INT(xQueueSend(queue, &sent, 0), pdPASS);

    int peeked = 0;
    CHECK_INT(xQueuePeek(queue, &peeked, 0), pdPASS);
    CHECK_INT(peeked, 42);
    CHECK_INT(uxQueueMessagesWaiting(queue), 1);

    int received = 0;
    CHECK_INT(xQueueReceive(queue, &received, 0), pdPASS);
    CHECK_INT(received, 42);
    CHECK_INT(uxQueueMessagesWaiting(queue), 0);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_reset_discards_contents) {
    QueueHandle_t queue = xQueueCreate(4, sizeof(int));
    CHECK(queue != NULL);

    int value = 7;
    CHECK_INT(xQueueSend(queue, &value, 0), pdPASS);
    CHECK_INT(xQueueSend(queue, &value, 0), pdPASS);
    xQueueReset(queue);
    CHECK_INT(uxQueueMessagesWaiting(queue), 0);

    int received = 0;
    CHECK_INT(xQueueReceive(queue, &received, 0), pdFAIL);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_copies_whole_item) {
    typedef struct { uint32_t a; uint16_t b; uint8_t c; uint8_t d; } Packet_t;
    QueueHandle_t queue = xQueueCreate(1, sizeof(Packet_t));
    CHECK(queue != NULL);

    Packet_t sent = { 0xAABBCCDDu, 0x1234u, 0x56u, 0x78u };
    CHECK_INT(xQueueSend(queue, &sent, 0), pdPASS);

    Packet_t received;
    memset(&received, 0, sizeof received);
    CHECK_INT(xQueueReceive(queue, &received, 0), pdPASS);
    CHECK_INT(received.a, 0xAABBCCDDu);
    CHECK_INT(received.b, 0x1234);
    CHECK_INT(received.c, 0x56);
    CHECK_INT(received.d, 0x78);
    vQueueDelete(queue);
}

TEST_CASE(freertos, queue_handles_invalid_arguments) {
    CHECK(xQueueCreate(0, sizeof(int)) == NULL);
    CHECK(xQueueCreate(4, 0) == NULL);
    CHECK_INT(xQueueSend(NULL, NULL, 0), pdFAIL);
    CHECK_INT(uxQueueMessagesWaiting(NULL), 0);
}

TEST_CASE(freertos, binary_semaphore_give_take) {
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    CHECK(sem != NULL);

    CHECK_INT(xSemaphoreTake(sem, 0), pdFAIL);   /* starts empty */
    CHECK_INT(xSemaphoreGive(sem), pdPASS);
    CHECK_INT(xSemaphoreTake(sem, 0), pdPASS);
    CHECK_INT(xSemaphoreTake(sem, 0), pdFAIL);
    vSemaphoreDelete(sem);
}

TEST_CASE(freertos, binary_semaphore_cannot_overfill) {
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    CHECK(sem != NULL);

    CHECK_INT(xSemaphoreGive(sem), pdPASS);
    CHECK_INT(xSemaphoreGive(sem), pdFAIL);
    vSemaphoreDelete(sem);
}

TEST_CASE(freertos, counting_semaphore_bounds) {
    SemaphoreHandle_t sem = xSemaphoreCreateCounting(3, 0);
    CHECK(sem != NULL);

    CHECK_INT(xSemaphoreTake(sem, 0), pdFAIL);
    for (int i = 0; i < 3; i++) CHECK_INT(xSemaphoreGive(sem), pdPASS);
    CHECK_INT(xSemaphoreGive(sem), pdFAIL);

    for (int i = 0; i < 3; i++) CHECK_INT(xSemaphoreTake(sem, 0), pdPASS);
    CHECK_INT(xSemaphoreTake(sem, 0), pdFAIL);

    CHECK(xSemaphoreCreateCounting(2, 5) == NULL);  /* initial > max */
    vSemaphoreDelete(sem);
}

TEST_CASE(freertos, mutex_is_not_recursive) {
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    CHECK(mutex != NULL);

    CHECK_INT(xSemaphoreTake(mutex, 0), pdPASS);
    CHECK_INT(xSemaphoreTake(mutex, 0), pdFAIL);   /* already held */
    CHECK_INT(xSemaphoreGive(mutex), pdPASS);
    CHECK_INT(xSemaphoreGive(mutex), pdFAIL);      /* not held any more */
    vSemaphoreDelete(mutex);
}

typedef struct {
    SemaphoreHandle_t mutex;
    atomic_int        result;
} OwnerTest_t;

static void* grab_mutex_and_wait(void* arg) {
    OwnerTest_t* test = (OwnerTest_t*)arg;
    if (xSemaphoreTake(test->mutex, portMAX_DELAY) != pdPASS) {
        atomic_store(&test->result, -1);
        return NULL;
    }
    /* Hold it until the main thread has tried (and failed) to give it. */
    vTaskDelay(pdMS_TO_TICKS(60));
    xSemaphoreGive(test->mutex);
    return NULL;
}

TEST_CASE(freertos, mutex_can_only_be_given_by_owner) {
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    CHECK(mutex != NULL);

    OwnerTest_t test;
    test.mutex = mutex;
    atomic_init(&test.result, 0);

    pthread_t thread;
    CHECK_INT(pthread_create(&thread, NULL, grab_mutex_and_wait, &test), 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* This thread does not own the mutex: giving it must fail. */
    CHECK_INT(xSemaphoreGive(mutex), pdFAIL);

    pthread_join(thread, NULL);
    CHECK_INT(atomic_load(&test.result), 0);
    vSemaphoreDelete(mutex);
}

static atomic_int s_task_runs = 0;

static void counting_task(void* params) {
    int* count = (int*)params;
    for (int i = 0; i < *count; i++) {
        atomic_fetch_add(&s_task_runs, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

TEST_CASE(freertos, tasks_run_and_scheduler_joins) {
    atomic_store(&s_task_runs, 0);
    int iterations = 5;

    TaskHandle_t first = NULL, second = NULL;
    CHECK_INT(xTaskCreate(counting_task, "count_a", 256, &iterations, 2, &first), pdPASS);
    CHECK_INT(xTaskCreate(counting_task, "count_b", 256, &iterations, 3, &second), pdPASS);

    CHECK(first != NULL);
    CHECK(first != second);
    CHECK_STR(pcTaskGetName(first), "count_a");

    vTaskStartScheduler();
    CHECK_INT(atomic_load(&s_task_runs), 10);
}

TEST_CASE(freertos, task_create_validates_priority) {
    TaskHandle_t handle = NULL;
    CHECK_INT(xTaskCreate(counting_task, "bad", 256, NULL,
                          configMAX_PRIORITIES, &handle), pdFAIL);
    CHECK_INT(xTaskCreate(NULL, "bad", 256, NULL, 1, &handle), pdFAIL);
}

TEST_CASE(freertos, kernel_objects_release_their_memory) {
    PortDiagnostics_t before;
    vPortGetDiagnostics(&before);

    QueueHandle_t queue = xQueueCreate(8, sizeof(int));
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    CHECK(queue != NULL);
    CHECK(sem != NULL);
    CHECK(mutex != NULL);

    PortDiagnostics_t during;
    vPortGetDiagnostics(&during);
    CHECK(during.live_allocations > before.live_allocations);

    vQueueDelete(queue);
    vSemaphoreDelete(sem);
    vSemaphoreDelete(mutex);

    PortDiagnostics_t after;
    vPortGetDiagnostics(&after);
    CHECK_INT(after.live_allocations, before.live_allocations);
    CHECK_INT(after.live_bytes, before.live_bytes);
}

TEST_CASE(freertos, heap_roundtrip) {
    PortDiagnostics_t before;
    vPortGetDiagnostics(&before);

    unsigned char* block = pvPortMalloc(64);
    CHECK(block != NULL);
    memset(block, 0xAB, 64);
    CHECK_INT(block[0], 0xAB);
    CHECK_INT(block[63], 0xAB);

    vPortFree(block);
    vPortFree(NULL);   /* must be a no-op */

    PortDiagnostics_t after;
    vPortGetDiagnostics(&after);
    CHECK_INT(after.live_allocations, before.live_allocations);
    CHECK_INT(pvPortMalloc(0) == NULL, 1);
}
