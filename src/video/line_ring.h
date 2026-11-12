#ifndef LINE_RING_H
#define LINE_RING_H
#include "pico/critical_section.h"
#include "video_config.h"
#include "input_timing.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* Triple mailbox: capturing, latest completed, displayed. No pixel copies
 * under the lock; ownership transfers only at frame boundaries. */
#define FRAME_BUFFER_COUNT 3
typedef struct {
    uint16_t pixels[FRAME_BUFFER_COUNT][INPUT_MAX_HEIGHT][VIDEO_WIDTH];
    critical_section_t lock;
    int8_t writing, pending, reading;
    uint16_t heights[FRAME_BUFFER_COUNT];
    bool signal_valid;
    uint32_t dropped, repeated;
} line_ring_t;
extern line_ring_t g_line_ring;
static inline void line_ring_init(void) {
    memset(&g_line_ring, 0, sizeof(g_line_ring));
    g_line_ring.writing = g_line_ring.pending = g_line_ring.reading = -1;
    critical_section_init(&g_line_ring.lock);
}
static inline void line_ring_vsync(void) {
    critical_section_enter_blocking(&g_line_ring.lock);
    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        if (i != g_line_ring.reading && i != g_line_ring.pending) {
            g_line_ring.writing = i;
            break;
        }
    }
    critical_section_exit(&g_line_ring.lock);
}
static inline uint16_t *line_ring_write_ptr(uint16_t y) {
    return g_line_ring.pixels[g_line_ring.writing][y];
}
static inline void line_ring_publish(uint16_t height) {
    critical_section_enter_blocking(&g_line_ring.lock);
    if (g_line_ring.pending >= 0) ++g_line_ring.dropped;
    g_line_ring.heights[g_line_ring.writing] = height;
    g_line_ring.pending = g_line_ring.writing;
    g_line_ring.writing = -1;
    g_line_ring.signal_valid = true;
    critical_section_exit(&g_line_ring.lock);
}
static inline void line_ring_invalidate(void) {
    critical_section_enter_blocking(&g_line_ring.lock);
    g_line_ring.signal_valid = false;
    g_line_ring.pending = -1;
    g_line_ring.writing = -1;
    critical_section_exit(&g_line_ring.lock);
}
static inline void line_ring_output_vsync(void) {
    critical_section_enter_blocking(&g_line_ring.lock);
    if (!g_line_ring.signal_valid) g_line_ring.reading = -1;
    else if (g_line_ring.pending >= 0) {
        g_line_ring.reading = g_line_ring.pending;
        g_line_ring.pending = -1;
    } else if (g_line_ring.reading >= 0) ++g_line_ring.repeated;
    critical_section_exit(&g_line_ring.lock);
}
static inline uint16_t line_ring_height(void) {
    return g_line_ring.reading >= 0 ? g_line_ring.heights[g_line_ring.reading] : 224;
}
static inline bool line_ring_ready(uint16_t y) {
    return g_line_ring.reading >= 0 && y < line_ring_height();
}
static inline const uint16_t *line_ring_read_ptr(uint16_t y) {
    return g_line_ring.pixels[g_line_ring.reading][y];
}
#endif
