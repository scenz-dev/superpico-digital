/**
 * Audio Pipeline - SNES I2S Capture → HDMI
 *
 * Captures ~32kHz audio from S-DSP via PIO/DMA, upsamples to 48kHz
 * via linear interpolation SRC, and feeds to HDMI data island queue.
 *
 * Modeled after neopico-hd's audio_subsystem.c.
 */

#include "audio_pipeline.h"
#include "config.h"

#if ENABLE_AUDIO
#include "pico/stdlib.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/video_output_rt.h"

#include "audio_buffer.h"
#include "audio_common.h"
#include "i2s_capture.h"
#include "src.h"
#include "snes_pins.h"
#include "video/freq_counter.h"

#include "hardware/gpio.h"

#include <string.h>

#ifndef ENABLE_AUDIO_STARTUP_REARM
#define ENABLE_AUDIO_STARTUP_REARM 0
#endif

#define PROCESS_BUFFER_SIZE 16

// State machine — matches neopico-hd's pattern
enum {
    AUDIO_STATE_WAIT_HSTX,  // Wait for HSTX to stabilize
    AUDIO_STATE_INIT,       // Initialize PIO + DMA
    AUDIO_STATE_WARM,       // Capture running, output muted
    AUDIO_STATE_REARM,      // Hard restart I2S PIO/DMA after clocks settle
    AUDIO_STATE_REWARM,     // Capture running, output muted after rearm
    AUDIO_STATE_RUNNING,    // Normal operation
    AUDIO_STATE_RESET,      // S-DSP /RESET asserted — pipeline stopped
};

#define AUDIO_HSTX_SETTLE_US 2000000U
#define AUDIO_WARM_US 500000U

// Audio collect buffer (matches neopico-hd pattern)
#define AUDIO_COLLECT_SIZE 128

static struct {
    ap_ring_t capture_ring;
    i2s_capture_t capture;
    src_t src;

    // HDMI output
    int audio_frame_counter;
    audio_sample_t collect_buffer[AUDIO_COLLECT_SIZE];
    uint32_t collect_count;
    audio_sample_t input_buffer[PROCESS_BUFFER_SIZE];
    uint32_t input_count;
    bool output_muted;

    // State machine
    int state;
    uint32_t state_enter_us;

    uint32_t samples_output;
    bool initialized;
    bool hw_initialized;
    volatile bool rearm_requested;
    uint32_t rearm_count;
    uint32_t reset_count;
} g_pipeline;

static const audio_sample_t audio_silence[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

static inline bool audio_di_hsync_active(void)
{
    return hstx_di_queue_get_hsync_active();
}

// Rate tracking — use DCK measurement when available, fall back to drift compensation
#define DRIFT_CHECK_INTERVAL_US 500000U
#define DRIFT_RATE_STEP      10  // Hz adjustment per check
#define DRIFT_QUEUE_HIGH    160
#define DRIFT_QUEUE_LOW      96
#define DRIFT_RATE_MIN    30000  // SNES ~32kHz ±5%
#define DRIFT_RATE_MAX    34000

// Valid DCK range: 8.192 MHz ±5%
#define DCK_FREQ_MIN  7782000
#define DCK_FREQ_MAX  8601000

static uint32_t last_drift_us;

static void update_src_rate(void)
{
    if ((uint32_t)(time_us_32() - last_drift_us) < DRIFT_CHECK_INTERVAL_US)
        return;
    last_drift_us = time_us_32();

    // Prefer DCK-derived rate if available
    uint32_t dck = freq_dck_hz;
    if (dck >= DCK_FREQ_MIN && dck <= DCK_FREQ_MAX) {
        g_pipeline.src.input_rate = dck / 256;
        return;
    }

    // Fallback: drift compensation from DI queue level
    uint32_t level = hstx_di_queue_get_level();
    uint32_t rate = g_pipeline.src.input_rate;

    if (level > DRIFT_QUEUE_HIGH)
        rate += DRIFT_RATE_STEP;
    else if (level < DRIFT_QUEUE_LOW)
        rate -= DRIFT_RATE_STEP;

    if (rate < DRIFT_RATE_MIN) rate = DRIFT_RATE_MIN;
    if (rate > DRIFT_RATE_MAX) rate = DRIFT_RATE_MAX;

    g_pipeline.src.input_rate = rate;
}

/* Retain queued samples on backpressure. Bound work per background call so
 * audio cannot starve the HDMI scanline service. */
static void audio_flush_output(void)
{
    for (uint32_t n = 0; n < 8 && g_pipeline.collect_count >= 4; ++n) {
        hstx_packet_t packet;
        const audio_sample_t *src = g_pipeline.output_muted ? audio_silence : g_pipeline.collect_buffer;
        int fc = hstx_packet_set_audio_samples(&packet, src, 4, g_pipeline.audio_frame_counter);
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, audio_di_hsync_active());
        if (!hstx_di_queue_push(&island)) break;
        g_pipeline.audio_frame_counter = fc;
        g_pipeline.collect_count -= 4;
        memmove(g_pipeline.collect_buffer, g_pipeline.collect_buffer + 4,
                g_pipeline.collect_count * sizeof(audio_sample_t));
        g_pipeline.samples_output += 4;
    }
}

static void audio_do_process(void)
{
    i2s_capture_poll(&g_pipeline.capture);
    audio_flush_output();
    uint32_t room = AUDIO_COLLECT_SIZE - g_pipeline.collect_count;
    if (!room) return;
    if (!g_pipeline.input_count) {
        uint32_t n = ap_ring_available(&g_pipeline.capture_ring);
        if (n > PROCESS_BUFFER_SIZE) n = PROCESS_BUFFER_SIZE;
        for (uint32_t i = 0; i < n; ++i)
            g_pipeline.input_buffer[i] = ap_ring_read(&g_pipeline.capture_ring);
        g_pipeline.input_count = n;
    }
    uint32_t consumed;
    uint32_t count = src_process(&g_pipeline.src, g_pipeline.input_buffer,
        g_pipeline.input_count, g_pipeline.collect_buffer + g_pipeline.collect_count,
        room, &consumed);
    g_pipeline.collect_count += count;
    g_pipeline.input_count -= consumed;
    memmove(g_pipeline.input_buffer, g_pipeline.input_buffer + consumed,
            g_pipeline.input_count * sizeof(audio_sample_t));
}

static void audio_reset_gpio_init(void)
{
    gpio_init(PIN_AUDIO_RESET);
    gpio_set_dir(PIN_AUDIO_RESET, GPIO_IN);
    gpio_pull_up(PIN_AUDIO_RESET);
    gpio_set_input_hysteresis_enabled(PIN_AUDIO_RESET, true);
}

static inline bool audio_reset_active(void)
{
    return !gpio_get(PIN_AUDIO_RESET);  // Active-low
}

static void audio_flush_processing_state(void)
{
    ap_ring_init(&g_pipeline.capture_ring);
    g_pipeline.collect_count = 0;
    g_pipeline.input_count = 0;
    g_pipeline.audio_frame_counter = 0;
    g_pipeline.src.accumulator = 0;
    g_pipeline.src.phase = 0;
    g_pipeline.src.have_prev = false;
    last_drift_us = time_us_32();
}

static bool audio_hw_init_once(void)
{
    if (g_pipeline.hw_initialized)
        return true;

    ap_ring_init(&g_pipeline.capture_ring);

    i2s_capture_config_t cap_config = {
        .pin_bck = PIN_AUDIO_BCLK,
        .pin_dat = PIN_AUDIO_SDATA,
        .pin_ws = PIN_AUDIO_LRCK,
        .pio = pio0,
        .sm = 2,
    };

    if (!i2s_capture_init(&g_pipeline.capture, &cap_config, &g_pipeline.capture_ring))
        return false;

    // SRC: 32040 Hz (SNES S-DSP) → 48000 Hz (HDMI default)
    src_init(&g_pipeline.src, SRC_INPUT_RATE_DEFAULT, SRC_OUTPUT_RATE_DEFAULT);
    src_set_mode(&g_pipeline.src, SRC_MODE_LINEAR);
    g_pipeline.hw_initialized = true;
    return true;
}

static void audio_rearm_capture(void)
{
    if (!g_pipeline.hw_initialized)
        return;

    i2s_capture_stop(&g_pipeline.capture);
    audio_flush_processing_state();
    i2s_capture_start(&g_pipeline.capture);
    g_pipeline.rearm_count++;
}

void audio_pipeline_init(void)
{
    memset(&g_pipeline, 0, sizeof(g_pipeline));
    g_pipeline.state = AUDIO_STATE_WAIT_HSTX;
    g_pipeline.state_enter_us = time_us_32();
    g_pipeline.output_muted = true;
    g_pipeline.initialized = true;
    audio_reset_gpio_init();
}

// Background task — called from Core 1 via video_output_set_background_task.
// Must return quickly to avoid starving HDMI output.
void audio_pipeline_process(void)
{
    if (!g_pipeline.initialized)
        return;

    // Check /RESET in any active state
    if (g_pipeline.state >= AUDIO_STATE_INIT && audio_reset_active()) {
        if (g_pipeline.state != AUDIO_STATE_RESET) {
            g_pipeline.reset_count++;
        }
        if (g_pipeline.hw_initialized && g_pipeline.capture.running) {
            i2s_capture_stop(&g_pipeline.capture);
        }
        g_pipeline.output_muted = true;
        g_pipeline.collect_count = 0;
        g_pipeline.state = AUDIO_STATE_RESET;
    }

    if (g_pipeline.rearm_requested && g_pipeline.state == AUDIO_STATE_RUNNING) {
        g_pipeline.rearm_requested = false;
        g_pipeline.output_muted = true;
        audio_rearm_capture();
        g_pipeline.state_enter_us = time_us_32();
        g_pipeline.state = AUDIO_STATE_REWARM;
        return;
    }

    switch (g_pipeline.state) {
        case AUDIO_STATE_WAIT_HSTX:
            if ((uint32_t)(time_us_32() - g_pipeline.state_enter_us) >= AUDIO_HSTX_SETTLE_US)
                g_pipeline.state = AUDIO_STATE_INIT;
            break;

        case AUDIO_STATE_RESET:
            // Wait for /RESET to deassert
            if (!audio_reset_active())
                g_pipeline.state = AUDIO_STATE_INIT;
            break;

        case AUDIO_STATE_INIT:
            if (!audio_hw_init_once())
                break;
            audio_flush_processing_state();
            i2s_capture_start(&g_pipeline.capture);
            g_pipeline.state_enter_us = time_us_32();
            g_pipeline.state = AUDIO_STATE_WARM;
            break;

        case AUDIO_STATE_WARM:
            audio_do_process();
            if ((uint32_t)(time_us_32() - g_pipeline.state_enter_us) >= AUDIO_WARM_US) {
#if ENABLE_AUDIO_STARTUP_REARM
                g_pipeline.state = AUDIO_STATE_REARM;
#else
                audio_flush_processing_state();
                g_pipeline.output_muted = false;
                g_pipeline.state = AUDIO_STATE_RUNNING;
#endif
            }
            break;

        case AUDIO_STATE_REARM:
            g_pipeline.output_muted = true;
            audio_rearm_capture();
            g_pipeline.state_enter_us = time_us_32();
            g_pipeline.state = AUDIO_STATE_REWARM;
            break;

        case AUDIO_STATE_REWARM:
            audio_do_process();
            if ((uint32_t)(time_us_32() - g_pipeline.state_enter_us) >= AUDIO_WARM_US) {
                audio_flush_processing_state();
                g_pipeline.output_muted = false;
                g_pipeline.state = AUDIO_STATE_RUNNING;
            }
            break;

        case AUDIO_STATE_RUNNING:
            update_src_rate();
            audio_do_process();

            break;
    }
}

void audio_pipeline_step(void)
{
}

void audio_pipeline_request_rearm(void)
{
    g_pipeline.rearm_requested = true;
}

void audio_pipeline_get_diag(audio_pipeline_diag_t *diag)
{
    if (!diag)
        return;

    memset(diag, 0, sizeof(*diag));
    diag->samples_output = g_pipeline.samples_output;
    diag->measured_rate_hz = g_pipeline.hw_initialized ? i2s_capture_get_sample_rate(&g_pipeline.capture) : 0;
    diag->overflows = g_pipeline.hw_initialized ? g_pipeline.capture.overflows : 0;
    diag->rearm_count = g_pipeline.rearm_count;
    diag->reset_count = g_pipeline.reset_count;
    diag->muted = g_pipeline.output_muted;
    diag->running = g_pipeline.hw_initialized && g_pipeline.capture.running;
}
#endif
