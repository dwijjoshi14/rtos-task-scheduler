# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [2.0.0] — 2026-11-22

A rewrite of the simulation into a closed-loop control system. The 1.x
implementation described a temperature control system but did not actually run
one; the sections below record what changed and why.

### Fixed

- **Task periods never elapsed.** `SensorTask` initialised `xLastWakeTime` to 0,
  so `vTaskDelayUntil` computed a deadline in the past and returned immediately.
  The whole 30-sample "15 second" demo completed in well under a millisecond and
  every sample carried the same timestamp. The wake time is now seeded from
  `xTaskGetTickCount()` and the port reports missed deadlines instead of
  silently skipping them.
- **Timed waits mixed clocks.** Queue and semaphore timeouts were computed
  against `CLOCK_REALTIME` while elapsed time used `CLOCK_MONOTONIC`, so a wall
  clock adjustment could shorten or extend a wait. Condition variables are now
  created with a monotonic clock attribute and deadlines come from the same
  source.
- **Mutex ownership was not enforced.** `xSemaphoreGive` on a mutex succeeded
  from any thread and a nested take re-entered silently. The port now tracks the
  owner, refuses both cases and counts the violation.
- **Stuck-sensor detection ran on the filtered signal**, where the EMA turns a
  latched value into a plausible-looking sequence of small changes. Health checks
  moved in front of the filter.
- **No sensor liveness watchdog.** A producer that stopped publishing left the
  controller blocked forever. The receive now has a configurable timeout and the
  actuator holds its last state on timeout.
- **`usleep` was used without including `<unistd.h>`**, so the project did not
  compile warning-free under `-Werror`.
- **README overstated the system.** It advertised a closed loop and
  priority-based scheduling that the code did not implement; both are now
  documented accurately, including the deviations.

### Added

- First-order lumped-capacitance thermal plant with a workload burst, integrated
  in closed form so the physics is independent of the sample period.
- Pure, unit-testable control policy with asymmetric hysteresis (immediate
  escalation, guarded one-step de-escalation).
- EMA and boxcar filters; the sensor reports raw and filtered values so the
  filter can be evaluated offline from the CSV.
- Deterministic seeded noise, so a failing run can be reproduced exactly.
- Runtime configuration for every tunable, with validation that rejects
  descending thresholds, alpha outside (0, 1], a watchdog shorter than the
  sensor period, and non-numeric input.
- Telemetry: aggregate metrics, an ASCII report with a temperature histogram,
  per-sample CSV and a JSON summary, plus scheduler diagnostics (deadline
  overruns, mutex violations, timed-out waits, live allocations).
- Fault injection (`--stuck-at`, `--stall-at`) and fail-safe actuator behaviour
  with once-per-episode fault reporting.
- Graceful SIGINT handling: the run stops, reports and exits instead of
  truncating.
- Self-registering test suite: 77 cases / 494 checks over the port, control
  policy, plant, filters, configuration and telemetry.
- Build targets for `debug`, `asan`, `tsan` and `analyze`, plus incremental
  compilation with header dependency tracking.
- GitHub Actions pipeline: gcc and clang builds, unit tests, telemetry
  validation, both sanitizers, static analysis and fault-injection checks.
- Documentation: architecture and concurrency invariants, control theory and
  tuning guide, STM32 porting guide.

### Changed

- Kernel objects are allocated through `pvPortMalloc`, so leak checks observe
  the same allocator the port uses.
- The display task is explicitly the lowest priority and the only task allowed
  to fall behind; queue back-pressure is counted rather than blocking the
  producer.
- Default run is 16 s with a 200 ms cadence, long enough to show escalation,
  saturation at ~81 °C, a critical alert and recovery.

## [1.0.0] — 2025-06-01

Initial release: three tasks, two queues, a binary semaphore and a console mutex,
with a scripted temperature ramp.
