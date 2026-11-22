/*
 * app_config.c — command-line parsing and validation.
 *
 * Kept free of any RTOS dependency so the parser is unit-testable.
 */

#define _POSIX_C_SOURCE 200809L

#include "app_config.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void app_config_defaults(AppConfig_t* cfg) {
    memset(cfg, 0, sizeof *cfg);

    cfg->sensor_period_ms  = 200;
    cfg->display_period_ms = 200;
    cfg->watchdog_ms       = 1000;

    cfg->run_duration_ms = 16000;
    cfg->max_samples     = 0;
    cfg->seed            = 2026;

    cfg->thresholds.low_c        = 30.0f;
    cfg->thresholds.high_c       = 50.0f;
    cfg->thresholds.critical_c   = 75.0f;
    cfg->thresholds.hysteresis_c = 2.0f;

    cfg->ema_alpha         = 0.35f;
    cfg->noise_amplitude_c = 0.35f;

    cfg->ambient_c           = 25.0f;
    cfg->heater_power_w      = 25.0f;
    cfg->heat_capacity_j_per_k = 5.0f;
    cfg->conductance_w_per_k = 0.35f;
    cfg->fan_gain_w_per_k    = 2.2f;

    cfg->burst_start_ms  = 6000;
    cfg->burst_end_ms    = 9500;
    cfg->burst_multiplier = 6.0f;

    cfg->stuck_at_sample = 0;
    cfg->stall_at_sample = 0;
    cfg->csv_path        = NULL;
    cfg->json_path       = NULL;
    cfg->use_color       = isatty(STDOUT_FILENO) ? 1 : 0;
    cfg->verbose         = 1;
}

static int parse_u32(const char* text, uint32_t* out) {
    if (!text || !*text || !isdigit((unsigned char)*text)) return -1;
    errno = 0;
    char* end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > 0xFFFFFFFFull) return -1;
    *out = (uint32_t)value;
    return 0;
}

static int parse_float_value(const char* text, float* out) {
    if (!text || !*text) return -1;
    errno = 0;
    char* end = NULL;
    double value = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !isfinite(value)) return -1;
    *out = (float)value;
    return 0;
}

static int parse_thresholds(const char* text, Thresholds_t* out) {
    if (!text) return -1;
    char buffer[96];
    size_t len = strlen(text);
    if (len == 0 || len >= sizeof buffer) return -1;
    memcpy(buffer, text, len + 1);

    double values[3];
    int    found = 0;
    char*  cursor = buffer;
    while (found < 3) {
        char* comma = strchr(cursor, ',');
        if (comma) *comma = '\0';
        char* end = NULL;
        errno = 0;
        double parsed = strtod(cursor, &end);
        if (errno != 0 || !end || end == cursor || *end != '\0' ||
            !isfinite(parsed)) {
            return -1;
        }
        values[found++] = parsed;
        if (!comma) break;
        cursor = comma + 1;
    }
    if (found != 3) return -1;

    out->low_c      = (float)values[0];
    out->high_c     = (float)values[1];
    out->critical_c = (float)values[2];
    return 0;
}

int app_config_validate(const AppConfig_t* cfg, char* err, size_t err_size) {
    char scratch[128];
    char* message = err ? err : scratch;
    size_t size = err ? err_size : sizeof scratch;
    if (size == 0) size = 1;

#define FAIL(...)                                                     \
    do {                                                              \
        snprintf(message, size, __VA_ARGS__);                         \
        return -1;                                                    \
    } while (0)

    if (cfg->sensor_period_ms == 0)  FAIL("sensor period must be > 0 ms");
    if (cfg->display_period_ms == 0) FAIL("display period must be > 0 ms");
    if (cfg->run_duration_ms == 0)   FAIL("run duration must be > 0 ms");
    if (cfg->watchdog_ms < cfg->sensor_period_ms) {
        FAIL("watchdog (%u ms) must be >= sensor period (%u ms)",
             cfg->watchdog_ms, cfg->sensor_period_ms);
    }
    if (cfg->max_samples > 0 && cfg->max_samples < 2) {
        FAIL("max-samples must be 0 or >= 2");
    }
    if (!(cfg->ema_alpha > 0.0f) || cfg->ema_alpha > 1.0f) {
        FAIL("alpha must be in (0, 1]");
    }
    if (cfg->noise_amplitude_c < 0.0f) FAIL("noise amplitude must be >= 0");
    if (cfg->heat_capacity_j_per_k <= 0.0f) FAIL("heat capacity must be > 0");
    if (cfg->conductance_w_per_k <= 0.0f)   FAIL("ambient conductance must be > 0");
    if (cfg->fan_gain_w_per_k < 0.0f)       FAIL("fan gain must be >= 0");
    if (cfg->heater_power_w <= 0.0f)        FAIL("heater power must be > 0");
    if (cfg->burst_multiplier < 1.0f)       FAIL("burst multiplier must be >= 1");
    if (cfg->burst_end_ms < cfg->burst_start_ms) {
        FAIL("burst end (%u ms) must be >= burst start (%u ms)",
             cfg->burst_end_ms, cfg->burst_start_ms);
    }

    const Thresholds_t* t = &cfg->thresholds;
    if (!(t->low_c < t->high_c && t->high_c < t->critical_c)) {
        FAIL("thresholds must be strictly increasing (got %.1f, %.1f, %.1f)",
             (double)t->low_c, (double)t->high_c, (double)t->critical_c);
    }
    if (t->hysteresis_c < 0.0f) FAIL("hysteresis must be >= 0");
    if (t->hysteresis_c >= (t->critical_c - t->low_c)) {
        FAIL("hysteresis (%.2f) must be smaller than the control band (%.2f)",
             (double)t->hysteresis_c, (double)(t->critical_c - t->low_c));
    }
    return 0;
#undef FAIL
}

void app_config_print_usage(FILE* out, const char* argv0) {
    fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Scheduling\n"
        "  --duration <ms>        run length (default 16000)\n"
        "  --sensor-period <ms>   SensorTask period (default 200)\n"
        "  --display-period <ms>  DisplayTask period (default 200)\n"
        "  --watchdog <ms>        sensor liveness timeout (default 1000)\n"
        "  --max-samples <n>      stop after n rendered samples (0 = off)\n"
        "\n"
        "Control\n"
        "  --thresholds <lo,hi,cr> fan thresholds in C (default 30,50,75)\n"
        "  --hysteresis <C>       downshift guard band (default 2.0)\n"
        "  --alpha <0..1>         sensor EMA coefficient (default 0.35)\n"
        "\n"
        "Plant (simulated hardware)\n"
        "  --ambient <C>          ambient temperature (default 25)\n"
        "  --power <W>            heater power (default 25)\n"
        "  --capacity <J/K>       thermal capacity (default 5)\n"
        "  --conductance <W/K>    ambient conductance (default 0.35)\n"
        "  --fan-gain <W/K>       extra conductance at full fan (default 2.2)\n"
        "  --burst-start <ms>     workload burst start (default 6000)\n"
        "  --burst-end <ms>       workload burst end (default 9500)\n"
        "  --burst <x>            heater power multiplier (default 6)\n"
        "  --noise <C>            sensor noise amplitude (default 0.35)\n"
        "  --seed <n>             noise seed (default 2026)\n"
        "  --stuck-at <n>         latch the sensor from sample n onward\n"
        "  --stall-at <n>         stop publishing from sample n (watchdog test)\n"
        "\n"
        "Output\n"
        "  --csv <path>           per-sample telemetry CSV\n"
        "  --json <path>          run summary JSON\n"
        "  --quiet                suppress per-sample LCD lines\n"
        "  --no-color             disable ANSI colour\n"
        "  --version              print version and exit\n"
        "  --help                 print this help and exit\n",
        argv0 ? argv0 : APP_NAME);
}

static void print_version(void) {
    printf("%s %s\n", APP_NAME, APP_VERSION);
}

void app_config_print_summary(FILE* out, const AppConfig_t* cfg) {
    fprintf(out,
        "  period    sensor %u ms · display %u ms · watchdog %u ms\n"
        "  control   thresholds %.1f / %.1f / %.1f C · hysteresis %.1f C · alpha %.2f\n"
        "  plant     ambient %.1f C · %0.1f W · %.1f J/K · %.2f W/K + %.2f W/K fan\n"
        "  workload  burst x%.1f from %u ms to %u ms\n"
        "  run       %u ms · seed %u · noise ±%.2f C%s\n",
        cfg->sensor_period_ms, cfg->display_period_ms, cfg->watchdog_ms,
        (double)cfg->thresholds.low_c, (double)cfg->thresholds.high_c,
        (double)cfg->thresholds.critical_c, (double)cfg->thresholds.hysteresis_c,
        (double)cfg->ema_alpha,
        (double)cfg->ambient_c, (double)cfg->heater_power_w,
        (double)cfg->heat_capacity_j_per_k, (double)cfg->conductance_w_per_k,
        (double)cfg->fan_gain_w_per_k,
        (double)cfg->burst_multiplier, cfg->burst_start_ms, cfg->burst_end_ms,
        cfg->run_duration_ms, cfg->seed, (double)cfg->noise_amplitude_c,
        cfg->max_samples ? " · sample-capped" : "");
}

int app_config_parse(AppConfig_t* cfg, int argc, char** argv) {
    char err[160] = {0};

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            app_config_print_usage(stdout, argv[0]);
            return APP_CONFIG_EXIT;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return APP_CONFIG_EXIT;
        }
        if (strcmp(arg, "--quiet") == 0)  { cfg->verbose = 0; continue; }
        if (strcmp(arg, "--no-color") == 0) { cfg->use_color = 0; continue; }
        if (strcmp(arg, "--color") == 0)  { cfg->use_color = 1; continue; }

        if (!value) {
            fprintf(stderr, "%s: missing value for %s\n", argv[0], arg);
            return APP_CONFIG_ERROR;
        }

        int bad = 0;
        if (strcmp(arg, "--duration") == 0) {
            bad = parse_u32(value, &cfg->run_duration_ms);
        } else if (strcmp(arg, "--sensor-period") == 0) {
            bad = parse_u32(value, &cfg->sensor_period_ms);
        } else if (strcmp(arg, "--display-period") == 0) {
            bad = parse_u32(value, &cfg->display_period_ms);
        } else if (strcmp(arg, "--watchdog") == 0) {
            bad = parse_u32(value, &cfg->watchdog_ms);
        } else if (strcmp(arg, "--max-samples") == 0) {
            bad = parse_u32(value, &cfg->max_samples);
        } else if (strcmp(arg, "--seed") == 0) {
            bad = parse_u32(value, &cfg->seed);
        } else if (strcmp(arg, "--thresholds") == 0) {
            bad = parse_thresholds(value, &cfg->thresholds);
        } else if (strcmp(arg, "--hysteresis") == 0) {
            bad = parse_float_value(value, &cfg->thresholds.hysteresis_c);
        } else if (strcmp(arg, "--alpha") == 0) {
            bad = parse_float_value(value, &cfg->ema_alpha);
        } else if (strcmp(arg, "--noise") == 0) {
            bad = parse_float_value(value, &cfg->noise_amplitude_c);
        } else if (strcmp(arg, "--ambient") == 0) {
            bad = parse_float_value(value, &cfg->ambient_c);
        } else if (strcmp(arg, "--power") == 0) {
            bad = parse_float_value(value, &cfg->heater_power_w);
        } else if (strcmp(arg, "--capacity") == 0) {
            bad = parse_float_value(value, &cfg->heat_capacity_j_per_k);
        } else if (strcmp(arg, "--conductance") == 0) {
            bad = parse_float_value(value, &cfg->conductance_w_per_k);
        } else if (strcmp(arg, "--fan-gain") == 0) {
            bad = parse_float_value(value, &cfg->fan_gain_w_per_k);
        } else if (strcmp(arg, "--burst-start") == 0) {
            bad = parse_u32(value, &cfg->burst_start_ms);
        } else if (strcmp(arg, "--burst-end") == 0) {
            bad = parse_u32(value, &cfg->burst_end_ms);
        } else if (strcmp(arg, "--burst") == 0) {
            bad = parse_float_value(value, &cfg->burst_multiplier);
        } else if (strcmp(arg, "--stuck-at") == 0) {
            bad = parse_u32(value, &cfg->stuck_at_sample);
        } else if (strcmp(arg, "--stall-at") == 0) {
            bad = parse_u32(value, &cfg->stall_at_sample);
        } else if (strcmp(arg, "--csv") == 0) {
            cfg->csv_path = value;
        } else if (strcmp(arg, "--json") == 0) {
            cfg->json_path = value;
        } else {
            fprintf(stderr, "%s: unknown option '%s' (try --help)\n", argv[0], arg);
            return APP_CONFIG_ERROR;
        }

        if (bad) {
            fprintf(stderr, "%s: invalid value for %s: '%s'\n", argv[0], arg, value);
            return APP_CONFIG_ERROR;
        }
        i++;  /* consumed the value */
    }

    if (app_config_validate(cfg, err, sizeof err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return APP_CONFIG_ERROR;
    }
    return APP_CONFIG_OK;
}
