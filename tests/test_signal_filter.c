/*
 * test_signal_filter.c — signal conditioning behaviour.
 */

#include "signal_filter.h"
#include "test_framework.h"

TEST_CASE(filter, ema_seeds_with_first_sample) {
    EmaFilter_t filter;
    ema_init(&filter, 0.35f);

    /* Seeding at 0 C would look like a sensor fault for the first samples. */
    CHECK_NEAR(ema_update(&filter, 40.0f), 40.0f, 1e-6);
    CHECK_NEAR(ema_update(&filter, 42.0f), 40.7f, 1e-4);
}

TEST_CASE(filter, ema_converges_to_constant_input) {
    EmaFilter_t filter;
    ema_init(&filter, 0.25f);

    ema_update(&filter, 10.0f);
    for (int i = 0; i < 200; i++) ema_update(&filter, 60.0f);
    CHECK_NEAR(filter.value, 60.0f, 0.05f);
}

TEST_CASE(filter, ema_with_alpha_one_passes_through) {
    EmaFilter_t filter;
    ema_init(&filter, 1.0f);

    ema_update(&filter, 10.0f);
    CHECK_NEAR(ema_update(&filter, 55.5f), 55.5f, 1e-6);
}

TEST_CASE(filter, ema_attenuates_noise) {
    EmaFilter_t filter;
    ema_init(&filter, 0.2f);
    ema_update(&filter, 50.0f);

    /* Symmetric noise around a stable mean must leave the estimate near it. */
    float result = 0.0f;
    for (int i = 0; i < 200; i++) {
        result = ema_update(&filter, 50.0f + ((i % 2) ? 1.0f : -1.0f));
    }
    CHECK_NEAR(result, 50.0f, 0.2f);
}

TEST_CASE(filter, ema_rejects_invalid_state) {
    /* NULL is handled rather than dereferenced. */
    CHECK_NEAR(ema_update(NULL, 12.0f), 12.0f, 1e-6);
    ema_init(NULL, 0.5f);
    ema_reset(NULL, 0.0f);
}

TEST_CASE(filter, moving_average_window) {
    MovingAverage_t average;
    CHECK_INT(moving_average_init(&average, 3), 0);

    CHECK_NEAR(moving_average_update(&average, 1.0f), 1.0f, 1e-6);
    CHECK_NEAR(moving_average_update(&average, 2.0f), 1.5f, 1e-6);
    CHECK_NEAR(moving_average_update(&average, 3.0f), 2.0f, 1e-6);
    CHECK_NEAR(moving_average_update(&average, 4.0f), 3.0f, 1e-6);
    CHECK_NEAR(moving_average_update(&average, 6.0f), 4.333333f, 1e-4f);
}

TEST_CASE(filter, moving_average_validates_window) {
    MovingAverage_t average;
    CHECK_INT(moving_average_init(&average, 0), -1);
    CHECK_INT(moving_average_init(&average, MOVING_AVERAGE_MAX + 1), -1);
    CHECK_INT(moving_average_init(NULL, 4), -1);
    CHECK_INT(moving_average_init(&average, 1), 0);
}

TEST_CASE(filter, moving_average_tracks_step) {
    MovingAverage_t average;
    CHECK_INT(moving_average_init(&average, 4), 0);

    for (int i = 0; i < 8; i++) moving_average_update(&average, 0.0f);
    for (int i = 0; i < 4; i++) moving_average_update(&average, 10.0f);

    CHECK_NEAR(moving_average_update(&average, 10.0f), 10.0f, 1e-6);
}
