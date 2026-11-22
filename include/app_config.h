#pragma once
/*
 * app_config.h — runtime configuration.
 *
 * Every tunable in the system lives here and is settable from the command
 * line, so the control loop can be characterised without recompiling. Defaults
 * are the ones documented in docs/CONTROL_THEORY.md.
 */

#include <stdint.h>
#include <stdio.h>

#define APP_NAME    "rtos-task-scheduler"
#define APP_VERSION "2.0.0"

/* Fan thresholds, in degrees Celsius. */
typedef struct {
    float low_c;        /* above this: FAN_LOW (below: FAN_OFF)          */
    float high_c;       /* above this: FAN_HIGH                         */
    float critical_c;   /* above this: FAN_MAX and a critical alert     */
    float hysteresis_c; /* downshift guard band, avoids fan chatter     */
} Thresholds_t;

typedef struct {
    /* Task scheduling */
    uint32_t sensor_period_ms;
    uint32_t display_period_ms;
    uint32_t watchdog_ms;      /* ControlTask liveness timeout for readings */

    /* Run control */
    uint32_t run_duration_ms;
    uint32_t max_samples;      /* 0 = only bounded by run_duration_ms */
    uint32_t seed;             /* deterministic noise generator       */

    /* Signal conditioning */
    Thresholds_t thresholds;
    float ema_alpha;           /* 0 < alpha <= 1, 1 = no filtering    */
    float noise_amplitude_c;   /* uniform ±amplitude, 0 = clean plant */

    /* Thermal plant (see docs/CONTROL_THEORY.md) */
    float ambient_c;
    float heater_power_w;
    float heat_capacity_j_per_k;
    float conductance_w_per_k;
    float fan_gain_w_per_k;    /* extra conductance at 100% duty */

    /* Workload burst: multiplies heater power between start and end */
    uint32_t burst_start_ms;
    uint32_t burst_end_ms;
    float    burst_multiplier;

    /* Fault injection */
    uint32_t stuck_at_sample;   /* latch the sensor from this sample onward  */
    uint32_t stall_at_sample;   /* stop publishing from this sample onward   */

    /* Output */
    const char* csv_path;      /* NULL = disabled */
    const char* json_path;     /* NULL = disabled */
    int  use_color;
    int  verbose;              /* print one LCD line per sample */
} AppConfig_t;

/* Parse results */
#define APP_CONFIG_OK    0
#define APP_CONFIG_ERROR 1
#define APP_CONFIG_EXIT  2   /* --help/--version handled, exit successfully */

void app_config_defaults(AppConfig_t* cfg);

/* Parses argv (argv[0] is the program name). Returns one of the above. */
int  app_config_parse(AppConfig_t* cfg, int argc, char** argv);

/* Validates a fully populated config. Returns 0 on success, else -1 and
 * writes a human-readable reason into `err`. */
int  app_config_validate(const AppConfig_t* cfg, char* err, size_t err_size);

void app_config_print_usage(FILE* out, const char* argv0);
void app_config_print_summary(FILE* out, const AppConfig_t* cfg);
