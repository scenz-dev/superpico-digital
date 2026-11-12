#ifndef INPUT_TIMING_H
#define INPUT_TIMING_H
#include <stdbool.h>
#include <stdint.h>
/* Timing families, not cartridge regions. Includes PAL-clock 60 Hz mods. */
typedef enum { INPUT_UNKNOWN = 0, INPUT_60HZ = 1, INPUT_50HZ = 2 } input_region_t;
typedef struct {
    uint32_t candidate, locked;
    uint8_t consecutive;
} input_detector_t;
#define INPUT_LOCK_FRAMES 8U
#define INPUT_MAX_HEIGHT 239U
static inline uint32_t input_profile(input_region_t r, uint16_t h) {
    return ((uint32_t)r << 16) | h;
}
static inline input_region_t input_region(uint32_t p) { return (input_region_t)(p >> 16); }
static inline uint16_t input_height(uint32_t p) { return (uint16_t)p; }
/* VBLANK-low includes pre-render. Disjoint windows tolerate PPU/clock variants.
 * These signals alone do not identify hires or distinguish interlace fields. */
static inline uint16_t input_active_height(uint32_t us) {
    if (us >= 13900U && us <= 14600U) return 224;
    if (us >= 14800U && us <= 15500U) return 239;
    return 0;
}
static inline uint32_t input_classify(uint32_t period, uint32_t active) {
    uint16_t h = input_active_height(active);
    if (!h) return 0;
    if (period >= 15500U && period <= 17500U) return input_profile(INPUT_60HZ, h);
    if (period >= 19000U && period <= 21000U) return input_profile(INPUT_50HZ, h);
    return 0;
}
static inline void input_detector_reset(input_detector_t *d) { *d = (input_detector_t){0}; }
static inline void input_detector_observe(input_detector_t *d, uint32_t period, uint32_t active) {
    uint32_t p = input_classify(period, active);
    if (!p || p != d->candidate) {
        d->locked = 0;
        d->candidate = p;
        d->consecutive = p ? 1 : 0;
    } else if (d->consecutive < INPUT_LOCK_FRAMES) ++d->consecutive;
    if (d->consecutive >= INPUT_LOCK_FRAMES) d->locked = p;
}
#endif
