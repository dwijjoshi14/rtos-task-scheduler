/*
 * test_app_config.c — command-line contract.
 *
 * A wrong configuration is the most common way a control loop misbehaves, so
 * the parser rejects nonsense (descending thresholds, alpha outside (0,1], a
 * watchdog shorter than the sensor period) instead of silently clamping.
 */

#define _POSIX_C_SOURCE 200809L

#include "app_config.h"
#include "test_framework.h"

#include <fcntl.h>
#include <unistd.h>

static int parse(AppConfig_t* cfg, int argc, char** argv) {
    app_config_defaults(cfg);
    return app_config_parse(cfg, argc, argv);
}

/* Error paths print diagnostics; keep the test log readable by swallowing them. */
static int parse_captured(AppConfig_t* cfg, int argc, char** argv) {
    fflush(stdout);
    fflush(stderr);
    const int saved_out = dup(STDOUT_FILENO);
    const int saved_err = dup(STDERR_FILENO);
    const int devnull   = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    const int result = parse(cfg, argc, argv);

    fflush(stdout);
    fflush(stderr);
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    if (saved_err >= 0) { dup2(saved_err, STDERR_FILENO); close(saved_err); }
    return result;
}

TEST_CASE(config, defaults_are_valid) {
    AppConfig_t cfg;
    app_config_defaults(&cfg);

    char err[128] = {0};
    CHECK_INT(app_config_validate(&cfg, err, sizeof err), 0);
    CHECK_INT(cfg.sensor_period_ms, 200);
    CHECK_NEAR(cfg.thresholds.low_c, 30.0, 1e-6);
    CHECK_NEAR(cfg.thresholds.high_c, 50.0, 1e-6);
    CHECK_NEAR(cfg.thresholds.critical_c, 75.0, 1e-6);
    CHECK_NEAR(cfg.ema_alpha, 0.35, 1e-6);
    CHECK(cfg.verbose == 1);
}

TEST_CASE(config, parses_integer_options) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--duration", "5000", "--seed", "42",
                     "--sensor-period", "100", "--max-samples", "7" };

    CHECK_INT(parse(&cfg, 9, argv), APP_CONFIG_OK);
    CHECK_INT(cfg.run_duration_ms, 5000);
    CHECK_INT(cfg.seed, 42);
    CHECK_INT(cfg.sensor_period_ms, 100);
    CHECK_INT(cfg.max_samples, 7);
}

TEST_CASE(config, parses_threshold_triple) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--thresholds", "20,45,80" };

    CHECK_INT(parse(&cfg, 3, argv), APP_CONFIG_OK);
    CHECK_NEAR(cfg.thresholds.low_c, 20.0, 1e-6);
    CHECK_NEAR(cfg.thresholds.high_c, 45.0, 1e-6);
    CHECK_NEAR(cfg.thresholds.critical_c, 80.0, 1e-6);
}

TEST_CASE(config, parses_float_options) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--alpha", "0.8", "--noise", "1.5",
                     "--conductance", "0.5", "--fan-gain", "3" };

    CHECK_INT(parse(&cfg, 9, argv), APP_CONFIG_OK);
    CHECK_NEAR(cfg.ema_alpha, 0.8, 1e-6);
    CHECK_NEAR(cfg.noise_amplitude_c, 1.5, 1e-6);
    CHECK_NEAR(cfg.conductance_w_per_k, 0.5, 1e-6);
    CHECK_NEAR(cfg.fan_gain_w_per_k, 3.0, 1e-6);
}

TEST_CASE(config, parses_output_flags) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--csv", "out.csv", "--json", "out.json",
                     "--quiet", "--no-color" };

    CHECK_INT(parse(&cfg, 7, argv), APP_CONFIG_OK);
    CHECK_STR(cfg.csv_path, "out.csv");
    CHECK_STR(cfg.json_path, "out.json");
    CHECK_INT(cfg.verbose, 0);
    CHECK_INT(cfg.use_color, 0);
}

TEST_CASE(config, help_and_version_exit_successfully) {
    AppConfig_t cfg;
    char* help[] = { "test", "--help" };
    char* version[] = { "test", "--version" };
    char* short_help[] = { "test", "-h" };

    CHECK_INT(parse_captured(&cfg, 2, help), APP_CONFIG_EXIT);
    CHECK_INT(parse_captured(&cfg, 2, version), APP_CONFIG_EXIT);
    CHECK_INT(parse_captured(&cfg, 2, short_help), APP_CONFIG_EXIT);
}

TEST_CASE(config, parses_fault_injection) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--stuck-at", "12", "--stall-at", "20" };

    CHECK_INT(parse(&cfg, 5, argv), APP_CONFIG_OK);
    CHECK_INT(cfg.stuck_at_sample, 12);
    CHECK_INT(cfg.stall_at_sample, 20);
}

TEST_CASE(config, rejects_unknown_option) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--turbo" };
    CHECK_INT(parse_captured(&cfg, 2, argv), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_missing_value) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--duration" };
    CHECK_INT(parse_captured(&cfg, 2, argv), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_non_numeric_values) {
    AppConfig_t cfg;
    char* duration[] = { "test", "--duration", "soon" };
    char* alpha[]    = { "test", "--alpha", "nan" };
    char* noise[]    = { "test", "--noise", "lots" };

    CHECK_INT(parse_captured(&cfg, 3, duration), APP_CONFIG_ERROR);
    CHECK_INT(parse_captured(&cfg, 3, alpha), APP_CONFIG_ERROR);
    CHECK_INT(parse_captured(&cfg, 3, noise), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_negative_numbers) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--duration", "-5" };
    CHECK_INT(parse_captured(&cfg, 3, argv), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_descending_thresholds) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--thresholds", "70,50,75" };
    CHECK_INT(parse_captured(&cfg, 3, argv), APP_CONFIG_ERROR);

    char* equal[] = { "test", "--thresholds", "50,50,75" };
    CHECK_INT(parse_captured(&cfg, 3, equal), APP_CONFIG_ERROR);

    char* partial[] = { "test", "--thresholds", "30,50" };
    CHECK_INT(parse_captured(&cfg, 3, partial), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_excessive_hysteresis) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--hysteresis", "60" };
    CHECK_INT(parse_captured(&cfg, 3, argv), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_bad_alpha) {
    AppConfig_t cfg;
    char* zero[] = { "test", "--alpha", "0" };
    char* above[] = { "test", "--alpha", "1.5" };

    CHECK_INT(parse_captured(&cfg, 3, zero), APP_CONFIG_ERROR);
    CHECK_INT(parse_captured(&cfg, 3, above), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_watchdog_shorter_than_period) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--watchdog", "100", "--sensor-period", "500" };
    CHECK_INT(parse_captured(&cfg, 5, argv), APP_CONFIG_ERROR);
}

TEST_CASE(config, rejects_zero_durations) {
    AppConfig_t cfg;
    char* argv[] = { "test", "--duration", "0" };
    CHECK_INT(parse_captured(&cfg, 3, argv), APP_CONFIG_ERROR);

    AppConfig_t other;
    char* sensor[] = { "test", "--sensor-period", "0" };
    CHECK_INT(parse_captured(&other, 3, sensor), APP_CONFIG_ERROR);
}

TEST_CASE(config, validate_reports_reason) {
    AppConfig_t cfg;
    app_config_defaults(&cfg);
    cfg.thresholds.low_c = 90.0f;   /* now above `high` */

    char err[160] = {0};
    CHECK_INT(app_config_validate(&cfg, err, sizeof err), -1);
    CHECK(err[0] != '\0');
    CHECK(strstr(err, "increasing") != NULL);
}
