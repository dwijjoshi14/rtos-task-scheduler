/*
 * test_control_logic.c — the control policy, in isolation from the RTOS.
 */

#include "app_config.h"
#include "control_logic.h"
#include "test_framework.h"

#include <math.h>

static Thresholds_t default_thresholds(void) {
    Thresholds_t thresholds = {
        .low_c = 30.0f,
        .high_c = 50.0f,
        .critical_c = 75.0f,
        .hysteresis_c = 2.0f,
    };
    return thresholds;
}

TEST_CASE(control, fan_state_threshold_mapping) {
    Thresholds_t t = default_thresholds();

    CHECK_INT(fan_state_for_temp(20.0f, &t), FAN_OFF);
    CHECK_INT(fan_state_for_temp(30.0f, &t), FAN_OFF);       /* boundary is exclusive */
    CHECK_INT(fan_state_for_temp(30.01f, &t), FAN_LOW);
    CHECK_INT(fan_state_for_temp(50.0f, &t), FAN_LOW);
    CHECK_INT(fan_state_for_temp(50.01f, &t), FAN_HIGH);
    CHECK_INT(fan_state_for_temp(75.0f, &t), FAN_HIGH);
    CHECK_INT(fan_state_for_temp(75.01f, &t), FAN_MAX);
    CHECK_INT(fan_state_for_temp(200.0f, &t), FAN_MAX);
}

TEST_CASE(control, duty_mapping) {
    CHECK_INT(fan_duty_pct(FAN_OFF), 0);
    CHECK_INT(fan_duty_pct(FAN_LOW), 40);
    CHECK_INT(fan_duty_pct(FAN_HIGH), 70);
    CHECK_INT(fan_duty_pct(FAN_MAX), 100);
    CHECK_INT(fan_duty_pct((FanState_t)99), 0);
}

TEST_CASE(control, state_names_are_stable) {
    CHECK_STR(fan_state_name(FAN_OFF), "OFF");
    CHECK_STR(fan_state_name(FAN_LOW), "LOW");
    CHECK_STR(fan_state_name(FAN_HIGH), "HIGH");
    CHECK_STR(fan_state_name(FAN_MAX), "MAX");
    CHECK_STR(sensor_fault_name(SENSOR_STUCK), "STUCK");
    CHECK_STR(sensor_fault_name(SENSOR_TIMEOUT), "TIMEOUT");
}

TEST_CASE(control, escalation_is_immediate) {
    Thresholds_t t = default_thresholds();

    /* A hot device must get full cooling at once, not one step per sample. */
    CHECK_INT(fan_next_state(FAN_OFF, 76.0f, &t), FAN_MAX);
    CHECK_INT(fan_next_state(FAN_OFF, 51.0f, &t), FAN_HIGH);
    CHECK_INT(fan_next_state(FAN_LOW, 76.0f, &t), FAN_MAX);
}

TEST_CASE(control, de_escalation_waits_for_guard_band) {
    Thresholds_t t = default_thresholds();

    /* Still inside the guard band: hold the current state. */
    CHECK_INT(fan_next_state(FAN_LOW, 29.0f, &t), FAN_LOW);
    CHECK_INT(fan_next_state(FAN_HIGH, 49.0f, &t), FAN_HIGH);
    CHECK_INT(fan_next_state(FAN_MAX, 74.0f, &t), FAN_MAX);

    /* Below threshold - hysteresis: step down exactly one level. */
    CHECK_INT(fan_next_state(FAN_LOW, 28.0f, &t), FAN_OFF);
    CHECK_INT(fan_next_state(FAN_HIGH, 48.0f, &t), FAN_LOW);
    CHECK_INT(fan_next_state(FAN_MAX, 73.0f, &t), FAN_HIGH);
}

TEST_CASE(control, de_escalation_is_one_step_at_a_time) {
    Thresholds_t t = default_thresholds();

    /* A sudden cold reading must not jump MAX straight to OFF. */
    CHECK_INT(fan_next_state(FAN_MAX, 0.0f, &t), FAN_HIGH);
    CHECK_INT(fan_next_state(FAN_HIGH, 0.0f, &t), FAN_LOW);
    CHECK_INT(fan_next_state(FAN_LOW, 0.0f, &t), FAN_OFF);
}

TEST_CASE(control, zero_hysteresis_still_steps_down) {
    Thresholds_t t = default_thresholds();
    t.hysteresis_c = 0.0f;

    CHECK_INT(fan_next_state(FAN_LOW, 30.0f, &t), FAN_OFF);
    CHECK_INT(fan_next_state(FAN_HIGH, 50.0f, &t), FAN_LOW);
}

TEST_CASE(control, policy_rejects_implausible_readings) {
    Thresholds_t t = default_thresholds();
    ControlPolicy_t policy;
    control_policy_init(&policy, &t);

    CHECK_INT(control_policy_check(&policy, 20.0f), SENSOR_OK);
    CHECK_INT(control_policy_check(&policy, 200.0f), SENSOR_OUT_OF_RANGE);
    CHECK_INT(control_policy_check(&policy, -100.0f), SENSOR_OUT_OF_RANGE);
    CHECK_INT(control_policy_check(&policy, NAN), SENSOR_OUT_OF_RANGE);
    CHECK_INT(control_policy_check(&policy, INFINITY), SENSOR_OUT_OF_RANGE);
}

TEST_CASE(control, policy_detects_stuck_sensor) {
    Thresholds_t t = default_thresholds();
    ControlPolicy_t policy;
    control_policy_init(&policy, &t);

    /* Moving readings are healthy. */
    for (int i = 0; i < 10; i++) {
        CHECK_INT(control_policy_check(&policy, 40.0f + (float)i), SENSOR_OK);
    }

    /* A latched value is reported once the run threshold is reached. */
    SensorFault_t fault = SENSOR_OK;
    for (uint32_t i = 0; i <= policy.stuck_samples; i++) {
        fault = control_policy_check(&policy, 55.0f);
    }
    CHECK_INT(fault, SENSOR_STUCK);
}

TEST_CASE(control, policy_reset_clears_detector) {
    Thresholds_t t = default_thresholds();
    ControlPolicy_t policy;
    control_policy_init(&policy, &t);

    for (uint32_t i = 0; i <= policy.stuck_samples; i++) {
        control_policy_check(&policy, 55.0f);
    }
    control_policy_reset(&policy);
    CHECK_INT(control_policy_check(&policy, 55.0f), SENSOR_OK);
}

TEST_CASE(control, policy_tolerates_normal_jitter) {
    Thresholds_t t = default_thresholds();
    ControlPolicy_t policy;
    control_policy_init(&policy, &t);

    /* ADC noise of a few tenths of a degree must not look stuck. */
    const float samples[] = { 45.00f, 45.31f, 44.87f, 45.19f, 45.02f, 44.95f };
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        CHECK_INT(control_policy_check(&policy, samples[i]), SENSOR_OK);
    }
}
