#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "video_pipeline.h"
#include "pal_output.h"
#include "video_geometry.h"
#include "osd/fast_osd.h"
#include "hardware/structs/watchdog.h"

static test_watchdog_t test_watchdog;
test_watchdog_t *watchdog_hw = &test_watchdog;
static jmp_buf reboot;
const video_mode_t *video_output_active_mode;
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t ms) {
    (void)pc; (void)sp; assert(ms == 10); longjmp(reboot, 1);
}
void audio_pipeline_step(void) {}

static void test_modes(void) {
    video_pipeline_set_reboot_requested_mode(VIDEO_PIPELINE_REBOOT_MODE_AUTO);
    if (!setjmp(reboot)) {
        video_pipeline_follow_input(INPUT_50HZ);
        assert(!"Auto should request a reboot into PAL output");
    }
    video_pipeline_reboot_mode_t m;
    assert(video_pipeline_take_reboot_mode_boot_request(&m));
    assert(m == VIDEO_PIPELINE_REBOOT_MODE_AUTO_PAL);
    assert(video_pipeline_reboot_requested_mode() == VIDEO_PIPELINE_REBOOT_MODE_AUTO);
    assert(!video_pipeline_take_save_request()); /* automatic changes never wear flash */
    video_pipeline_follow_input(INPUT_50HZ); /* already matches */
    if (!setjmp(reboot)) {
        video_pipeline_follow_input(INPUT_60HZ);
        assert(!"Auto should request a reboot into NTSC output");
    }
    assert(video_pipeline_take_reboot_mode_boot_request(&m) && m == VIDEO_PIPELINE_REBOOT_MODE_AUTO);
    video_pipeline_set_reboot_requested_mode(VIDEO_PIPELINE_REBOOT_MODE_480P);
    video_pipeline_follow_input(INPUT_50HZ); /* manual mode overrides auto */

    if (!setjmp(reboot)) video_pipeline_request_reboot_mode_pending(
        VIDEO_PIPELINE_REBOOT_MODE_576P, VIDEO_PIPELINE_REBOOT_MODE_AUTO);
    assert(video_pipeline_take_reboot_mode_boot_request(&m) && m == VIDEO_PIPELINE_REBOOT_MODE_576P);
    assert(!video_pipeline_take_save_request()); /* preview never requests flash write */
    assert(video_pipeline_take_pending_confirmation(&m) && m == VIDEO_PIPELINE_REBOOT_MODE_AUTO);
    if (!setjmp(reboot)) video_pipeline_request_reboot_mode_confirmed(VIDEO_PIPELINE_REBOOT_MODE_576P);
    assert(video_pipeline_take_reboot_mode_boot_request(&m) && m == VIDEO_PIPELINE_REBOOT_MODE_576P);
    assert(video_pipeline_take_save_request());
    assert(!video_pipeline_take_save_request()); /* one-shot */
    assert(!video_pipeline_take_reboot_mode_boot_request(&m));

    test_watchdog.scratch[0]=0x53505253;
    test_watchdog.scratch[1]=99;
    test_watchdog.scratch[2]=0x53505253 ^ 99 ^ 0xa5a55a5a;
    assert(!video_pipeline_take_reboot_mode_boot_request(&m));
}
static void test_render(void) {
    const unsigned heights[] = {240,480,576,720};
    const unsigned widths[] = {1280,640,720,1280};
    for (unsigned mode=0;mode<4;++mode) {
        video_mode_t output = {.h_active_pixels=widths[mode],.v_active_lines=heights[mode]};
        video_output_active_mode=&output;
        unsigned scale=mode==0?4:mode==3?3:2;
        unsigned vscale=mode==0?1:mode==3?3:2;
        unsigned left=(widths[mode]-256*scale)/2;
        for(unsigned h=224;h<=239;h+=15) {
            line_ring_init(); line_ring_vsync();
            for(unsigned y=0;y<h;++y)
                for(unsigned x=0;x<256;++x)
                    line_ring_write_ptr(y)[x]=(uint16_t)(y+1);
            line_ring_publish(h);
            fast_osd_init();
            for(unsigned visible=0;visible<2;++visible) {
                osd_visible=visible;
                fast_osd_puts_color(0,0,"PAL",OSD_COLOR_GREEN);
                vsync_callback();
                uint32_t guarded[642];
                for(unsigned y=0;y<heights[mode];++y) {
                    for(unsigned x=0;x<642;++x) guarded[x]=0xdeadbeef;
                    scanline_callback(0,y,guarded+1);
                    assert(guarded[0]==0xdeadbeef);
                    assert(guarded[widths[mode]/2+1]==0xdeadbeef);
                    const uint16_t *pixels=(const uint16_t *)(guarded+1);
                    int row=video_source_row(y,heights[mode],h,vscale);
                    if(!visible) {
                        for(unsigned x=0;x<widths[mode];++x) {
                            uint16_t expected=(row>=0 && x>=left && x<left+256*scale)?(uint16_t)(row+1):0;
                            assert(pixels[x]==expected);
                        }
                    }
                }
            }
        }
    }
    assert(superpico_mode_576p50.h_total_pixels * superpico_mode_576p50.v_total_lines * 50U == 27000000U);
}
int main(void) {
    test_modes(); test_render();
    puts("PASS: auto/manual reboot policy, confirmation persistence, production scanline renderer and guards");
}
