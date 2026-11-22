/*
 * runtime.c — run-wide state shared by every task.
 *
 * Kept out of main.c so unit tests can link the task sources without dragging
 * in main().
 */

#define _POSIX_C_SOURCE 200809L

#include "tasks_common.h"

atomic_int            g_sim_stop     = 0;
volatile sig_atomic_t g_sigint       = 0;
atomic_int            g_fan_duty_pct = 0;
_Atomic(uint32_t)     g_sim_start_ms = 0;

int sim_should_stop(void) {
    return atomic_load(&g_sim_stop) != 0 || g_sigint != 0;
}

void sim_request_stop(void) {
    atomic_store(&g_sim_stop, 1);
}

uint32_t sim_elapsed_ms(void) {
    return xTaskGetTickCount() - atomic_load(&g_sim_start_ms);
}

int sim_deadline_reached(uint32_t duration_ms) {
    if (duration_ms == 0) return 0;
    return sim_elapsed_ms() >= duration_ms;
}
