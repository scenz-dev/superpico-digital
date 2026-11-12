/**
 * Sample Rate Conversion Implementation
 *
 * Three modes:
 * - NONE: Passthrough (plays slow since 32kHz < 48kHz)
 * - DROP: Bresenham-style sample repeating (minimal CPU, upsamples 32→48kHz)
 * - LINEAR: Linear interpolation (better quality)
 */

#include "src.h"

void src_init(src_t *s, uint32_t input_rate, uint32_t output_rate)
{
    s->mode = SRC_MODE_DROP; // Default to DROP
    s->input_rate = input_rate;
    s->output_rate = output_rate;
    s->accumulator = 0;
    s->phase = 0;
    s->prev_sample.left = 0;
    s->prev_sample.right = 0;
    s->have_prev = false;
}

void src_set_mode(src_t *s, src_mode_t mode)
{
    if (mode >= SRC_MODE_COUNT) {
        mode = SRC_MODE_NONE;
    }
    s->mode = mode;

    // Reset state on mode change
    s->accumulator = 0;
    s->phase = 0;
    s->have_prev = false;
}

src_mode_t src_cycle_mode(src_t *s)
{
    src_mode_t new_mode = (src_mode_t)((s->mode + 1) % SRC_MODE_COUNT);
    src_set_mode(s, new_mode);
    return new_mode;
}

// NONE mode: direct passthrough
static uint32_t src_process_none(const audio_sample_t *in, uint32_t in_count,
                                 audio_sample_t *out, uint32_t out_max,
                                 uint32_t *in_consumed)
{
    uint32_t count = (in_count < out_max) ? in_count : out_max;
    for (uint32_t i = 0; i < count; i++) {
        out[i] = in[i];
    }
    *in_consumed = count;
    return count;
}

// DROP mode: Bresenham-style sample rate conversion
// For upsampling (32→48kHz): repeats samples when output_rate > input_rate
static uint32_t src_process_drop(src_t *s, const audio_sample_t *in, uint32_t in_count,
                                 audio_sample_t *out, uint32_t out_max,
                                 uint32_t *in_consumed)
{
    uint32_t out_count = 0;
    uint32_t consumed = 0;

    while (out_count < out_max) {
        if (s->accumulator >= s->input_rate) {
            s->accumulator -= s->input_rate;
            out[out_count++] = s->prev_sample;
        } else if (consumed < in_count) {
            s->prev_sample = in[consumed++];
            s->accumulator += s->output_rate;
        } else break;
    }

    *in_consumed = consumed;
    return out_count;
}

// LINEAR mode: Linear interpolation between samples
// Uses fixed-point phase accumulator (16.16 format)
static uint32_t src_process_linear(src_t *s, const audio_sample_t *in, uint32_t in_count,
                                   audio_sample_t *out, uint32_t out_max,
                                   uint32_t *in_consumed)
{
    uint32_t out_count = 0, in_idx = 0;
    uint32_t step = ((uint64_t)s->input_rate << 16) / s->output_rate;
    if (!s->have_prev && in_idx < in_count) {
        s->prev_sample = in[in_idx++];
        s->have_prev = true;
    }
    while (out_count < out_max && s->have_prev) {
        while (s->phase >= 0x10000U && in_idx < in_count) {
            s->prev_sample = in[in_idx++];
            s->phase -= 0x10000U;
        }
        /* Need a real next sample; don't synthesize one at a chunk boundary. */
        if (in_idx == in_count || s->phase >= 0x10000U) break;
        const audio_sample_t next = in[in_idx];
        const int32_t dl = (int32_t)next.left - s->prev_sample.left;
        const int32_t dr = (int32_t)next.right - s->prev_sample.right;
        out[out_count].left = s->prev_sample.left + (((int64_t)dl * s->phase) >> 16);
        out[out_count].right = s->prev_sample.right + (((int64_t)dr * s->phase) >> 16);
        ++out_count;
        s->phase += step;
    }
    *in_consumed = in_idx;
    return out_count;
}

uint32_t src_process(src_t *s, const audio_sample_t *in, uint32_t in_count,
                     audio_sample_t *out, uint32_t out_max, uint32_t *in_consumed)
{
    if (!s->input_rate || !s->output_rate || !out_max) {
        *in_consumed = 0;
        return 0;
    }
    switch (s->mode) {
        case SRC_MODE_NONE:
            return src_process_none(in, in_count, out, out_max, in_consumed);
        case SRC_MODE_DROP:
            return src_process_drop(s, in, in_count, out, out_max, in_consumed);
        case SRC_MODE_LINEAR:
            return src_process_linear(s, in, in_count, out, out_max, in_consumed);
        default:
            *in_consumed = 0;
            return 0;
    }
}
