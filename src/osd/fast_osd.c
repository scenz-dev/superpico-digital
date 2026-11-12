#include "fast_osd.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "font_8x8.h"
#include "pico.h"

#ifndef NEOPICO_EXP_RAM_SELECTOR_UI
#define NEOPICO_EXP_RAM_SELECTOR_UI 0
#endif

#if NEOPICO_EXP_RAM_SELECTOR_UI
#define FAST_OSD_RENDER_RAM(name) __not_in_flash_func(name)
#else
#define FAST_OSD_RENDER_RAM(name) name
#endif

volatile bool osd_visible = false;


// Text grid: one NUL-terminated string per row.
static char fast_osd_text[FAST_OSD_ROWS][FAST_OSD_COLS + 1];
// Foreground color per cell.
static uint16_t fast_osd_color[FAST_OSD_ROWS][FAST_OSD_COLS];

static inline bool fast_osd_in_bounds(uint8_t row, uint8_t col)
{
    return row < FAST_OSD_ROWS && col < FAST_OSD_COLS;
}

static inline uint8_t fast_osd_normalize_char(char c)
{
    const uint8_t ch = (uint8_t)c;
    if (ch == (uint8_t)FAST_OSD_GLYPH_CHECK || ch == (uint8_t)FAST_OSD_GLYPH_CROSS) {
        return ch;
    }
    if (ch < 32U || ch > 126U) {
        return (uint8_t)' ';
    }
    return ch;
}

/* Render one glyph row on demand: saves 56 KiB for frame ownership. */
void fast_osd_render_line(uint32_t line, uint16_t *dst)
{
    if (line >= OSD_BOX_H) return;
    const uint32_t row = line / 8U;
    for (uint32_t col = 0; col < FAST_OSD_COLS; ++col) {
        uint8_t ch = fast_osd_normalize_char(fast_osd_text[row][col]);
        uint8_t bits = font8x8[ch][line % 8U];
        uint16_t color = fast_osd_color[row][col];
        for (uint32_t x = 0; x < 8; ++x)
            dst[col * 8U + x] = (bits & (0x80U >> x)) ? color : OSD_COLOR_BG;
    }
}

void FAST_OSD_RENDER_RAM(fast_osd_clear)(void)
{
    for (uint8_t r = 0; r < FAST_OSD_ROWS; r++) {
        memset(fast_osd_text[r], ' ', FAST_OSD_COLS);
        for (uint8_t c = 0; c < FAST_OSD_COLS; c++) {
            fast_osd_color[r][c] = OSD_COLOR_FG;
        }
        fast_osd_text[r][FAST_OSD_COLS] = '\0';
    }
}

void fast_osd_init(void)
{
    fast_osd_clear();
}

void FAST_OSD_RENDER_RAM(fast_osd_putc)(uint8_t row, uint8_t col, char c)
{
    fast_osd_putc_color(row, col, c, OSD_COLOR_FG);
}

void FAST_OSD_RENDER_RAM(fast_osd_putc_color)(uint8_t row, uint8_t col, char c, uint16_t color)
{
    if (!fast_osd_in_bounds(row, col)) {
        return;
    }

    const char norm = (char)fast_osd_normalize_char(c);
    if (fast_osd_text[row][col] == norm && fast_osd_color[row][col] == color) {
        return; // Nothing changed; skip render.
    }

    fast_osd_text[row][col] = norm;
    fast_osd_color[row][col] = color;

}

void FAST_OSD_RENDER_RAM(fast_osd_puts)(uint8_t row, uint8_t col, const char *text)
{
    fast_osd_puts_color(row, col, text, OSD_COLOR_FG);
}

void FAST_OSD_RENDER_RAM(fast_osd_puts_color)(uint8_t row, uint8_t col, const char *text, uint16_t color)
{
    if (!text || row >= FAST_OSD_ROWS || col >= FAST_OSD_COLS) {
        return;
    }
    while (*text && col < FAST_OSD_COLS) {
        fast_osd_putc_color(row, col, *text++, color);
        col++;
    }
}

const char *fast_osd_get_row(uint8_t row)
{
    if (row >= FAST_OSD_ROWS) {
        return "";
    }
    return fast_osd_text[row];
}
