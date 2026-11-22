/*
 * thermal_model.c — closed-form lumped-capacitance plant.
 */

#include "thermal_model.h"

#include <math.h>

void thermal_plant_init(ThermalPlant_t* plant, const AppConfig_t* cfg) {
    plant->ambient_c         = cfg->ambient_c;
    plant->power_w           = cfg->heater_power_w;
    plant->capacity_j_per_k  = cfg->heat_capacity_j_per_k;
    plant->conductance_w_per_k = cfg->conductance_w_per_k;
    plant->fan_gain_w_per_k  = cfg->fan_gain_w_per_k;
    plant->burst_start_s     = (float)cfg->burst_start_ms / 1000.0f;
    plant->burst_end_s       = (float)cfg->burst_end_ms / 1000.0f;
    plant->burst_multiplier  = cfg->burst_multiplier;
    plant->temp_c            = cfg->ambient_c;
}

static float effective_conductance(const ThermalPlant_t* plant, uint8_t duty_pct) {
    float duty = (float)duty_pct / 100.0f;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    return plant->conductance_w_per_k + plant->fan_gain_w_per_k * duty;
}

float thermal_plant_load(const ThermalPlant_t* plant, double t_s) {
    double t = t_s;
    if (t >= (double)plant->burst_start_s && t < (double)plant->burst_end_s) {
        return plant->burst_multiplier;
    }
    return 1.0f;
}

float thermal_plant_time_constant(const ThermalPlant_t* plant, uint8_t duty_pct) {
    float h = effective_conductance(plant, duty_pct);
    if (h <= 0.0f) return 0.0f;
    return plant->capacity_j_per_k / h;
}

float thermal_plant_steady_state(const ThermalPlant_t* plant, uint8_t duty_pct,
                                 float load) {
    float h = effective_conductance(plant, duty_pct);
    if (h <= 0.0f) return plant->temp_c;
    return plant->ambient_c + plant->power_w * load / h;
}

float thermal_plant_step(ThermalPlant_t* plant, uint8_t duty_pct, float load,
                         double dt_s) {
    if (dt_s <= 0.0) return plant->temp_c;

    float h  = effective_conductance(plant, duty_pct);
    float ss = plant->ambient_c + plant->power_w * load / h;
    float tau = plant->capacity_j_per_k / h;

    /* Closed-form solution: stable for any step size. */
    float decay = expf((float)(-dt_s) / tau);
    plant->temp_c = ss + (plant->temp_c - ss) * decay;
    return plant->temp_c;
}
