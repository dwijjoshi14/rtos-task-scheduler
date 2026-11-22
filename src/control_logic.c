/*
 * control_logic.c — pure decision logic for the fan controller.
 */

#include "control_logic.h"

#include <math.h>

#define DEFAULT_MIN_PLAUSIBLE_C  (-40.0f)
#define DEFAULT_MAX_PLAUSIBLE_C  (125.0f)   /* TMP36 datasheet range */
#define DEFAULT_STUCK_SAMPLES    4u
#define DEFAULT_STUCK_EPSILON_C  0.0005f

void control_policy_init(ControlPolicy_t* policy, const Thresholds_t* thresholds) {
    policy->thresholds      = *thresholds;
    policy->min_plausible_c = DEFAULT_MIN_PLAUSIBLE_C;
    policy->max_plausible_c = DEFAULT_MAX_PLAUSIBLE_C;
    policy->stuck_epsilon_c = DEFAULT_STUCK_EPSILON_C;
    policy->stuck_samples   = DEFAULT_STUCK_SAMPLES;
    control_policy_reset(policy);
}

void control_policy_reset(ControlPolicy_t* policy) {
    policy->stuck_run   = 0;
    policy->last_temp_c = 0.0f;
    policy->have_last   = 0;
}

FanState_t fan_state_for_temp(float temp_c, const Thresholds_t* thresholds) {
    if (temp_c > thresholds->critical_c) return FAN_MAX;
    if (temp_c > thresholds->high_c)     return FAN_HIGH;
    if (temp_c > thresholds->low_c)      return FAN_LOW;
    return FAN_OFF;
}

FanState_t fan_next_state(FanState_t previous, float temp_c,
                          const Thresholds_t* thresholds) {
    FanState_t requested = fan_state_for_temp(temp_c, thresholds);

    if (requested > previous) return requested;   /* escalate immediately */
    if (requested == previous) return previous;

    /* De-escalate one step, only once the temperature is below the threshold
     * that would have requested the current state by a full guard band. */
    float exit_c;
    switch (previous) {
        case FAN_MAX:  exit_c = thresholds->critical_c; break;
        case FAN_HIGH: exit_c = thresholds->high_c;     break;
        case FAN_LOW:  exit_c = thresholds->low_c;      break;
        default:       return previous;
    }

    if (temp_c <= exit_c - thresholds->hysteresis_c) {
        return (FanState_t)((int)previous - 1);
    }
    return previous;
}

uint8_t fan_duty_pct(FanState_t state) {
    switch (state) {
        case FAN_OFF:  return 0;
        case FAN_LOW:  return 40;
        case FAN_HIGH: return 70;
        case FAN_MAX:  return 100;
        default:       return 0;
    }
}

const char* fan_state_name(FanState_t state) {
    switch (state) {
        case FAN_OFF:  return "OFF";
        case FAN_LOW:  return "LOW";
        case FAN_HIGH: return "HIGH";
        case FAN_MAX:  return "MAX";
        default:       return "?";
    }
}

const char* sensor_fault_name(SensorFault_t fault) {
    switch (fault) {
        case SENSOR_OK:           return "OK";
        case SENSOR_STUCK:        return "STUCK";
        case SENSOR_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case SENSOR_TIMEOUT:      return "TIMEOUT";
        default:                  return "?";
    }
}

SensorFault_t control_policy_check(ControlPolicy_t* policy, float temp_c) {
    /* A NaN would silently fail every comparison below. */
    if (!isfinite(temp_c)) {
        return SENSOR_OUT_OF_RANGE;
    }
    if (temp_c < policy->min_plausible_c || temp_c > policy->max_plausible_c) {
        policy->have_last = 0;
        return SENSOR_OUT_OF_RANGE;
    }

    if (policy->have_last &&
        fabsf(temp_c - policy->last_temp_c) < policy->stuck_epsilon_c) {
        policy->stuck_run++;
        policy->last_temp_c = temp_c;
        if (policy->stuck_samples > 0 && policy->stuck_run >= policy->stuck_samples) {
            return SENSOR_STUCK;
        }
        return SENSOR_OK;
    }

    policy->stuck_run   = 0;
    policy->last_temp_c = temp_c;
    policy->have_last   = 1;
    return SENSOR_OK;
}
