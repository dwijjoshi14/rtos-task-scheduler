#pragma once
#include "FreeRTOS.h"
#include "tasks_common.h"

/*
 * DisplayTask
 * -----------
 * Two responsibilities:
 *  1. Reads DisplayMsg from xDisplayQueue and prints formatted status.
 *  2. Waits on xAlertSemaphore — when taken, fires a critical alert banner.
 *
 * On real STM32: replace printf with lcd_print() / HAL_UART_Transmit()
 * and setStripColor() for the NeoPixel LED strip.
 */
void DisplayTask(void* params);
