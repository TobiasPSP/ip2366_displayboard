#ifndef ZH_FONT_H
#define ZH_FONT_H

#include <stdint.h>

uint16_t zh_font_draw_text16(uint16_t x, uint16_t y, const char *utf8, uint16_t fg, uint16_t bg);
uint16_t zh_font_draw_text16_ref(uint16_t x, uint16_t y, const char *utf8, uint16_t fg, uint16_t bg);
uint16_t zh_font_draw_text16_ref_scaled(uint16_t x, uint16_t y, const char *utf8, uint16_t fg, uint16_t bg, uint8_t scale);
uint16_t zh_font_draw_text8(uint16_t x, uint16_t y, const char *utf8, uint16_t fg, uint16_t bg);

#endif
