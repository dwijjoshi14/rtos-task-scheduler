#pragma once
#include "FreeRTOS.h"
#include "tasks_common.h"

/*
 * SensorTask
 * ----------
 * Simulates a TMP36 analog temperature sensor on ADC pin A0.
 * Every SENSOR_PERIOD_MS, generates a realistic temperature reading
 * (sinusoidal climb from 20 C → 90 C over ~30 s) and pushes it
 * onto xSensorQueue for ControlTask to consume.
 *
 * On real STM32: replace simulate_temperature() with HAL_ADC_GetValue()
 * and the TMP36 voltage-to-celsius conversion formula.
 */

extern QueueHandle_t xSensorQueue;

void SensorTask(void* params);
