# PAL/NTSC automatic output — implementation and validation

Status: software implementation, cross-compiled and host-tested on 2026-11-12.
**Not tested on a physical SNES/RP2350 or HDMI sink.** The UF2 is a bench-test build,
not a hardware-validated release. No console was flashed.

## Behavior

- The new default **Auto 50/60 Hz** observes PPU VBLANK timing, not cartridge region
  or the PALMODE pin. A region-switched console can change between timing families.
- Eight consistent complete measurements are required before lock. Roughly 50 Hz
  input selects 720x576p50; roughly 60 Hz input selects 640x480p60. The 60 Hz window
  includes PAL-clock 60 Hz modifications around 59.55 Hz.
- Output timing changes use the existing watchdog reboot mechanism. The mod
  restarts and HDMI/audio briefly stop; the SNES itself is not reset or sped up.
  A cold Auto boot initially outputs 480p60, then restarts into 576p50 if needed.
- Active height is detected separately: 224 or 239 lines, in either timing family.
  The PIO sampling edge and horizontal skip remain unchanged.
- Manual 480p60, 576p50, 240p and 720p choices override automatic output switching.
  Input capture still detects 224/239 lines. Manual 60 Hz output with PAL repeats
  complete frames; it does not speed up audio or gameplay.
- Hold **BACK during power-on** to recover a screen that rejects 576p50. This
  selects and saves manual 480p60. Select Auto again in the menu to re-enable it.
- Settings format is version 2. Version-1 settings deliberately default to Auto
  on upgrade. A preview is never persisted; Keep schedules a reboot and saves
  before core 1 starts. Timeout, Revert, or power loss during a preview retains
  the prior persisted setting. An invalid resolution value falls back to Auto.

This milestone supports the existing 256-pixel progressive capture path. It does
not establish 512-pixel/hires or interlace compatibility, add 288p, implement
brightness/Mode 7 fixes, or genlock HDMI to the source. VBLANK timing alone cannot
identify every PPU screen mode. Ordinary source/output clock mismatch is handled
with whole-frame repeats/drops, with additional buffering latency.

## Design

`input_timing.h` classifies measured VBLANK falling-to-falling periods and low
intervals. No HBLANK/PCLK CPU interrupts are added: only 100/120 VBLANK edges per
second interrupt core 0. Low intervals of 13.9–14.6 ms map to 224 lines;
14.8–15.5 ms map to 239. Frame windows are 15.5–17.5 ms and 19–21 ms. These
intentionally disjoint acceptance windows must be verified with real PPU traces.
Invalid measurements clear lock, and reacquisition is debounced. Unsigned timer
differences handle microsecond-counter wrap.

The producer captures into one of three frame buffers. Only completed, validated
frames are published, and HDMI acquires one at output vsync. A spinlock protects
ownership transfers; pixels are never copied under that lock. A displayed buffer
cannot be reused by capture. The latest completed frame wins if HDMI is slower.
Signal loss invalidates the mailbox and leaves HDMI/OSD running. DMA line waits
are bounded to 200 us; VBLANK acquisition is bounded to 100 ms per phase. Recovery
aborts DMA, clears the FIFO/IRQ, and reloads PIO state before the next capture.

Three 256x239 RGB565 frames occupy 367,104 bytes. To make room, the 64 KiB color
lookup became three 32-entry channel tables, and the 56 KiB OSD framebuffer became
a glyph grid rendered one line at a time. The renderer is in normal RAM rather
than scratch X, reserving space there for the backend and core-1 stack.

576p50 uses a 27 MHz pixel clock, 864x625 total timing, negative sync, VIC 17
(4:3), and no pixel repetition. The new 270 MHz system-clock operating point uses
HSTX dividers 2 and 5. This is an overclock and requires board-level validation.
The 64-clock horizontal sync has room for the existing 8-clock preamble and
36-clock data island. Audio scheduling/ACR continues to use the backend's actual
output timing; no 50/60 multiplier is applied to the audio source.

`lib/` and the pinned submodule revision are unchanged. CMake generates an adapted
`video_output_rt.c` in the build directory, adding the 576p AVI identification.
The generator verifies the exact reviewed source blob and fails on a different
version. It accepts a separate read-only dependency checkout via
`SUPERPICO_HDMI_SOURCE_DIR`.

## Confirmed bugs fixed

- RGB565 green replication previously leaked into the blue bit; full green is
  now `0x07e0`.
- Audio linear interpolation uses a 64-bit multiply for full-scale transitions.
- SRC preserves phase across input chunks and pending repeated samples across
  output-capacity boundaries.
- Audio output retains data when the HDMI queue is full. Pending input is also
  preserved; processing is bounded per background call instead of draining in
  an unbounded loop.
- Audio startup and rate-control intervals use elapsed microseconds, so PAL does
  not lengthen them simply by having fewer video frames per second.
- Resolution previews no longer write flash before confirmation. Flash writes
  occur at boot before the second core starts.
- Missing input clocks no longer leave capture blocked in an infinite DMA wait.
- Output maps every 224/239 source line without unsigned crop errors. In 576p,
  239 doubled lines have exactly 49 border lines above and below.

## Reproducing the build

Base application commit: `f5d52d541c92c2680605f081105d39205aeed778`.
Dependency: `fliperama86/pico_hdmi` at
`f8034c7ae5e4927692229d114b84b6e81cc789d9`.
Validated compiler: Arm GNU 14.3.Rel1. Validated SDK: Pico SDK 2.2.0, including its
pinned TinyUSB dependency. Board: `weact_studio_rp2350b_core` (RP2350B).

With the SDK and pinned HDMI checkout already available:

```sh
cmake -S . -B build-pal \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DPICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain \
  -DSUPERPICO_HDMI_SOURCE_DIR=/path/to/pinned-pico-hdmi \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-pal -j4
SUPERPICO_HDMI_SOURCE_DIR=/path/to/pinned-pico-hdmi sh tests/run_host_tests.sh
```

The dependency argument can be omitted when the existing submodule is already
initialized. The build never patches that checkout. The UF2 is generated at
`build-pal/src/superpico-digital.uf2`. `SUPERPICO_COPY_TO_RAM` remains ON.

## Validation performed

- Release ARM firmware compiled and linked; RP2350 ARM Secure UF2 identified by
  picotool, with the expected board and SDK metadata.
- Linker map: main SRAM allocations end at `0x2007cd58`, leaving 12,968 bytes
  before `0x20080000`. Scratch X/Y and their 2 KiB stacks fit. Runtime stack
  high-water and interrupt deadlines still require hardware measurements.
- Host tests with undefined-behavior sanitizer: classification/debounce, modified
  60 Hz timing, malformed signals and timer wrap; all 32,768 RGB555 colors;
  all rows at both source heights across 240p/480p/576p/720p; 96,000 simulated
  producer/consumer frame interleavings; audio full-scale arithmetic and output
  invariance across input chunk sizes and output capacities.
- Tests also execute the production scanline renderer with guard words, with
  and without OSD, and exercise Auto/manual watchdog requests and the preview
  versus confirmed-save state transitions.
- `git diff --check` passes. These are software checks, not waveform simulation,
  multicore stress measurements, or HDMI compliance certification.

## Hardware acceptance checklist

1. Verify the actual SNES motherboard/PPUs, RP2350 board, and level conversion.
   GPIO40–44 must not be assumed 5 V tolerant. Confirm the input circuit before
   connecting the console; this build makes no wiring changes.
2. Measure VBLANK period/low duration, PCLK edges, signal voltage and sampling
   margin at both output clock settings. Confirm the classification windows.
3. Use PAL and NTSC 224/239-line border patterns to verify the first and last
   captured line. The existing first-line alignment is preserved but still
   requires a physical test, particularly on a new motherboard revision.
4. Switch 50/60 Hz during operation; check lock, automatic reboot, correct HDMI
   mode, unchanged gameplay speed, and audio recovery. Exercise reset and loss
   of PCLK/VBLANK; HDMI and the menu must remain responsive.
5. Test at least 15 minutes for rate-mismatch events and one hour for temperature
   stability, with OSD open and audio running. Measure queue/stack headroom and
   scanline deadlines, especially in the existing experimental 720p mode.
6. Test multiple TVs/capture devices. Verify Keep, Revert, power loss during
   preview, and BACK-at-power-on recovery. No automatic flash writes should occur
   merely because the source switches timing families.
