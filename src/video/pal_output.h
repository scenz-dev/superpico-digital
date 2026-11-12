#ifndef PAL_OUTPUT_H
#define PAL_OUTPUT_H
#include "pico_hdmi/video_output_rt.h"
/* CTA VIC 17: 720x576p50, 27 MHz = 270 MHz / 2 / 5.
 * New operating point requires physical validation on the target board. */
static const video_mode_t superpico_mode_576p50 = {
    .h_front_porch = 12, .h_sync_width = 64, .h_back_porch = 68,
    .h_active_pixels = 720,
    .v_front_porch = 5, .v_sync_width = 5, .v_back_porch = 39,
    .v_active_lines = 576, .h_total_pixels = 864, .v_total_lines = 625,
    .hstx_clk_div = 2, .hstx_csr_clkdiv = 5,
    .sync_positive = false, .data_island_in_hsync = true,
};
#endif
