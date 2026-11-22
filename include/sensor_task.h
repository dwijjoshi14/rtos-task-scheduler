#pragma once
/*
 * SensorTask
 * ----------
 * Samples the simulated thermal plant every sensor period, applies the EMA
 * filter and publishes SensorReading_t packets on xSensorQueue.
 *
 * The plant is stepped with the fan duty published by ControlTask, so the
 * sensor is measuring a closed loop rather than a scripted waveform. The raw
 * plant value is included alongside the filtered one: the control decision uses
 * the filtered value, the CSV export keeps both so the filter can be evaluated
 * offline.
 *
 * On target hardware simulate_temperature() is replaced by an ADC read and the
 * TMP36 conversion; nothing else in the task changes.
 */

#include "FreeRTOS.h"
#include "app_config.h"
#include "tasks_common.h"

extern QueueHandle_t xSensorQueue;

/* pvParameters must point at the live AppConfig_t. */
void SensorTask(void* params);
