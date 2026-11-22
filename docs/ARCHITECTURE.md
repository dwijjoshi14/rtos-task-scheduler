# Architecture

## Goals

1. **Task logic must be portable.** Application code uses only FreeRTOS
   primitives; everything host-specific lives behind `FreeRTOS.h`.
2. **Decisions must be testable without threads.** All policy lives in pure
   functions that the suite can call directly.
3. **Failures must be observable.** Every dropped reading, expired wait, miss
   and fault is counted, not printed and forgotten.
4. **A simulation that hides timing bugs is worse than no simulation.** The port
   reports lateness instead of smoothing it over.

## Layers

```
                 ┌─────────────────────────────────────────┐
  application    │ sensor_task  control_task  display_task │
                 ├─────────────────────────────────────────┤
  policy/domain  │ control_logic · thermal_model · filter  │   pure, no RTOS
                 │ app_config · metrics                    │
                 ├─────────────────────────────────────────┤
  platform       │ FreeRTOS.h / FreeRTOS_sim.c (pthreads)  │
                 └─────────────────────────────────────────┘
```

The arrow of dependency points down only. `control_logic.c` does not include
`FreeRTOS.h`; `thermal_model.c` does not know a task exists. This is what keeps
the policy unit-testable and the port replaceable.

| Module | Owns | Must not |
|---|---|---|
| `FreeRTOS_sim.c` | threads, condvars, allocator, diagnostics | contain application policy |
| `runtime.c` | run-wide stop flag, start tick, fan-duty publication | contain blocking calls |
| `app_config.c` | parsing and validation of tunables | touch the RTOS |
| `thermal_model.c` | plant physics | read globals or clocks |
| `signal_filter.c` | filter state | allocate |
| `control_logic.c` | thresholds, hysteresis, health rules | print, block, allocate |
| `metrics.c` | aggregation, exporters | block for long periods |
| `*_task.c` | orchestration between kernel objects | contain policy decisions |

## Data flow

1. `SensorTask` wakes on its deadline (`vTaskDelayUntil`), reads the duty
   published by `ControlTask`, steps the plant with the measured `Δt`, adds
   seeded noise, filters, and pushes a `SensorReading_t` (raw **and** filtered).
2. `ControlTask` blocks on `xSensorQueue` with a watchdog timeout. It checks
   plausibility and stuck-value rules on the raw value, applies the hysteresis
   policy to the filtered value, publishes the new duty atomically, records
   metrics, then forwards a `DisplayMsg_t`.
3. `DisplayTask` polls `xAlertSemaphore` non-blockingly, renders the message,
   appends a CSV row and records the latency from sample timestamp to render.

The duty is published through `g_fan_duty_pct`, an `atomic_int`. The sensor task
reads it at the start of each period, so the loop has one sample of actuation
delay — the same as a real PWM register written by a controller task.

## Concurrency invariants

| Object | Holder | Invariant |
|---|---|---|
| `xConsoleMutex` | any task printing | non-recursive; never taken while holding a queue lock |
| `xSensorQueue` | one producer, one consumer | full queue drops a reading rather than blocking the producer |
| `xDisplayQueue` | one producer, one consumer | display falls behind, never the controller |
| `xAlertSemaphore` | given by control, taken by display | binary; a second give while set is ignored |
| `g_fan_duty_pct` | written by control, read by sensor | atomic, no tearing, no lock |
| `g_sim_stop` | any task, and `main`'s report path | atomic; idempotent |
| `g_sigint` | signal handler only | `volatile sig_atomic_t`, only stored |
| metrics state | any task | private mutex; snapshots are consistent |

Rules the code follows and the tests enforce:

* No task ever gives a mutex it does not own (the port would refuse and count it).
* No blocking call happens while a mutex is held, except the console mutex, which
  is only held across a single `printf`.
* Nothing sleeps in a loop waiting for a flag: every wait has a timeout derived
  from the configuration, so shutdown always completes within one period.

## Shutdown sequence

```
deadline reached / --max-samples / SIGINT
        │
        ├─ SensorTask:   checks flag each period, exits after its current wait
        ├─ ControlTask:  blocking receive has a watchdog timeout, then re-checks
        └─ DisplayTask:  blocking receive has the display period as its timeout
                 │
        every task exits → vTaskStartScheduler joins them → main prints the report
```

`main` publishes `g_sim_start_ms` **before** creating any task: every task
measures elapsed time against it from its first iteration. ThreadSanitizer
caught this as a data race when the order was the other way round, which is why
the clock is both published early and atomic.

## Why the port reports lateness

On target hardware `vTaskDelayUntil` is exact to the tick because the scheduler
controls execution. On a host, the OS may deschedule a thread for tens of
milliseconds. Silently subtracting that lateness — the common shortcut of
resetting the wake time to `now` — hides the very thing the simulation exists to
exercise.

The port therefore keeps the real semantics: the deadline is absolute, a task
that arrives late is counted (`deadline_overruns`, `max_overrun_ms`) and the next
period is scheduled from the original deadline, never from the wake time. The
report surfaces the numbers; the default run shows zero.

## Extending the system

* **New sensor** — add a module next to `thermal_model.c`, have `SensorTask` read
  it, keep the `SensorReading_t` contract.
* **New actuator** — put the mapping in `control_logic.c` (pure) and the write in
  `ControlTask`.
* **New telemetry field** — add it to `MetricsSnapshot_t`, record it under the
  metrics mutex, export it in both JSON and CSV, and assert on it in `metrics`
  tests.
* **A different policy** (PI, model predictive) — implement it as a pure
  function next to `fan_next_state` so it inherits the existing test harness.
