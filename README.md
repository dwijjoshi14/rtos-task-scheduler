# RTOS Task Scheduler — closed-loop FreeRTOS temperature control

![CI](https://github.com/dwijjoshi14/rtos-task-scheduler/actions/workflows/ci.yml/badge.svg)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C11](https://img.shields.io/badge/C-11-informational)
![tests](https://img.shields.io/badge/tests-77%20cases%20%2F%20494%20checks-success)

A three-task FreeRTOS application that regulates the temperature of a simulated
device. The task logic is written against the standard FreeRTOS API and runs on
Linux, macOS or WSL through a POSIX port (`pthreads`), so the same sources build
for an STM32 target.

The interesting part is that the sensor is **not** a scripted waveform. It
samples a first-order thermal plant that is heated by a workload and cooled by
the fan the controller commands, so the system exhibits real closed-loop
behaviour: overshoot, saturation when the load exceeds the fan's capacity, a
critical alert, and recovery when the load drops.

```
                     ┌──────────────────────────────────────────┐
   workload burst ──►│  thermal plant  C·dT/dt = P·load − h·(T−T∞) │
                     └───────────────────┬──────────────────────┘
                                         │ temperature
                                   ┌─────▼──────┐  SensorReading_t
                                   │ SensorTask │  (raw + EMA filtered)
                                   │   P3/200ms │
                                   └─────┬──────┘
                                  xSensorQueue (depth 8)
                                   ┌─────▼──────┐
                                   │ControlTask │  hysteresis policy,
                                   │   P4/event │  watchdog, health checks
                                   └──┬──────┬──┘
              xAlertSemaphore (bin)   │      │  xDisplayQueue (depth 8)
                    ┌─────────────────▼┐   ┌─▼──────────┐
                    │  critical alert  │   │DisplayTask │  P2/200ms
                    └──────────────────┘   └────────────┘
                                   │
                     g_fan_duty_pct (atomic) ──► SensorTask  (loop closes)
```

## Tasks and kernel objects

| Task | Priority | Cadence | Responsibility |
|---|---|---|---|
| `SensorTask` | 3 | 200 ms, `vTaskDelayUntil` | Step the thermal plant, add seeded noise, EMA-filter, publish readings |
| `ControlTask` | 4 (highest) | event driven, 1 s watchdog | Hysteresis state machine, fan duty, alerts, sensor health, telemetry |
| `DisplayTask` | 2 | 200 ms poll | LCD rendering, alert banner, CSV streaming, sample-to-display latency |

| Object | Type | Purpose |
|---|---|---|
| `xSensorQueue` | Queue, depth 8 | `SensorReading_t`: raw plant value, filtered value, timestamp, sequence |
| `xDisplayQueue` | Queue, depth 8 | `DisplayMsg_t`: state, duty, critical flag, fault code |
| `xAlertSemaphore` | Binary semaphore | Rising-edge critical alert from control → display |
| `xConsoleMutex` | Mutex | Serialises console output (non-recursive, ownership enforced by the port) |

## Control loop

The fan has four states with a 2 °C guard band; escalation is immediate,
de-escalation takes one step at a time and only after the temperature has
fallen a full guard band below the threshold that caused the escalation. That
band is what stops a sensor sitting on a boundary from flapping the relay every
sample.

| Reading | State | Duty |
|---|---|---|
| ≤ 30 °C | `OFF` | 0 % |
| > 30 °C | `LOW` | 40 % |
| > 50 °C | `HIGH` | 70 % |
| > 75 °C | `MAX` | 100 % + critical alert |

The plant is a lumped-capacitance model integrated in closed form, so changing
the sample period does not change the physics:

```
C·dT/dt = P·load(t) − h(duty)·(T − T∞)
h(duty) = G_amb + G_fan·duty/100
T(t+Δt) = T_ss + (T(t) − T_ss)·e^(−Δt/τ),   T_ss = T∞ + P·load/h,   τ = C/h
```

With the default parameters the equilibrium temperatures are:

| Duty | `h` (W/K) | `T_ss` at idle (25 W) | `T_ss` during burst (150 W) |
|---|---|---|---|
| 0 % | 0.35 | 96.4 °C | 453.6 °C |
| 40 % | 1.23 | 45.3 °C | 147.0 °C |
| 70 % | 1.89 | 38.2 °C | 104.4 °C |
| 100 % | 2.55 | 34.8 °C | 83.8 °C |

The burst is deliberately stronger than the fan can reject at 70 % duty, which
is why the controller must escalate to `MAX` and still saturates around 84 °C —
and why recovery only completes once the workload ends. See
[docs/CONTROL_THEORY.md](docs/CONTROL_THEORY.md) for the derivation and a tuning
guide.

## Measured behaviour

Default run (`./build/rtos_scheduler`, seed 2026, 16 s, 200 ms cadence), verbatim
from the summary report:

```
[ControlTask] fan OFF → LOW  (30.3 C, duty 40%)
[ControlTask] fan LOW → HIGH  (52.6 C, duty 70%)
[ControlTask] fan HIGH → MAX  (75.9 C, duty 100%)

  +--------------------------------------+
  |   CRITICAL TEMPERATURE - FAN AT MAX  |
  |   plant above the critical limit     |
  +--------------------------------------+

[  8.2s]  75.9C  MAX  100%  [##########] ▃▃▃▃▄▄▄▅▅▅▆▆▆▆▇
[  9.4s]  80.8C  MAX  100%  [##########] ▄▄▅▅▅▆▆▆▆▇▇▇▇▇▇▇
[ControlTask] fan MAX → HIGH  (71.4 C, duty 70%)
[ControlTask] fan HIGH → LOW  (48.0 C, duty 40%)
```

```
  run summary
  ───────────
  window           15.60 s  (79 samples, 200 ms cadence)
  temperature     min  25.9 C · mean  50.1 C · max  80.8 C
  fan             5 transitions · 1 alert(s) · 13% of samples critical
  dwell           OFF 8% · LOW 49% · HIGH 30% · MAX 13%
  health          0 fault(s) · 0 timeout(s) · 0 reading(s) dropped · queue high-water 1/8
  latency         mean 0.1 ms · max 1 ms (sample → display)
  scheduler       0 deadline overrun(s) · worst 0 ms · 0 mutex violation(s) · 3 task(s)
```

State transitions land exactly where the hysteresis rule says they should:
escalation on the raw crossing (30.3, 52.6, 75.9) and de-escalation one rung at
a time below `threshold − hysteresis` (73.0, 48.0).

## Build, test, run

Requirements: any C11 compiler and pthreads (`gcc`, `clang`).

```bash
make            # release build → build/rtos_scheduler
make run        # run with default configuration
make test       # build and run the unit suite
make asan       # AddressSanitizer + UBSan + leak detection, tests and a run
make tsan       # ThreadSanitizer, tests and a run
make analyze    # GCC static analyzer over the whole program
make debug      # -O0 -g3
make clean
```

```bash
./build/rtos_scheduler --help
./build/rtos_scheduler --duration 20000 --seed 7 --csv telemetry.csv --json summary.json
./build/rtos_scheduler --thresholds 25,40,60 --hysteresis 3 --quiet
./build/rtos_scheduler --burst 8 --burst-start 5000 --burst-end 9000
./build/rtos_scheduler --stuck-at 20        # inject a latched sensor
```

| Option | Default | Meaning |
|---|---|---|
| `--duration <ms>` | 16000 | run length |
| `--sensor-period <ms>` | 200 | `SensorTask` period |
| `--display-period <ms>` | 200 | `DisplayTask` period |
| `--watchdog <ms>` | 1000 | sensor liveness timeout in `ControlTask` |
| `--thresholds <lo,hi,cr>` | 30,50,75 | fan thresholds (°C) |
| `--hysteresis <C>` | 2.0 | de-escalation guard band |
| `--alpha <0..1>` | 0.35 | sensor EMA coefficient |
| `--ambient`, `--power`, `--capacity`, `--conductance`, `--fan-gain` | 25, 25, 5, 0.35, 2.2 | plant parameters |
| `--burst-start`, `--burst-end`, `--burst <x>` | 6000, 9500, 6 | workload burst window and multiplier |
| `--noise <C>`, `--seed <n>` | 0.35, 2026 | sensor noise and its deterministic seed |
| `--stuck-at <n>` | 0 | fault injection: latched sensor from sample `n` |
| `--stall-at <n>` | 0 | fault injection: sensor stops publishing from sample `n` |
| `--csv <path>`, `--json <path>` | off | telemetry exports |
| `--quiet`, `--no-color` | off | suppress per-sample lines / ANSI colour |

Ctrl+C produces a full report and a clean shutdown instead of truncating the
run.

## Failure handling

The controller never acts on a reading it does not trust:

| Condition | Detection | Action |
|---|---|---|
| Silent or stalled producer | `xQueueReceive` exceeds `--watchdog` | hold last actuator state, publish `TIMEOUT` |
| Latched ADC | 4 consecutive samples within 0.0005 °C | hold state, publish `STUCK` |
| Implausible value | outside −40…125 °C, or NaN/Inf | hold state, publish `OUT_OF_RANGE` |
| Queue back-pressure | `xQueueSend` fails inside 10 ms | count the drop, keep the loop running |

Health checks run on the **raw** signal, before the EMA: filtering would smooth a
stuck sensor into a plausible-looking stream of changes and hide the fault.
`--stuck-at 20` exercises the whole path end to end — one fault line, the fan
held in fail-safe, and `STUCK` in the CSV from that sample onward.
`--stall-at 8 --watchdog 600` does the same for a producer that goes quiet:

```
[  2.0s] sensor fault: TIMEOUT      holding fan LOW (fail-safe)
  health          0 fault(s) · 4 timeout(s) · 0 reading(s) dropped · queue high-water 1/8
```

Both paths are exercised by CI on every push.

## Telemetry

* **CSV** — one row per rendered sample:
  `sequence,elapsed_ms,raw_c,filtered_c,fan_state,duty_pct,critical,fault`
* **JSON** — configuration, aggregate metrics and scheduler diagnostics, suitable
  for CI assertions:

```json
{
  "schema": 1,
  "app": {"name": "rtos-task-scheduler", "version": "2.0.0"},
  "metrics": {
    "window_s": 15.600, "samples": 79, "transitions": 5, "alerts": 1,
    "temperature_c": {"min": 25.882, "mean": 50.105, "max": 80.823},
    "dwell_pct": {"off": 7.59, "low": 49.36, "high": 30.39, "max": 12.66}
  },
  "diagnostics": {"tasks_created": 3, "deadline_overruns": 0, "mutex_violations": 0}
}
```

## Testing

```
77 case(s), 494 check(s), 0 failure(s)
```

| Suite | Covers |
|---|---|
| `freertos` | FIFO order, bounded capacity, blocking timeouts, peek/reset, binary and counting semaphores, non-recursive mutexes, cross-thread ownership, deadline arithmetic, task join, allocator round-trip |
| `control` | threshold boundaries, immediate escalation, guarded de-escalation, invalid readings, stuck-sensor detector |
| `thermal` | analytic steady state and time constant, monotonic heating, convergence, step-size independence, workload window |
| `filter` | EMA seeding and convergence, noise rejection, boxcar window arithmetic |
| `config` | every option, and rejection of descending thresholds, bad alpha, watchdog shorter than the period, non-numeric values |
| `metrics` | aggregation, dwell accounting, JSON shape, CSV round-trip |

The suite is also run under ASan/UBSan/LSan and TSan, and the GCC static analyzer
runs over the whole program; all are clean.

## Project layout

```
include/
  FreeRTOS.h          POSIX port: tasks, queues, semaphores, heap, diagnostics
  tasks_common.h      shared packets, priorities, run-control handshake
  app_config.h        every runtime tunable
  thermal_model.h     first-order plant
  signal_filter.h     EMA + boxcar filters
  control_logic.h     pure control policy (no RTOS dependency)
  metrics.h           telemetry aggregation and exporters
  sensor_task.h control_task.h display_task.h
src/                  one file per module, plus runtime.c (shared run state)
tests/                self-registering harness and six suites
docs/                 ARCHITECTURE.md · CONTROL_THEORY.md · PORTING.md
.github/workflows/    CI: build, test, sanitizers, static analysis
```

Design notes, module boundaries and the concurrency invariants live in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Porting to real hardware, including
the API and driver mapping, is in [docs/PORTING.md](docs/PORTING.md).

## Deliberate deviations from the kernel

These are documented rather than hidden, because they change what the
simulation can prove:

* **Ticks are milliseconds** (`configTICK_RATE_HZ` = 1000 in the port).
* **Priorities are recorded, not enforced.** The port models the FreeRTOS API on
  top of pthreads; the host scheduler decides who runs. That is precisely why
  the app measures deadline overruns and sample-to-display latency instead of
  assuming real-time behaviour.
* **`vTaskStartScheduler()` returns** once every task has exited, which is what
  lets `main` print a report. On target hardware it never returns.
* **Mutex misuse is refused, not asserted**: a give or nested take by a
  non-owner returns `pdFAIL` and increments a diagnostic counter.

## License

MIT — see [LICENSE](LICENSE).
