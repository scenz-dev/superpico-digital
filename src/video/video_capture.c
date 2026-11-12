#include "video_capture.h"
#include "config.h"
#if ENABLE_AUDIO
#include "audio/audio_pipeline.h"
#endif
#include "input_timing.h"
#include "pixel_convert.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "snes_pins.h"
#include "snes_timing.h"
#include "video_capture.pio.h"
#include "video_pipeline.h"
#include <stdio.h>
#include <string.h>

// =============================================================================
// State
// =============================================================================

static input_detector_t g_detector;
static volatile uint32_t g_profile;
static volatile uint32_t g_active_us;
static uint32_t g_last_fall_us;
static bool g_have_fall, g_have_rise;

/* Only VBLANK edges interrupt the CPU (100/120 IRQ/s). PCLK stays in PIO.
 * Unsigned differences remain correct across the 32-bit microsecond wrap. */
static void vblank_irq(uint gpio, uint32_t events) {
  if (gpio != PIN_SNES_VBLANK) return;
  uint32_t now = time_us_32();
  if (events == GPIO_IRQ_EDGE_RISE && g_have_fall) {
    g_active_us = now - g_last_fall_us;
    g_have_rise = true;
  } else if (events == GPIO_IRQ_EDGE_FALL) {
    if (g_have_fall && g_have_rise)
      input_detector_observe(&g_detector, now - g_last_fall_us, g_active_us);
    else input_detector_reset(&g_detector);
    g_profile = g_detector.locked;
    g_last_fall_us = now;
    g_have_fall = true;
    g_have_rise = false;
  } else {
    input_detector_reset(&g_detector);
    g_profile = 0;
    g_have_fall = g_have_rise = false;
  }
}
static PIO g_pio_snes = pio1;

static uint g_sm_pixel = 0;
static uint g_offset_pixel = 0;
static pio_sm_config g_pio_config;

static int g_dma_chan = -1;
static uint32_t g_line_buffers[2][SNES_H_TOTAL];
static volatile uint32_t g_frame_count = 0;

#ifndef ENABLE_AUDIO_REARM_ON_VIDEO_REACQUIRE
#define ENABLE_AUDIO_REARM_ON_VIDEO_REACQUIRE 0
#endif

// =============================================================================
// Pixel Conversion - RGB555 to RGB565 LUT
// =============================================================================
// PCB wires MSB (R4/G4/B4) to lower GPIO, so each 5-bit channel is bit-reversed.
// Same pattern as neopico-hd.

/* Three small channel LUTs replace the 64 KiB full-color LUT. */
static uint16_t g_red[32], g_green[32], g_blue[32];
static void generate_pixel_lut(void) {
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t c = snes_reverse_5bit(i);
    g_red[i] = snes_pack_rgb565(c, 0, 0);
    g_green[i] = snes_pack_rgb565(0, c, 0);
    g_blue[i] = snes_pack_rgb565(0, 0, c);
  }
}
static inline uint16_t convert_pixel(uint32_t raw) {
  return g_red[(raw >> 12) & 31U] | g_green[(raw >> 7) & 31U] |
         g_blue[(raw >> 2) & 31U];
}

// =============================================================================
// Internal Helpers
// =============================================================================

static void video_capture_reset_hardware(void) {
  pio_sm_set_enabled(g_pio_snes, g_sm_pixel, false);
  pio_sm_clear_fifos(g_pio_snes, g_sm_pixel);
  pio_sm_init(g_pio_snes, g_sm_pixel, g_offset_pixel, &g_pio_config);
  // Reapply IN_BASE after pio_sm_init (which resets pinctrl).
  // pin index 11 = GP27 (VBLANK) with GPIOBASE=16.
  uint pin_idx = PIN_SNES_BASE - 16;
  g_pio_snes->sm[g_sm_pixel].pinctrl =
      (g_pio_snes->sm[g_sm_pixel].pinctrl & ~0x000f8000u) | (pin_idx << 15);
  pio_sm_set_enabled(g_pio_snes, g_sm_pixel, true);
  pio_sm_put_blocking(g_pio_snes, g_sm_pixel, SNES_H_ACTIVE - 1);
}

// =============================================================================
// Public API
// =============================================================================

void video_capture_init(void) {
  generate_pixel_lut();

  pio_clear_instruction_memory(g_pio_snes);

  // RP2350: reset both PIO blocks, then force PIO1 GPIOBASE=16 via direct
  // register write (offset 0x168) so PIO can reach GP44 with 5-bit indices.
  // Pattern matches neopico-hd reference implementation.
  pio_set_gpio_base(pio0, 0);
  pio_set_gpio_base(pio1, 0);
  *(volatile uint32_t *)((uintptr_t)g_pio_snes + 0x168) = 16;

  g_offset_pixel = pio_add_program(g_pio_snes, &snes_hard_sync_program);
  g_sm_pixel = pio_claim_unused_sm(g_pio_snes, true);

  // Initialize all capture GPIOs GP27-GP44 (VBLANK, PCLK, B, G, R, HBLANK).
  for (uint pin = PIN_SNES_BASE; pin <= SNES_CAPTURE_PIN_LAST; pin++) {
    pio_gpio_init(g_pio_snes, pin);
    gpio_disable_pulls(pin);
    gpio_set_input_enabled(pin, true);
    gpio_set_input_hysteresis_enabled(pin, true);
  }

  g_pio_config = snes_hard_sync_program_get_default_config(g_offset_pixel);
  sm_config_set_clkdiv(&g_pio_config, 1.0f);
  // Autopush every 18 bits (one full capture word per pixel).
  sm_config_set_in_shift(&g_pio_config, false, true, SNES_CAPTURE_BITS);

  // video_capture_reset_hardware() calls pio_sm_init and applies IN_BASE.
  video_capture_reset_hardware();

  g_dma_chan = dma_claim_unused_channel(true);
  dma_channel_config dc = dma_channel_get_default_config(g_dma_chan);
  channel_config_set_read_increment(&dc, false);
  channel_config_set_write_increment(&dc, true);
  channel_config_set_dreq(&dc, pio_get_dreq(g_pio_snes, g_sm_pixel, false));
  dma_channel_configure(g_dma_chan, &dc, g_line_buffers[0],
                        &g_pio_snes->rxf[g_sm_pixel], SNES_H_ACTIVE, false);
  gpio_set_irq_enabled_with_callback(PIN_SNES_VBLANK,
      GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, vblank_irq);
}

static bool wait_vblank(bool level, uint32_t timeout_us) {
  uint32_t start = time_us_32();
  while (gpio_get(PIN_SNES_VBLANK) != level) {
    if ((uint32_t)(time_us_32() - start) >= timeout_us) return false;
    tight_loop_contents();
  }
  return true;
}

static void capture_stop(void) {
  pio_sm_set_enabled(g_pio_snes, g_sm_pixel, false);
  dma_channel_abort(g_dma_chan);
  pio_sm_clear_fifos(g_pio_snes, g_sm_pixel);
  pio_interrupt_clear(g_pio_snes, 4);
}

static void capture_lost(void) {
  capture_stop();
  line_ring_invalidate();
  uint32_t irq = save_and_disable_interrupts();
  input_detector_reset(&g_detector);
  g_profile = 0;
  g_have_fall = g_have_rise = false;
  restore_interrupts(irq);
}

void video_capture_run(void) {
  bool reacquiring = true;
  while (true) {
    if (!wait_vblank(true, 100000U) || !wait_vblank(false, 100000U)) {
      capture_lost();
      reacquiring = true;
      continue;
    }
    uint32_t profile = g_profile;
    if (!profile) {
      line_ring_invalidate();
      continue;
    }
#if ENABLE_REBOOT_MODE_SWITCH
    video_pipeline_follow_input(input_region(profile));
#endif
#if ENABLE_AUDIO && ENABLE_AUDIO_REARM_ON_VIDEO_REACQUIRE
    if (reacquiring) audio_pipeline_request_rearm();
#endif
    reacquiring = false;
    const uint16_t height = input_height(profile);
    line_ring_vsync();
    capture_stop();
    video_capture_reset_hardware();
    dma_channel_set_trans_count(g_dma_chan, SNES_H_ACTIVE, false);
    dma_channel_set_write_addr(g_dma_chan, g_line_buffers[0], true);
    pio_sm_exec(g_pio_snes, g_sm_pixel, pio_encode_irq_set(false, 4));

    bool complete = true;
    uint32_t buf_idx = 0;
    for (uint16_t y = 0; y < height; ++y) {
      uint32_t start = time_us_32();
      while (dma_channel_is_busy(g_dma_chan)) {
        if ((uint32_t)(time_us_32() - start) >= 200U) {
          complete = false;
          break;
        }
      }
      if (!complete) break;
      uint32_t *src = g_line_buffers[buf_idx];
      buf_idx ^= 1U;
      if (y + 1U < height) {
        dma_channel_set_trans_count(g_dma_chan, SNES_H_ACTIVE, false);
        dma_channel_set_write_addr(g_dma_chan, g_line_buffers[buf_idx], true);
      }
      /* Reject a line if capture has crossed into vertical blanking. */
      if ((src[0] | src[SNES_H_ACTIVE - 1U]) & 1U) {
        complete = false;
        break;
      }
      uint16_t *dst = line_ring_write_ptr(y);
      for (uint32_t x = 0; x < SNES_H_ACTIVE; x += 4U) {
        dst[x] = convert_pixel(src[x]);
        dst[x + 1] = convert_pixel(src[x + 1]);
        dst[x + 2] = convert_pixel(src[x + 2]);
        dst[x + 3] = convert_pixel(src[x + 3]);
      }
    }
    capture_stop();
    /* Validate THIS frame's active interval before publishing. A runtime
     * overscan change must never publish a cropped or partial old profile. */
    if (!complete || !wait_vblank(true, 20000U) ||
        input_active_height(g_active_us) != height) {
      capture_lost();
      reacquiring = true;
      continue;
    }
    line_ring_publish(height);
    ++g_frame_count;
    if ((g_frame_count % 60U) == 0) gpio_xor_mask(1ul << PICO_DEFAULT_LED_PIN);
  }
}

uint32_t video_capture_get_frame_count(void) { return g_frame_count; }
uint32_t video_capture_get_profile(void) { return g_profile; }
