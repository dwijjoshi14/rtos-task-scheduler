/*
 * signal_filter.c — EMA and boxcar filters.
 */

#include "signal_filter.h"

#include <stddef.h>

void ema_init(EmaFilter_t* filter, float alpha) {
    if (!filter) return;
    filter->alpha       = alpha;
    filter->value       = 0.0f;
    filter->initialised = 0;
}

void ema_reset(EmaFilter_t* filter, float value) {
    if (!filter) return;
    filter->value       = value;
    filter->initialised = 1;
}

float ema_update(EmaFilter_t* filter, float sample) {
    if (!filter) return sample;
    if (!filter->initialised) {
        /* Seed with the first sample: otherwise the filter starts at 0 C and
         * needs ~1/alpha samples to climb out, which looks like a sensor fault. */
        filter->value       = sample;
        filter->initialised = 1;
        return filter->value;
    }
    filter->value = filter->alpha * sample + (1.0f - filter->alpha) * filter->value;
    return filter->value;
}

int moving_average_init(MovingAverage_t* filter, unsigned window) {
    if (!filter || window == 0 || window > MOVING_AVERAGE_MAX) return -1;
    filter->window = window;
    filter->count  = 0;
    filter->index  = 0;
    filter->sum    = 0.0f;
    for (unsigned i = 0; i < MOVING_AVERAGE_MAX; i++) filter->buffer[i] = 0.0f;
    return 0;
}

float moving_average_update(MovingAverage_t* filter, float sample) {
    if (!filter || filter->window == 0) return sample;

    if (filter->count < filter->window) {
        filter->buffer[filter->index] = sample;
        filter->sum += sample;
        filter->count++;
    } else {
        filter->sum -= filter->buffer[filter->index];
        filter->buffer[filter->index] = sample;
        filter->sum += sample;
    }
    filter->index = (filter->index + 1u) % filter->window;
    return filter->sum / (float)filter->count;
}
