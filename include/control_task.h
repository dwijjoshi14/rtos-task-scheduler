#pragma once
#include "FreeRTOS.h"
#include "tasks_common.h"

/*
 * ControlTask
 * -----------
 * Reads temperature readings from xSensorQueue.
 * Applies threshold logic to determine FanState.
 * Sends DisplayMsg to xDisplayQueue for DisplayTask.
 * Gives xAlertSemaphore when temperature goes critical (T > 75 C).
 *
 * On real STM32: FanState maps directly to PWM duty cycle via
 *   HAL_TIM_PWM_Start() and __HAL_TIM_SET_COMPARE().
 */

extern QueueHandle_t     xDisplayQueue;
extern SemaphoreHandle_t xAlertSemaphore;
extern SemaphoreHandle_t xConsoleMutex;

void ControlTask(void* params);
