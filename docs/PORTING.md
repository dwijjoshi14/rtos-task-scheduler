# Porting to STM32 hardware

The application sources use only the FreeRTOS API. Porting means replacing the
port and the drivers, not the tasks.

## 1. Replace the port

Remove `src/FreeRTOS_sim.c` and `include/FreeRTOS.h` from the build, then add the
real kernel:

```
FreeRTOS/Source/tasks.c queue.c list.c timers.c
FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
FreeRTOS/Source/portable/MemMang/heap_4.c
```

`include/FreeRTOS.h` is a stand-in for the kernel's own header, so
`#include "FreeRTOS.h"` in the task files keeps resolving — but the real header
requires `FreeRTOSConfig.h` next to it. A minimal configuration for this project:

```c
#define configUSE_PREEMPTION             1
#define configUSE_TIME_SLICING           1
#define configCPU_CLOCK_HZ               168000000
#define configTICK_RATE_HZ               1000
#define configMAX_PRIORITIES             8
#define configMINIMAL_STACK_SIZE         128
#define configTOTAL_HEAP_SIZE            (16 * 1024)
#define configUSE_MUTEXES                1
#define configUSE_COUNTING_SEMAPHORES    1
#define INCLUDE_vTaskDelayUntil          1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
```

The task priorities (`2..4`) and stack sizes (`256` words) in
`tasks_common.h` are already expressed in kernel terms. On a Cortex-M4 with
`configMINIMAL_STACK_SIZE = 128`, 256 words is a comfortable starting point; trim
it with `uxTaskGetStackHighWaterMark()` once the system runs.

## 2. Replace the sensor

`src/sensor_task.c` currently steps `thermal_model.c`. On hardware, replace the
plant with an ADC reading and delete `thermal_model.c` from the build:

```c
#include "stm32f4xx_hal.h"

static float read_temperature_c(ADC_HandleTypeDef* adc) {
    uint32_t raw = 0;
    HAL_ADC_Start(adc);
    if (HAL_ADC_PollForConversion(adc, 2) != HAL_OK) return NAN;  /* detector catches it */
    raw = HAL_ADC_GetValue(adc);          /* 12-bit */
    HAL_ADC_Stop(adc);

    float voltage = ((float)raw / 4095.0f) * 3.3f;
    return (voltage - 0.5f) * 100.0f;     /* TMP36: 10 mV/°C, 500 mV at 0 °C */
}
```

Keep the rest of `SensorTask` unchanged — the EMA, the sequence counter and the
queue publication are all hardware independent. `simulate`'s noise injection
disappears; real ADC jitter takes its place.

## 3. Replace the actuator

`control_task.c` publishes the duty atomically for the simulated plant. On
hardware, write the compare register instead:

```c
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)duty * __HAL_TIM_GET_AUTORELOAD(&htim2) / 100u);
```

Keep the atomic store if another task reads the duty; on a single core it can be
a plain variable, but leaving it atomic is harmless and documents the intent.

## 4. Replace the display

`display_task.c` uses `printf`. On target, map the same information:

```c
lcd_set_cursor(0, 0); lcd_printf("T %5.1fC %-4s %3u%%", temp, name, duty);
lcd_set_cursor(0, 1); lcd_printf("alerts %u  drops %u", alerts, drops);
setStripColor(r, g, b);   /* NeoPixel ring mirrors the fan state */
```

The sparkline and CSV streaming are host conveniences; drop them or route them
over UART. Keep the latency measurement if you have a free timer: it is the
cheapest end-to-end health signal you will get.

## 5. Run-control glue

`src/runtime.c` exists so unit tests can link the tasks without `main()`. On
target, replace it with:

* `g_sim_stop` → no equivalent; the firmware runs forever. Wrap the task loops in
  `for (;;)` and keep the stop checks only if you implement a sleep mode.
* `g_sigint` → not applicable.
* `g_sim_start_ms` → `xTaskGetTickCount()` at startup, used for uptime logging.
* `sim_deadline_reached()` → not applicable.

## 6. Things the simulation does not prove

Be explicit about this in review:

* **Priority scheduling.** The POSIX port does not enforce FreeRTOS priorities;
  the host scheduler decides. On target, `ControlTask` at priority 4 will
  preempt `SensorTask` (3) and `DisplayTask` (2) as intended.
* **Stack depth.** pthread stacks are heap-allocated and large. Measure
  high-water marks on target.
* **Interrupt latency, tick jitter, cache behaviour.** Not modelled.
* **`configASSERT`.** The port refuses invalid calls and counts them; the kernel
  asserts. Expect louder failures on hardware, which is usually what you want
  during bring-up.

## Checklist

- [ ] Real kernel sources and `FreeRTOSConfig.h` in the build
- [ ] `thermal_model.c` and `FreeRTOS_sim.c` removed
- [ ] ADC driver behind `read_temperature_c()`
- [ ] PWM compare register written in `ControlTask`
- [ ] `printf` replaced by LCD/UART
- [ ] `runtime.c` replaced with target equivalents
- [ ] Stack high-water marks measured and stack sizes trimmed
- [ ] Watchdog peripheral fed from a task that checks all three are alive
- [ ] Hysteresis and thresholds re-tuned with `docs/CONTROL_THEORY.md`
