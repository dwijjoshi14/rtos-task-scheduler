/*
 * display_task.c — status rendering, alert banner and CSV streaming.
 */

#define _POSIX_C_SOURCE 200809L

#include "display_task.h"

#include "control_logic.h"
#include "control_task.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>

#define SPARK_WIDTH 16

#define ANSI_RED     "\033[1;31m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_DIM     "\033[2m"
#define ANSI_RESET   "\033[0m"

static const char* const k_sparks[8] = {
    "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
};

typedef struct {
    float    values[SPARK_WIDTH];
    unsigned count;
    unsigned index;
} Sparkline_t;

static void sparkline_push(Sparkline_t* spark, float value) {
    spark->values[spark->index] = value;
    spark->index = (spark->index + 1u) % SPARK_WIDTH;
    if (spark->count < SPARK_WIDTH) spark->count++;
}

static void sparkline_render(const Sparkline_t* spark, char* out, size_t out_size) {
    /* Fixed scale: 20 C at the bottom, 90 C at the top. */
    static const float low_c  = 20.0f;
    static const float span_c = 70.0f;

    size_t used = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < spark->count; i++) {
        unsigned position = (spark->index + SPARK_WIDTH - spark->count + i) % SPARK_WIDTH;
        float    value = spark->values[position];
        int      level = (int)((value - low_c) / span_c * 7.0f + 0.5f);
        if (level < 0) level = 0;
        if (level > 7) level = 7;
        size_t len = strlen(k_sparks[level]);
        if (used + len + 1 > out_size) break;
        memcpy(out + used, k_sparks[level], len);
        used += len;
        out[used] = '\0';
    }
}

static const char* state_colour(FanState_t state, int use_color) {
    if (!use_color) return "";
    switch (state) {
        case FAN_OFF:  return ANSI_CYAN;
        case FAN_LOW:  return ANSI_GREEN;
        case FAN_HIGH: return ANSI_YELLOW;
        default:       return ANSI_RED;
    }
}

static void render_sample(const DisplayMsg_t* msg, const Sparkline_t* spark,
                          int use_color) {
    char bar[12];
    unsigned filled = (unsigned)(msg->duty_pct / 10u);
    if (filled > 10) filled = 10;
    for (unsigned i = 0; i < 10; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[10] = '\0';

    char tail[SPARK_WIDTH * 4 + 1];
    sparkline_render(spark, tail, sizeof tail);

    const char* colour = state_colour(msg->fan_state, use_color);
    const char* reset  = use_color ? ANSI_RESET : "";
    const char* dim    = use_color ? ANSI_DIM : "";

    printf("[%5.1fs] %s%5.1fC%s  %-4s %3u%%  [%s] %s%s%s\n",
           (double)(msg->tick_ms - g_sim_start_ms) / 1000.0,
           colour, (double)msg->temperature_c, reset,
           fan_state_name(msg->fan_state), (unsigned)msg->duty_pct,
           bar, dim, tail, reset);
}

static void render_alert(int use_color) {
    const char* colour = use_color ? ANSI_RED : "";
    const char* reset  = use_color ? ANSI_RESET : "";
    printf("\n%s"
           "  +--------------------------------------+\n"
           "  |   CRITICAL TEMPERATURE - FAN AT MAX  |\n"
           "  |   plant above the critical limit     |\n"
           "  +--------------------------------------+\n"
           "%s\n", colour, reset);
}

static void render_fault(const DisplayMsg_t* msg, int use_color) {
    const char* colour = use_color ? ANSI_YELLOW : "";
    const char* reset  = use_color ? ANSI_RESET : "";
    printf("%s[%5.1fs] sensor fault: %-12s holding fan %s (fail-safe)%s\n",
           colour, (double)(msg->tick_ms - g_sim_start_ms) / 1000.0,
           sensor_fault_name(msg->fault),
           fan_state_name(msg->fan_state), reset);
}

void DisplayTask(void* params) {
    AppConfig_t* cfg = (AppConfig_t*)params;

    Sparkline_t spark;
    memset(&spark, 0, sizeof spark);

    uint32_t rendered = 0;

    for (;;) {
        if (sim_should_stop()) break;

        if (xSemaphoreTake(xAlertSemaphore, 0) == pdPASS) {
            render_alert(cfg->use_color);
        }

        DisplayMsg_t msg;
        if (xQueueReceive(xDisplayQueue, &msg, pdMS_TO_TICKS(cfg->display_period_ms)) == pdPASS) {
            if (msg.fault != SENSOR_OK) {
                render_fault(&msg, cfg->use_color);
            } else {
                sparkline_push(&spark, msg.temperature_c);
                metrics_record_latency((uint32_t)xTaskGetTickCount() - msg.tick_ms);
                if (cfg->verbose) render_sample(&msg, &spark, cfg->use_color);
                rendered++;
            }

            metrics_csv_row(msg.sequence, msg.tick_ms - g_sim_start_ms,
                            msg.raw_c, msg.temperature_c, msg.fan_state,
                            msg.duty_pct, msg.is_critical, msg.fault);

            if (cfg->max_samples > 0 && rendered >= cfg->max_samples) {
                sim_request_stop();
                break;
            }
        }

        if (sim_deadline_reached(cfg->run_duration_ms)) {
            sim_request_stop();
            break;
        }
    }

    if (cfg->verbose) {
        printf("\n[DisplayTask] rendered %u sample(s)\n", rendered);
    }
}
