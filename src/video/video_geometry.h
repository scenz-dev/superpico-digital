#ifndef VIDEO_GEOMETRY_H
#define VIDEO_GEOMETRY_H
#include <stdint.h>
/* -1 is border; center at OUTPUT pixel precision (e.g. 239*2 in 576
 * has exactly 49 black lines above and below). */
static inline int32_t video_source_row(uint32_t line, uint32_t output_height,
                                      uint32_t input_height, uint32_t scale) {
    if (!scale || input_height * scale > output_height || line >= output_height) return -1;
    uint32_t top = (output_height - input_height * scale) / 2U;
    if (line < top || line - top >= input_height * scale) return -1;
    return (int32_t)((line - top) / scale);
}
#endif
