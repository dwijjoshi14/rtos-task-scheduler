# Control theory notes

## Plant model

The device is modelled as a single thermal mass at temperature `T` with heat
capacity `C`, losing heat to ambient `T∞` through a conductance `h` that depends
on the fan duty, and gaining heat from a workload of power `P`:

```
C·dT/dt = P·load(t) − h(duty)·(T − T∞)
h(duty) = G_amb + G_fan·duty/100
```

This is a first-order linear ODE. For constant `duty` and `load` it has the exact
solution

```
T_ss = T∞ + P·load / h            (steady state)
τ    = C / h                      (time constant)
T(t + Δt) = T_ss + (T(t) − T_ss)·e^(−Δt/τ)
```

`thermal_model_step()` uses that closed form rather than an Euler step. Two
consequences matter:

* the integration is **unconditionally stable**, so `--sensor-period` can be
  changed without changing the physics (asserted by
  `thermal/integration_is_step_size_independent`);
* the steady state and time constant are analytically checkable, which is how
  they are tested.

### Default parameters

| Symbol | Value | Meaning |
|---|---|---|
| `T∞` | 25 °C | ambient |
| `P` | 25 W | idle workload |
| `C` | 5 J/K | thermal capacity |
| `G_amb` | 0.35 W/K | ambient coupling |
| `G_fan` | 2.2 W/K | extra coupling at 100 % duty |
| burst | ×6 for 6.0–9.5 s | workload profile |

Equilibrium temperatures:

| Duty | `h` (W/K) | `τ` (s) | `T_ss` idle | `T_ss` burst |
|---|---|---|---|---|
| 0 % | 0.35 | 14.3 | 96.4 °C | 453.6 °C |
| 40 % | 1.23 | 4.1 | 45.3 °C | 147.0 °C |
| 70 % | 1.89 | 2.6 | 38.2 °C | 104.4 °C |
| 100 % | 2.55 | 2.0 | 34.8 °C | 83.8 °C |

Two design facts fall out of this table:

* **The fan alone cannot reject the burst.** At full duty the burst equilibrium
  is 83.8 °C, above the 75 °C critical threshold. The controller therefore drives
  the fan to `MAX` and the temperature still climbs — the correct physical
  answer, and the reason the run shows a critical alert followed by a slow
  recovery once the load is removed at 9.5 s.
* **70 % duty cannot hold the burst either** (104.4 °C), so the escalation from
  `HIGH` to `MAX` is genuinely required, not decorative.

## Controller

The actuator has four discrete states. Mapping is a pure function of the
temperature with exclusive boundaries:

```
T > 75 → MAX (duty 100)   T > 50 → HIGH (70)   T > 30 → LOW (40)   else OFF (0)
```

### Hysteresis

Instantaneous mapping on a noisy signal chatters: a reading hovering at 50 °C
would toggle the relay every sample. The policy is therefore asymmetric:

```
escalation   : immediate, any number of steps up
de-escalation: one step, only when T ≤ threshold(current) − hysteresis
```

`threshold(FAN_MAX) = 75`, `threshold(FAN_HIGH) = 50`, `threshold(FAN_LOW) = 30`.
With the default 2 °C band, `HIGH` is entered above 50 °C and left below 48 °C.

Measured, verbatim from a default run:

| Transition | Reported temperature | Rule |
|---|---|---|
| OFF → LOW | 30.3 °C | > 30 |
| LOW → HIGH | 52.6 °C | > 50 |
| HIGH → MAX | 75.9 °C | > 75 |
| MAX → HIGH | 71.4 °C | ≤ 75 − 2 + filter lag |
| HIGH → LOW | 48.0 °C | ≤ 50 − 2 |

De-escalation happens a little below `threshold − hysteresis` because the
decision uses the EMA-filtered value, which lags the plant by roughly
`(1 − α)/α` samples (α = 0.35 → about two samples, 0.4 s). That lag is visible
in the reported numbers and is the price of noise rejection; lowering `--alpha`
trades responsiveness for smoothness.

### Signal conditioning

The EMA is `y[n] = α·x[n] + (1−α)·y[n−1]`, with α = 0.35 by default. It is
seeded with the first sample rather than zero — otherwise the estimate would
start at 0 °C and take `1/α` samples to climb out, which looks exactly like a
sensor fault.

The filter is deliberately **not** used for health checks. A latched sensor
produces a perfectly constant raw value; after filtering, consecutive values
still drift slightly toward the plant, so a filtered stuck-detector never fires.
`ControlTask` therefore checks the raw signal and controls on the filtered one.

## Tuning guide

Given a plant, choose the thresholds from its steady states:

1. Pick the duty you are willing to run at steady state for the quiet workload
   (`LOW` = 40 % here, 45.3 °C) and set `low` well below it (30 °C) so the fan
   stays off while the device is cold.
2. Set `high` below the `HIGH` equilibrium but above the `LOW` equilibrium
   (50 °C sits between 45.3 and 38.2), so the loop settles without oscillating.
3. Set `critical` below the ambient-cooled limit of the largest load you must
   survive, and accept that a load larger than the fan can reject will saturate.
4. Set `hysteresis` to cover your noise floor: with ±0.35 °C noise after
   filtering, 2 °C is comfortably larger than the residual ripple.

Validate with:

```bash
./build/rtos_scheduler --thresholds 28,45,70 --hysteresis 1 --duration 20000 \
                       --csv telemetry.csv --json summary.json
```

and check `transitions` in the summary: a well-tuned loop has a handful. A loop
with dozens is chattering — either the band is too narrow or `alpha` is too high.

## What this model deliberately omits

* Sensor dynamics (the TMP36 responds in milliseconds, far below the 200 ms
  sample period).
* Fan spin-up and the resulting actuator dead time.
* Ambient changes, radiation, and spatial temperature gradients inside the
  device.
* Quantisation of the 12-bit ADC reading.

These would matter for a production controller; the point of this model is that
the *structure* (first-order, saturating actuator, load disturbance) is faithful,
so the control logic is exercised the way it would be on hardware.
