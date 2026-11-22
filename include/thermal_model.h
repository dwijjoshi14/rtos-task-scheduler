#pragma once
/*
 * thermal_model.h — first-order lumped-capacitance thermal plant.
 *
 * The sensor is not a scripted waveform: it measures a plant that is heated by
 * a constant power source and cooled by the fan the control task commands. The
 * control loop therefore has a real closed-loop response — overshoot, a
 * limit-cycle if the thresholds are too tight, and recovery after the workload
 * burst — instead of a decorative sine wave.
 *
 * Model (see docs/CONTROL_THEORY.md for the derivation):
 *
 *   C dT/dt = P·load(t) − h(duty)·(T − T_amb)
 *   h(duty)  = G_amb + G_fan·duty/100
 *
 * which has the analytic solution used for stepping:
 *
 *   T(t+dt) = T_ss + (T(t) − T_ss)·exp(−dt / τ)
 *   T_ss    = T_amb + P·load / h
 *   τ       = C / h
 *
 * Integrating in closed form makes the step unconditionally stable, so the
 * sensor period can be changed without changing the physics.
 */

#include <stdint.h>

#include "app_config.h"

typedef struct {
    float ambient_c;
    float power_w;
    float capacity_j_per_k;
    float conductance_w_per_k;
    float fan_gain_w_per_k;
    float burst_start_s;
    float burst_end_s;
    float burst_multiplier;
    float temp_c;
} ThermalPlant_t;

void  thermal_plant_init(ThermalPlant_t* plant, const AppConfig_t* cfg);

/* Workload multiplier at time t (1.0 outside the burst window). */
float thermal_plant_load(const ThermalPlant_t* plant, double t_s);

/* Equilibrium temperature for a duty cycle and load factor. */
float thermal_plant_steady_state(const ThermalPlant_t* plant, uint8_t duty_pct,
                                 float load);

/* Effective thermal time constant tau = C / h (seconds). */
float thermal_plant_time_constant(const ThermalPlant_t* plant, uint8_t duty_pct);

/* Advances the plant by dt_s seconds and returns the new temperature. */
float thermal_plant_step(ThermalPlant_t* plant, uint8_t duty_pct, float load,
                         double dt_s);
