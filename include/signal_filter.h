#pragma once
/*
 * signal_filter.h — sensor signal conditioning.
 *
 * A TMP36 on a 12-bit ADC jitters by a few tenths of a degree; feeding that
 * straight into a threshold comparison makes the fan chatter across a boundary.
 * Both filters here are allocation-free and O(1), which is what an ISR-safe
 * embedded implementation needs.
 */

#define MOVING_AVERAGE_MAX 32

/* Exponential moving average: y[n] = a·x[n] + (1 − a)·y[n−1] */
typedef struct {
    float alpha;
    float value;
    int   initialised;
} EmaFilter_t;

void  ema_init(EmaFilter_t* filter, float alpha);
void  ema_reset(EmaFilter_t* filter, float value);
float ema_update(EmaFilter_t* filter, float sample);

/* Boxcar (moving average) over a fixed window of recent samples. */
typedef struct {
    float    buffer[MOVING_AVERAGE_MAX];
    unsigned window;
    unsigned count;
    unsigned index;
    float    sum;
} MovingAverage_t;

/* Returns -1 if the window is outside [1, MOVING_AVERAGE_MAX]. */
int   moving_average_init(MovingAverage_t* filter, unsigned window);
float moving_average_update(MovingAverage_t* filter, float sample);
