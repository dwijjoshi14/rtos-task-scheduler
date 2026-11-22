#pragma once
/*
 * control_logic.h — the control policy, free of any RTOS dependency.
 *
 * Every decision ControlTask makes lives in these pure functions so it can be
 * unit-tested on the host without threads, timers or a simulated plant. The
 * task itself only moves data between kernel objects.
 *
 * Hysteresis
 * ----------
 * Escalation is immediate (a hot device must get more cooling now), but
 * de-escalation happens one step at a time and only after the temperature has
 * dropped a full guard band below the threshold that caused the escalation.
 * Without that band, a reading sitting on a threshold flips the fan every
 * sample: audible relay chatter on hardware, and pointless telemetry noise.
 */

#include <stdint.h>

#include "app_config.h"
#include "tasks_common.h"

/* Sensor plausibility limits and stuck-reading detector state. */
typedef struct {
    Thresholds_t thresholds;
    float        min_plausible_c;
    float        max_plausible_c;
    float        stuck_epsilon_c;
    uint32_t     stuck_samples;   /* identical readings tolerated */
    uint32_t     stuck_run;       /* detector state             */
    float        last_temp_c;
    int          have_last;
} ControlPolicy_t;

void control_policy_init(ControlPolicy_t* policy, const Thresholds_t* thresholds);

/* State for a temperature with no hysteresis (instantaneous mapping). */
FanState_t  fan_state_for_temp(float temp_c, const Thresholds_t* thresholds);

/* State for a temperature given the previous state, applying hysteresis. */
FanState_t  fan_next_state(FanState_t previous, float temp_c,
                           const Thresholds_t* thresholds);

/* PWM duty for a state, as a percentage. */
uint8_t     fan_duty_pct(FanState_t state);

const char* fan_state_name(FanState_t state);
const char* sensor_fault_name(SensorFault_t fault);

/*
 * Checks one reading against plausibility and stuck-value rules.
 * Returns SENSOR_OK or the detected fault, and updates the detector state.
 */
SensorFault_t control_policy_check(ControlPolicy_t* policy, float temp_c);

/* Clears detector state, e.g. after a watchdog timeout. */
void control_policy_reset(ControlPolicy_t* policy);
