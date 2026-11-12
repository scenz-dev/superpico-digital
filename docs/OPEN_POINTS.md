# Open-point audit — 2026-11-12

Scope: README checklist, scratchbook, pin-reference proposals, current firmware,
and open GitHub issues in scenz-dev/superpico-digital (none returned).
Historical scratchbook entries are not a reliable statement of current behavior.

| Point | Current finding | Implementation / next requirement |
|---|---|---|
| Digital audio | Capture and SRC already exist; the README checkbox was stale | Corrected status. Reserved audio PIO SM, bounded DMA polling to 32 stereo pairs, removed the unnecessary 16 KiB restart clear, cleared stale rate diagnostics |
| Audio corruption | Old notes report malformed LRCK even on a scope | Firmware cannot reconstruct missing channel framing reliably; obtain a fresh trace before changing sample edges or padding |
| Master brightness | No brightness register/latch inputs in the current capture wiring | Requires additional hardware signals; implementation design below |
| Forced blank | Four brightness bits cannot convey INIDISP bit 7 | Capture D7 separately and associate it with pixels; do not infer it from brightness zero |
| Mode 7 overflow | Proposed universal AND mask is not established by the reference implementation | Capture register context and validate the hardware patch path before implementation |
| TOUMEI / star animation | Pin interpretation and universal masking claims remain unverified | Measure TST, OVER and TOUMEI together with known scenes; preserve backdrop/lower-layer composition |
| PAL/NTSC auto | Implemented in prior commit, software tests passed | Physical first/last-line alignment, clocks, HDMI sinks and long-run audio still open |
| Frequency counter | Dormant code has resource/GPIO-base assumptions incompatible with simply enabling it | Keep disabled; use a separately allocated PIO/resource plan before wiring it into rate control |
| Hires/interlace | Existing acquisition is fixed at 256 samples per line | Separate capture/storage design; VBLANK timing alone cannot identify all modes |
| PCB DRC | Scratchbook contains historical clearance and externally-common supply-net discussions | Re-run DRC on the actual board revision; do not modify copper based solely on historical notes |

## Brightness and forced-blank implementation design

Observe writes to INIDISP ($2100) using address decode plus /PAWR, retaining both
D[3:0] and D7. The currently documented four-bit latch is insufficient for full
blanking support. Confirm actual GPIO allocation, voltage conversion, reset
state, write-edge setup/hold and hardware revision before enabling acquisition.

Attach captured state to the source pixels before frame publication. Applying a
live brightness value at HDMI scanout would incorrectly affect older buffered
frames. A once-per-frame snapshot cannot reproduce raster effects. A first
line-latched implementation could support ordinary fades, but must explicitly
exclude writes within a line; faithful handling requires event timestamps or
sampling brightness/blanking alongside pixels.

Use small per-channel 16x32 tables rather than duplicating full-color LUTs.
A linear brightness multiplier can be offered as an approximation, but measured
PPU behavior distinguishes brightness zero from forced black. D7 must override
the picture independently. Validate all brightness levels, fades, forced blank,
and mid-line writes against analog output before claiming fidelity.

Primary research: [INIDISP experiments](https://www.undisbeliever.net/snesdev/registers/inidisp.html).

## Mode 7 and TOUMEI implementation design

The [SNES_TST RTL](https://github.com/Opatusos/SNES_TST/blob/main/Firmware/rtl/SNES_TST.v)
tracks writes to addresses $05 and $1A, combines mode and screen-over state,
and uses OVER1 to control a hardware patch output. It is not merely a software
operation replacing every transparent pixel with black. TOUMEI is commented
out as an uncertain input in that source.

Therefore the repository's universal /OVER AND /TOUMEI PIXEL_VALID proposal is
not sufficient evidence for a firmware fix. First record register writes and
pixel/sync signals during overflow, wrapping, backdrop and sprite cases. Determine
whether the necessary correction is upstream at the PPU test interface or can be
performed on captured RGB. Information absent from the test bus cannot generally
be recovered by a downstream mask. Test Super Mario Kart/F-Zero and the reported
SMW star animation independently; one passing scene does not validate all cases.

The pin-reference document now prominently marks these proposals unverified.
No new GPIO assignment or console signal-driving circuit is enabled by this audit.

## Audio changes and remaining checks

PIO SM2 is now cooperatively claimed before resource use, preventing the dormant
frequency-counter code from silently allocating it later. DMA polling consumes at
most 32 stereo pairs per invocation and retains its position for the next call.
This bounds one previously unbounded loop on the HDMI core. Restart does not
clear the entire DMA buffer: reset read/write positions delimit new samples.

These changes do not fix a malformed physical LRCK signal, nor do they establish
DMA-overrun immunity after arbitrarily long CPU stalls. Full DMA-ring laps are
still indistinguishable from an empty ring using write position alone. A future
robust overrun detector needs a monotonic transfer/completion counter, with the
RP2350 endless-transfer semantics explicitly accounted for.

Validation: ARM Release build passed; existing UBSan host suites passed. Those
suites cover SRC, frame ownership, geometry and mode control, not physical PIO/DMA
execution. Required bench tests: occupied-SM initialization failure, restart with
old nonzero DMA contents, sustained backlog while HDMI remains stable, signal
pause/resume and LRCK integrity. No hardware fix is marked complete without these.
