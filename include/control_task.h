#pragma once
/*
 * ControlTask
 * -----------
 * Consumes SensorReading_t packets and owns every decision in the system:
 *
 *   - applies the hysteresis policy from control_logic.h
 *   - commands the fan duty (published to SensorTask, closing the loop)
 *   - raises xAlertSemaphore on the rising edge of the critical band
 *   - forwards a DisplayMsg_t to DisplayTask
 *   - watches the sensor for liveness, stuck values and implausible readings
 *
 * On target hardware the duty is written to a timer compare register:
 *   __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty_pct * ARR / 100);
 */

#include "FreeRTOS.h"
#include "app_config.h"
#include "tasks_common.h"

extern QueueHandle_t     xDisplayQueue;
extern SemaphoreHandle_t xAlertSemaphore;
extern SemaphoreHandle_t xConsoleMutex;

/* pvParameters must point at the live AppConfig_t. */
void ControlTask(void* params);
