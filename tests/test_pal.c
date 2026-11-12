#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "input_timing.h"
#include "pixel_convert.h"
#include "video_geometry.h"
#include "line_ring.h"
#include "src.h"

line_ring_t g_line_ring;

static void test_timing(void) {
    const uint32_t periods[] = {16639, 16792, 20000, 19997};
    const uint32_t widths[] = {14290, 15242, 14421, 15382};
    for (unsigned i = 0; i < 4; ++i) {
        input_detector_t d = {0};
        for (unsigned j = 0; j < INPUT_LOCK_FRAMES - 1; ++j) {
            input_detector_observe(&d, periods[i], widths[i]);
            assert(d.locked == 0);
        }
        input_detector_observe(&d, periods[i], widths[i]);
        assert(input_region(d.locked) == (i < 2 ? INPUT_60HZ : INPUT_50HZ));
        assert(input_height(d.locked) == ((i & 1) ? 239 : 224));
        /* A malformed period invalidates old lock; reacquisition is debounced. */
        input_detector_observe(&d, 1000, widths[i]);
        assert(!d.locked);
        for (unsigned j = 0; j < INPUT_LOCK_FRAMES; ++j)
            input_detector_observe(&d, 19997, 15382);
        assert(d.locked == input_profile(INPUT_50HZ, 239));
        input_detector_observe(&d, 16792, 14290);
        assert(!d.locked);
    }
    assert(!input_classify(18000, 14290));
    assert(!input_classify(20000, 14700));
    assert(!input_classify(20000, 0));
    uint32_t start = UINT32_MAX - 10000U, end = start + 19997U;
    assert(input_classify(end - start, 14421) == input_profile(INPUT_50HZ, 224));
}

static void test_color(void) {
    for (uint32_t raw = 0; raw < 32768; ++raw) {
        uint32_t channels[3] = {0};
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned b = 0; b < 5; ++b)
                channels[c] |= ((raw >> (c * 5 + b)) & 1) << (4 - b);
        uint16_t expected = (channels[2] << 11) |
            (((channels[1] * 2) + (channels[1] / 16)) << 5) | channels[0];
        uint16_t actual = snes_pack_rgb565(snes_reverse_5bit(raw >> 10),
            snes_reverse_5bit((raw >> 5) & 31), snes_reverse_5bit(raw & 31));
        assert(actual == expected);
    }
    assert(snes_pack_rgb565(0,31,0) == 0x07e0);
}

static void test_geometry(void) {
    const unsigned outputs[] = {240,480,576,720}, scales[] = {1,2,2,3};
    for (unsigned m = 0; m < 4; ++m)
        for (unsigned h = 224; h <= 239; h += 15) {
            unsigned counts[239] = {0};
            for (unsigned y = 0; y < outputs[m]; ++y) {
                int row = video_source_row(y, outputs[m], h, scales[m]);
                assert(row == -1 || (row >= 0 && (unsigned)row < h));
                if (row >= 0) ++counts[row];
            }
            for (unsigned y = 0; y < h; ++y) assert(counts[y] == scales[m]);
        }
    assert(video_source_row(48,576,239,2) == -1);
    assert(video_source_row(49,576,239,2) == 0);
    assert(video_source_row(526,576,239,2) == 238);
    assert(video_source_row(527,576,239,2) == -1);
}

/* Sweep capture/output rates and phase, injecting vsync halfway through each
 * capture. Displayed pixels must remain immutable until output vsync. */
static void test_ownership(void) {
    const double inputs[] = {50,50.00698,59.55,60,60.0988};
    const double outputs[] = {50,60};
    for (unsigned a=0;a<5;++a) for (unsigned b=0;b<2;++b)
        for (unsigned phase=0;phase<8;++phase) {
            line_ring_init();
            double next_out = phase * (1000000.0 / outputs[b]) / 8;
            unsigned last_display = 0;
            for (unsigned frame=1;frame<=1200;++frame) {
                double start = frame * (1000000.0 / inputs[a]);
                line_ring_vsync();
                int writing = g_line_ring.writing;
                assert(writing != g_line_ring.reading && writing != g_line_ring.pending);
                line_ring_write_ptr(0)[0] = frame;
                while (next_out <= start + 15000) {
                    line_ring_output_vsync();
                    if (line_ring_ready(0)) {
                        assert(line_ring_read_ptr(0)[0] >= last_display);
                        last_display = line_ring_read_ptr(0)[0];
                        assert(last_display < frame);
                    }
                    next_out += 1000000.0 / outputs[b];
                }
                if (line_ring_ready(0)) assert(line_ring_read_ptr(0)[0] == last_display);
                line_ring_publish((frame & 1) ? 224 : 239);
            }
            line_ring_invalidate();
            line_ring_output_vsync();
            assert(!line_ring_ready(0));
            line_ring_vsync();
            line_ring_write_ptr(0)[0] = 777;
            line_ring_publish(239);
            line_ring_output_vsync();
            assert(line_ring_read_ptr(0)[0] == 777 && line_ring_height() == 239);
        }
}

static audio_sample_t input[1000], whole[2500], chunked[2500];
static unsigned convert(src_mode_t mode, unsigned chunk, unsigned cap, audio_sample_t *out) {
    src_t s;
    src_init(&s, 32040, 48000);
    src_set_mode(&s, mode);
    unsigned pos=0, total=0;
    for (unsigned guard=0; guard<10000; ++guard) {
        unsigned n = 1000-pos;
        if(n>chunk)n=chunk;
        uint32_t consumed;
        unsigned written=src_process(&s,input+pos,n,out+total,cap,&consumed);
        assert(consumed <= n);
        pos+=consumed; total+=written;
        assert(total<2500);
        if(!written&&!consumed) break;
    }
    assert(pos==1000);
    return total;
}
static void test_audio(void) {
    for (unsigned i=0;i<1000;++i) {
        input[i].left = (i&1) ? 32767 : -32768;
        input[i].right = (i&1) ? -32768 : 32767;
    }
    for (src_mode_t m=SRC_MODE_DROP;m<=SRC_MODE_LINEAR;++m) {
        unsigned reference=convert(m,1000,2400,whole);
        for(unsigned chunk=1;chunk<=64;chunk*=2)
            for(unsigned cap=1;cap<=128;cap*=2) {
                unsigned got=convert(m,chunk,cap,chunked);
                assert(got==reference);
                assert(!memcmp(whole,chunked,got*sizeof(audio_sample_t)));
            }
    }
    /* Midpoint of full-scale transition, also exercises the >INT32 multiply. */
    src_t s; src_init(&s,32000,48000); src_set_mode(&s,SRC_MODE_LINEAR);
    s.have_prev=true; s.prev_sample=(audio_sample_t){-32768,32767}; s.phase=49152;
    audio_sample_t next={32767,-32768}, out;
    uint32_t consumed;
    assert(src_process(&s,&next,1,&out,1,&consumed)==1);
    assert(out.left==16383 && out.right==-16385);
}
int main(void) {
    test_timing(); test_color(); test_geometry(); test_ownership(); test_audio();
    puts("PASS: timing, all RGB555 colors, geometry, 96,000 frame interleavings, audio chunk invariance");
}
