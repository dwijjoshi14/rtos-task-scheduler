# Contributing

## Setup

Any C11 compiler and `make` will do; nothing else is required.

```bash
git clone https://github.com/dwijjoshi14/rtos-task-scheduler.git
cd rtos-task-scheduler
make test
```

## Definition of done

A change is ready when all of these hold locally:

```bash
make                                  # builds with -Wall -Wextra -Wpedantic -Werror
make test                             # 77 cases, 0 failures
make asan                             # ASan + UBSan + leak detection
make tsan                             # no data races
make analyze                          # GCC static analyzer, no diagnostics
```

Plus, for anything that touches the control loop:

```bash
./build/rtos_scheduler --duration 16000 --csv /tmp/t.csv --json /tmp/s.json
```

Read the summary: `deadline_overruns`, `mutex_violations` and `dropped_readings`
must stay at zero for the default configuration, and the transition count should
stay in single digits. A jump in transitions means the hysteresis band is too
narrow for the noise floor.

## Where code goes

| Change | Location |
|---|---|
| Threshold / hysteresis / health rules | `src/control_logic.c` (pure, no RTOS calls) |
| Plant physics | `src/thermal_model.c` |
| Signal conditioning | `src/signal_filter.c` |
| Tunables and their validation | `include/app_config.h`, `src/app_config.c` |
| Task orchestration only | `src/*_task.c` |
| Metrics fields and exporters | `src/metrics.c` |
| Kernel API behaviour | `src/FreeRTOS_sim.c` |

Two rules keep the design intact:

1. **No policy in task files.** If a task needs a decision, add a pure function
   to `control_logic.c` and call it. That is what makes the policy testable
   without a scheduler.
2. **No RTOS in policy files.** `control_logic.c`, `thermal_model.c`,
   `signal_filter.c` and `app_config.c` must not include `FreeRTOS.h`.

## Tests

Cases self-register; adding one is two lines:

```c
TEST_CASE(control, my_new_rule) {
    Thresholds_t thresholds = default_thresholds();
    CHECK_INT(fan_next_state(FAN_HIGH, 40.0f, &thresholds), FAN_LOW);
}
```

Put it in the suite that matches the module under test. Use `CHECK_NEAR` for
anything derived from the plant or a filter, never exact float equality.

## Commit style

Conventional Commits, imperative subject under ~72 characters, body explaining
*why* rather than restating the diff:

```
fix(control): detect stuck sensors on the raw signal

The stuck-value detector ran after the EMA, which smoothed a latched ADC into a
stream of plausible-looking changes. Health checks now run before filtering;
the control decision still uses the filtered value.
```

Prefixes in use: `feat`, `fix`, `build`, `test`, `docs`, `ci`, `refactor`,
`perf`, `chore`.

## Style

* C11, 4 spaces, no tabs.
* `-Werror` is not negotiable; do not silence a warning you do not understand.
* No dynamic allocation after startup in task code; the port's allocator is for
  kernel objects only.
* Every blocking call gets a timeout derived from configuration, never
  `portMAX_DELAY` in a task that also needs to observe shutdown.
* Comments explain constraints and consequences ("why"), not mechanics ("what").
