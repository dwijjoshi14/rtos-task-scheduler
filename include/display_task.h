#pragma once
/*
 * DisplayTask
 * -----------
 * Lowest priority task: renders the LCD/NeoPixel view of the controller state,
 * prints the critical alert banner when xAlertSemaphore is given, streams the
 * CSV telemetry row and measures sample-to-display latency.
 *
 * Because it runs at the lowest priority it can never delay a control decision;
 * if the console blocks, only the display falls behind — which is exactly what
 * the queue-depth and drop telemetry is there to show.
 *
 * On target hardware printf becomes lcd_printf() / HAL_UART_Transmit() and
 * setStripColor().
 */

#include "FreeRTOS.h"
#include "app_config.h"
#include "tasks_common.h"

/* pvParameters must point at the live AppConfig_t. */
void DisplayTask(void* params);
