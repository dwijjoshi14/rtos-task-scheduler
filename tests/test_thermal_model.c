/*
 * test_thermal_model.c — physics of the simulated plant.
 *
 * The plant is stepped with the analytic solution of a first-order ODE, so the
 * steady state, time constant and monotonicity are all predictable. These
 * tests pin the model down so the controller's behaviour can be reasoned about.
 */

#include "app_config.h"
#include "test_framework.h"
#include "thermal_model.h"

static AppConfig_t test_config(void) {
    AppConfig_t cfg;
    app_config_defaults(&cfg);
    cfg.ambient_c            = 25.0f;
    cfg.heater_power_w       = 25.0f;
    cfg.heat_capacity_j_per_k = 5.0f;
    cfg.conductance_w_per_k  = 0.35f;
    cfg.fan_gain_w_per_k     = 2.2f;
    cfg.burst_start_ms       = 6000;
    cfg.burst_end_ms         = 9500;
    cfg.burst_multiplier     = 6.0f;
    return cfg;
}

TEST_CASE(thermal, init_starts_at_ambient) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    CHECK_NEAR(plant.temp_c, 25.0f, 1e-6);
    CHECK_NEAR(plant.ambient_c, 25.0f, 1e-6);
}

TEST_CASE(thermal, steady_state_follows_conduction_balance) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    /* T_ss = T_amb + P / h, with h = G_amb + G_fan * duty */
    CHECK_NEAR(thermal_plant_steady_state(&plant, 0, 1.0f),
               25.0 + 25.0 / 0.35, 1e-3);
    CHECK_NEAR(thermal_plant_steady_state(&plant, 100, 1.0f),
               25.0 + 25.0 / 2.55, 1e-3);
    CHECK_NEAR(thermal_plant_steady_state(&plant, 50, 1.0f),
               25.0 + 25.0 / 1.45, 1e-3);
}

TEST_CASE(thermal, fan_lowers_the_steady_state) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    float off  = thermal_plant_steady_state(&plant, 0, 1.0f);
    float low  = thermal_plant_steady_state(&plant, 40, 1.0f);
    float high = thermal_plant_steady_state(&plant, 70, 1.0f);
    float max  = thermal_plant_steady_state(&plant, 100, 1.0f);

    CHECK(off > low);
    CHECK(low > high);
    CHECK(high > max);
}

TEST_CASE(thermal, time_constant_matches_capacity_over_conductance) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    CHECK_NEAR(thermal_plant_time_constant(&plant, 0), 5.0 / 0.35, 1e-4);
    CHECK_NEAR(thermal_plant_time_constant(&plant, 100), 5.0 / 2.55, 1e-4);
}

TEST_CASE(thermal, heating_is_monotonic_and_bounded) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    float previous = plant.temp_c;
    for (int i = 0; i < 200; i++) {
        float now = thermal_plant_step(&plant, 0, 1.0f, 0.2);
        CHECK(now >= previous - 1e-4f);
        previous = now;
    }
    /* Never overshoots the analytic equilibrium. */
    CHECK(plant.temp_c <= thermal_plant_steady_state(&plant, 0, 1.0f) + 1e-3f);
}

TEST_CASE(thermal, cooling_from_hot_converges_downwards) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);
    plant.temp_c = 90.0f;

    float steady = thermal_plant_steady_state(&plant, 100, 1.0f);
    for (int i = 0; i < 300; i++) {
        thermal_plant_step(&plant, 100, 1.0f, 0.2);
    }
    CHECK_NEAR(plant.temp_c, steady, 0.05);
    CHECK(plant.temp_c < 90.0f);
}

TEST_CASE(thermal, workload_burst_only_inside_window) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    CHECK_NEAR(thermal_plant_load(&plant, 0.0), 1.0f, 1e-6);
    CHECK_NEAR(thermal_plant_load(&plant, 5.999), 1.0f, 1e-6);
    CHECK_NEAR(thermal_plant_load(&plant, 6.0), 6.0f, 1e-6);
    CHECK_NEAR(thermal_plant_load(&plant, 9.0), 6.0f, 1e-6);
    CHECK_NEAR(thermal_plant_load(&plant, 9.5), 1.0f, 1e-6);
    CHECK_NEAR(thermal_plant_load(&plant, 30.0), 1.0f, 1e-6);
}

TEST_CASE(thermal, load_increases_steady_state) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    float idle = thermal_plant_steady_state(&plant, 70, 1.0f);
    float busy = thermal_plant_steady_state(&plant, 70, 6.0f);
    CHECK(busy > idle);
}

TEST_CASE(thermal, zero_dt_is_a_no_op) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);
    plant.temp_c = 42.0f;

    CHECK_NEAR(thermal_plant_step(&plant, 50, 1.0f, 0.0), 42.0f, 1e-6);
    CHECK_NEAR(plant.temp_c, 42.0f, 1e-6);
}

TEST_CASE(thermal, integration_is_step_size_independent) {
    AppConfig_t cfg = test_config();

    ThermalPlant_t coarse, fine;
    thermal_plant_init(&coarse, &cfg);
    thermal_plant_init(&fine, &cfg);

    for (int i = 0; i < 20; i++) thermal_plant_step(&coarse, 40, 1.0f, 1.0);
    for (int i = 0; i < 100; i++) thermal_plant_step(&fine, 40, 1.0f, 0.2);

    /* Closed-form integration: identical trajectories regardless of dt. */
    CHECK_NEAR(coarse.temp_c, fine.temp_c, 1e-3);
}

TEST_CASE(thermal, exhaustive_duty_is_clamped) {
    AppConfig_t cfg = test_config();
    ThermalPlant_t plant;
    thermal_plant_init(&plant, &cfg);

    /* A duty above 100% must not produce a negative conductance. */
    float steady = thermal_plant_steady_state(&plant, 255, 1.0f);
    CHECK(steady > cfg.ambient_c);
}
