#ifndef ST7735_H
#define ST7735_H

#include <stdbool.h>
#include <stdint.h>

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

enum {
  COLOR_BLACK = 0x0000,
  COLOR_WHITE = 0xFFFF,
  COLOR_CYAN = 0x07FF,
  COLOR_BLUE = 0x041F,
  COLOR_NAVY = 0x0010,
  COLOR_GREEN = 0x07E0,
  COLOR_LIME = 0x87F0,
  COLOR_YELLOW = 0xFFE0,
  COLOR_ORANGE = 0xFD20,
  COLOR_ORANGE_R = 0xFA20,
  COLOR_ORANGE_Y = 0xFE40,
  COLOR_PURPLE = 0xF81F,
  COLOR_RED = 0xF800,
  COLOR_GRAY = 0x8410,
  COLOR_DARK = 0x1082
};

void st7735_init(void);
void st7735_reinit(void);
void st7735_display_enable(bool on);
void st7735_set_rotation(uint8_t mode);
void st7735_set_offsets(uint16_t x_offset, uint16_t y_offset);
void st7735_set_brightness(uint8_t percent);
void st7735_fill_screen(uint16_t color);
void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void st7735_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void st7735_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void st7735_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void st7735_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
void st7735_draw_char8x16(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);
void st7735_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale);
void st7735_draw_glyph16(uint16_t x, uint16_t y, const uint8_t glyph[32], uint16_t fg, uint16_t bg);

#endif
