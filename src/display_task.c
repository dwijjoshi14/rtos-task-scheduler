#include "display_task.h"
#include "control_task.h"
#include "FreeRTOS.h"
#include "tasks_common.h"
#include <stdio.h>
#include <string.h>

/* ANSI colour codes for terminal output */
#define RED     "\033[1;31m"
#define YELLOW  "\033[1;33m"
#define GREEN   "\033[1;32m"
#define CYAN    "\033[1;36m"
#define RESET   "\033[0m"

static const char* fan_bar(FanState_t state) {
    switch (state) {
        case FAN_OFF:  return "          ";
        case FAN_LOW:  return "####      ";
        case FAN_HIGH: return "#######   ";
        case FAN_MAX:  return "##########";
        default:       return "          ";
    }
}

static const char* temp_colour(float t) {
    if (t > TEMP_CRITICAL_THRESHOLD) return RED;
    if (t > TEMP_HIGH_THRESHOLD)     return YELLOW;
    if (t > TEMP_LOW_THRESHOLD)      return GREEN;
    return CYAN;
}

/*
 * Prints a simulated 16x2 LCD + NeoPixel status line.
 * On real hardware this maps to:
 *   lcd_set_cursor(0,0); lcd_printf("Temp: %.1fC", msg.temperature_c);
 *   lcd_set_cursor(0,1); lcd_printf("Fan:  %s %d%%", fan_name, duty);
 *   setStripColor(r, g, b);
 */
static void render_display(const DisplayMsg_t* msg) {
    const char* col = temp_colour(msg->temperature_c);

    xSemaphoreTake(xConsoleMutex, portMAX_DELAY);
    printf("%s[LCD] Temp: %5.1f C  |  Fan: %-4s  [%s] %s | t=%u ms%s\n",
           col,
           msg->temperature_c,
           msg->fan_state == FAN_OFF  ? "OFF"  :
           msg->fan_state == FAN_LOW  ? "LOW"  :
           msg->fan_state == FAN_HIGH ? "HIGH" : "MAX",
           fan_bar(msg->fan_state),
           msg->is_critical ? RED "⚠ CRITICAL" RESET : "",
           msg->tick_ms,
           RESET);
    xSemaphoreGive(xConsoleMutex);
}

static void render_alert(void) {
    xSemaphoreTake(xConsoleMutex, portMAX_DELAY);
    printf(RED "\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║  ⚡  CRITICAL TEMPERATURE ALERT  ⚡  ║\n");
    printf("  ║     Fan at MAX — check cooling!      ║\n");
    printf("  ╚══════════════════════════════════════╝\n");
    printf(RESET "\n");
    xSemaphoreGive(xConsoleMutex);
}

void DisplayTask(void* params) {
    (void)params;
    DisplayMsg_t msg;
    int          msg_count = 0;

    for (;;) {
        /* Check for critical alert — non-blocking poll */
        if (xSemaphoreTake(xAlertSemaphore, 0) == pdPASS) {
            render_alert();
        }

        /* Read next display message — block up to 200 ms */
        if (xQueueReceive(xDisplayQueue, &msg, pdMS_TO_TICKS(200)) == pdPASS) {
            render_display(&msg);
            msg_count++;

            /* Stop simulation after 30 readings for demo purposes */
            if (msg_count >= 30) {
                xSemaphoreTake(xConsoleMutex, portMAX_DELAY);
                printf(CYAN "\n[DisplayTask] Simulation complete — %d readings processed.\n" RESET, msg_count);
                xSemaphoreGive(xConsoleMutex);
                g_sim_stop = 1;
                return;
            }
        }
    }
}
