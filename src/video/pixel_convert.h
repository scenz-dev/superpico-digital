#ifndef PIXEL_CONVERT_H
#define PIXEL_CONVERT_H
#include <stdint.h>
static inline uint32_t snes_reverse_5bit(uint32_t x) {
    return ((x & 1U) << 4) | ((x & 2U) << 2) | (x & 4U) |
           ((x & 8U) >> 2) | ((x & 16U) >> 4);
}
static inline uint16_t snes_pack_rgb565(uint32_t r, uint32_t g, uint32_t b) {
    return (uint16_t)((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
}
#endif
