# RTOS Task Scheduler — FreeRTOS Temperature Control System

A three-task FreeRTOS application simulating a real-time temperature monitoring
and fan control system. Written against the standard FreeRTOS API and runnable
on Linux/WSL via a POSIX simulation layer — the task logic is identical to what
runs on STM32 hardware.

---

## Architecture

```
SensorTask (P3) ──[xSensorQueue]──► ControlTask (P4) ──[xDisplayQueue]──► DisplayTask (P2)
                                          │
                                          └──[xAlertSemaphore]────────────► DisplayTask (P2)

All tasks share xConsoleMutex to protect console output from interleaving.
```

| Task | Priority | Period | Role |
|---|---|---|---|
| `SensorTask` | 3 | 500 ms | Simulates TMP36 ADC reads; pushes `SensorReading_t` to `xSensorQueue` |
| `ControlTask` | 4 (highest) | event-driven | Reads queue; applies thresholds; drives fan PWM state; gives `xAlertSemaphore` on critical temp |
| `DisplayTask` | 2 | 100 ms poll | Reads `xDisplayQueue`; renders LCD + NeoPixel output; handles critical alerts |

### Kernel objects used

| Object | Type | Purpose |
|---|---|---|
| `xSensorQueue` | Queue (depth 8) | SensorTask → ControlTask temperature data |
| `xDisplayQueue` | Queue (depth 8) | ControlTask → DisplayTask formatted status |
| `xAlertSemaphore` | Binary semaphore | ControlTask → DisplayTask critical alert signal |
| `xConsoleMutex` | Mutex | Protects shared console output across all tasks |

---

## Sample Output

```
╔═══════════════════════════════════════════════╗
║   FreeRTOS Task Scheduler — POSIX Simulation  ║
║   Temperature Monitoring & Fan Control        ║
╚═══════════════════════════════════════════════╝

[main] Kernel objects created: xSensorQueue, xDisplayQueue, xAlertSemaphore, xConsoleMutex
[main] Tasks created: SensorTask(P3), ControlTask(P4), DisplayTask(P2)
[main] Starting scheduler...

[ControlTask] Fan state: OFF → LOW  (32.4 C, PWM 40%)
[LCD] Temp:  32.4 C  |  Fan: LOW   [####      ]  | t=1042 ms
[LCD] Temp:  34.1 C  |  Fan: LOW   [####      ]  | t=1542 ms
[ControlTask] Fan state: LOW → HIGH (52.7 C, PWM 70%)
[LCD] Temp:  52.7 C  |  Fan: HIGH  [#######   ]  | t=8212 ms
[ControlTask] Fan state: HIGH → MAX (76.3 C, PWM 100%)

  ╔══════════════════════════════════════╗
  ║  ⚡  CRITICAL TEMPERATURE ALERT  ⚡  ║
  ║     Fan at MAX — check cooling!      ║
  ╚══════════════════════════════════════╝

[LCD] Temp:  76.3 C  |  Fan: MAX   [##########] ⚠ CRITICAL | t=14881 ms
```

---

## Fan Control Thresholds

| Temperature | Fan State | PWM Duty | LED Colour (STM32) |
|---|---|---|---|
| ≤ 30 °C | OFF | 0% | Blue |
| 30–50 °C | LOW | 40% | Green |
| 50–75 °C | HIGH | 70% | Yellow |
| > 75 °C | MAX | 100% | Red |

---

## Build & Run

**Requirements:** GCC, pthreads (standard on Linux/macOS/WSL)

```bash
git clone https://github.com/dwijjoshi14/rtos-task-scheduler.git
cd rtos-task-scheduler
make
make run
```

**Manual build:**
```bash
gcc src/FreeRTOS_sim.c src/sensor_task.c src/control_task.c \
    src/display_task.c src/main.c \
    -std=c11 -Wall -O2 -Iinclude -lm -lpthread -o rtos_scheduler
./rtos_scheduler
```

---

## Project Structure

```
rtos-task-scheduler/
├── include/
│   ├── FreeRTOS.h          # FreeRTOS API — identical to hardware header
│   ├── tasks_common.h      # Shared types, thresholds, queue depths
│   ├── sensor_task.h
│   ├── control_task.h
│   └── display_task.h
├── src/
│   ├── FreeRTOS_sim.c      # POSIX simulation layer (pthreads backend)
│   ├── main.c              # Kernel object creation + task launch
│   ├── sensor_task.c       # TMP36 simulation + queue producer
│   ├── control_task.c      # Threshold logic + semaphore signalling
│   └── display_task.c      # Output rendering + alert handler
├── Makefile
└── README.md
```

---

## Porting to Real STM32 Hardware

The task logic is hardware-agnostic. To run on STM32:

1. Replace `FreeRTOS.h` and `FreeRTOS_sim.c` with the real FreeRTOS kernel
   (`FreeRTOS/Source/` + `portable/GCC/ARM_CM4/`)
2. In `sensor_task.c`: replace `simulate_temperature()` with
   `HAL_ADC_GetValue(&hadc1)` and the TMP36 voltage formula
3. In `control_task.c`: map `FanState_t` to `__HAL_TIM_SET_COMPARE()`
   for PWM output on a timer channel
4. In `display_task.c`: replace `printf` with `lcd_print()` / `HAL_UART_Transmit()`
   and `setStripColor()` for NeoPixel output

The queue depths, thresholds, and task periods in `tasks_common.h` need no changes.

---

## Why This Matters

Real embedded systems are concurrent by nature — sensors fire on timers,
actuators respond to events, displays refresh independently. FreeRTOS provides
the primitives (queues, semaphores, mutexes) to coordinate this safely.
This project demonstrates:
- Inter-task communication via typed queues
- Event-driven synchronization via binary semaphore
- Shared resource protection via mutex
- Priority-based task scheduling
- Clean separation between hardware drivers and application logic

---

## Built With

`C11` · `FreeRTOS API` · `POSIX pthreads` · `GCC` · `Make`
