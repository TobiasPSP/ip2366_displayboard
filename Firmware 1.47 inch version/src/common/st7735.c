#include "st7735.h"
#include "board.h"
#include "project_config.h"
#include "font8x16_ref.h"

static uint16_t g_x_offset = 0;
static uint16_t g_y_offset = 34;
static uint8_t g_madctl = 0x70;
static uint16_t g_width = SCREEN_WIDTH;
static uint16_t g_height = SCREEN_HEIGHT;
static uint8_t g_brightness_percent = 100;
static uint8_t g_color_buf[128];

static const uint8_t font5x7[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08}
};

static void lcd_cs_low(void){ board_lcd_cs_set(false); }
static void lcd_cs_high(void){ board_lcd_cs_set(true); }
static void lcd_dc_cmd(void){ board_lcd_dc_set(false); }
static void lcd_dc_data(void){ board_lcd_dc_set(true); }

static void lcd_write8(uint8_t data)
{
  board_spi_write8(data);
}

static uint16_t apply_brightness(uint16_t color);

static void lcd_write_command(uint8_t cmd)
{
  lcd_cs_low();
  lcd_dc_cmd();
  lcd_write8(cmd);
  lcd_cs_high();
}

static void lcd_write_data8(uint8_t data)
{
  lcd_cs_low();
  lcd_dc_data();
  lcd_write8(data);
  lcd_cs_high();
}

static void lcd_write_color_repeat(uint16_t color, uint32_t count)
{
  uint32_t i;

  color = apply_brightness(color);
  for (i = 0; i < sizeof(g_color_buf); i += 2U) {
    g_color_buf[i] = (uint8_t)(color >> 8);
    g_color_buf[i + 1U] = (uint8_t)(color & 0xFF);
  }

  lcd_cs_low();
  lcd_dc_data();
  while (count > 0U) {
    uint32_t chunk_pixels = count;
    if (chunk_pixels > (sizeof(g_color_buf) / 2U)) {
      chunk_pixels = sizeof(g_color_buf) / 2U;
    }
    board_spi_write(g_color_buf, chunk_pixels * 2U);
    count -= chunk_pixels;
  }
  lcd_cs_high();
}

static void lcd_write_data_block(const uint8_t *buf, uint32_t len)
{
  lcd_cs_low();
  lcd_dc_data();
  board_spi_write(buf, len);
  lcd_cs_high();
}

void st7735_set_offsets(uint16_t x_offset, uint16_t y_offset){ g_x_offset = x_offset; g_y_offset = y_offset; }

static uint16_t apply_brightness(uint16_t color)
{
  uint8_t r;
  uint8_t g;
  uint8_t b;

  if (color == COLOR_BLACK || g_brightness_percent >= 100U) {
    return color;
  }
  r = (uint8_t)((color >> 11) & 0x1FU);
  g = (uint8_t)((color >> 5) & 0x3FU);
  b = (uint8_t)(color & 0x1FU);
  r = (uint8_t)(((uint16_t)r * g_brightness_percent) / 100U);
  g = (uint8_t)(((uint16_t)g * g_brightness_percent) / 100U);
  b = (uint8_t)(((uint16_t)b * g_brightness_percent) / 100U);
  return (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | b);
}

void st7735_set_brightness(uint8_t percent)
{
  if (percent < 20U) percent = 20U;
  if (percent > 100U) percent = 100U;
  g_brightness_percent = percent;
}

void st7735_set_rotation(uint8_t mode)
{
  g_madctl = (mode & 0x02U) ? 0xA0 : 0x70;
  g_x_offset = 0;
  g_y_offset = 34;
  g_width = SCREEN_WIDTH;
  g_height = SCREEN_HEIGHT;
  lcd_write_command(0x36);
  lcd_write_data8(g_madctl);
}

static void st7735_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  uint8_t data[4];
  uint16_t x0 = (uint16_t)(x + g_x_offset);
  uint16_t x1 = (uint16_t)(x0 + w - 1U);
  uint16_t y0 = (uint16_t)(y + g_y_offset);
  uint16_t y1 = (uint16_t)(y0 + h - 1U);

  lcd_write_command(0x2A);
  data[0] = (uint8_t)(x0 >> 8); data[1] = (uint8_t)x0; data[2] = (uint8_t)(x1 >> 8); data[3] = (uint8_t)x1;
  lcd_write_data_block(data, 4);

  lcd_write_command(0x2B);
  data[0] = (uint8_t)(y0 >> 8); data[1] = (uint8_t)y0; data[2] = (uint8_t)(y1 >> 8); data[3] = (uint8_t)y1;
  lcd_write_data_block(data, 4);

  lcd_write_command(0x2C);
}

void st7735_display_enable(bool on)
{
  if (on) {
    lcd_write_command(0x11);
    delay_ms(120);
    lcd_write_command(0x29);
    delay_ms(20);
    backlight_set(true);
  } else {
    backlight_set(false);
    lcd_write_command(0x28);
    delay_ms(20);
    lcd_write_command(0x10);
    delay_ms(120);
  }
}

void st7735_init(void)
{
  static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  static const uint8_t pwr_d0[] = {0xA4, 0xA1};
  static const uint8_t gamma_p[] = {
    0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29,
    0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30
  };
  static const uint8_t gamma_n[] = {
    0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28,
    0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32
  };

  board_lcd_reset_set(true);
  delay_ms(20);
  board_lcd_reset_set(false);
  delay_ms(120);
  board_lcd_reset_set(true);
  delay_ms(120);

  lcd_write_command(0x11);
  delay_ms(120);

  st7735_set_rotation(1);

  lcd_write_command(0x3A); lcd_write_data8(0x55);
  lcd_write_command(0xB2); lcd_write_data_block(porch, sizeof(porch));
  lcd_write_command(0xB7); lcd_write_data8(0x35);
  lcd_write_command(0xBB); lcd_write_data8(0x35);
  lcd_write_command(0xC0); lcd_write_data8(0x2C);
  lcd_write_command(0xC2); lcd_write_data8(0x01);
  lcd_write_command(0xC3); lcd_write_data8(0x13);
  lcd_write_command(0xC4); lcd_write_data8(0x20);
  lcd_write_command(0xC6); lcd_write_data8(0x0F);
  lcd_write_command(0xD0); lcd_write_data_block(pwr_d0, sizeof(pwr_d0));
  lcd_write_command(0xD6); lcd_write_data8(0xA1);
  lcd_write_command(0xE0); lcd_write_data_block(gamma_p, sizeof(gamma_p));
  lcd_write_command(0xE1); lcd_write_data_block(gamma_n, sizeof(gamma_n));

  lcd_write_command(0x21);
  lcd_write_command(0x29);
  delay_ms(20);
  st7735_fill_screen(COLOR_BLACK);
}

void st7735_reinit(void)
{
  st7735_init();
  backlight_set(true);
}

void st7735_fill_screen(uint16_t color){ st7735_fill_rect(0, 0, g_width, g_height, color); }

void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if ((x >= g_width) || (y >= g_height) || (w == 0U) || (h == 0U)) return;
  if ((x + w) > g_width) w = (uint16_t)(g_width - x);
  if ((y + h) > g_height) h = (uint16_t)(g_height - y);
  st7735_set_window(x, y, w, h);
  lcd_write_color_repeat(color, (uint32_t)w * h);
}

void st7735_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color){ st7735_fill_rect(x, y, w, 1, color); }
void st7735_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color){ st7735_fill_rect(x, y, 1, h, color); }
void st7735_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color){ if ((w == 0U) || (h == 0U)) return; st7735_draw_hline(x, y, w, color); st7735_draw_hline(x, (uint16_t)(y + h - 1U), w, color); st7735_draw_vline(x, y, h, color); st7735_draw_vline((uint16_t)(x + w - 1U), y, h, color); }

void st7735_draw_char8x16(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
  uint8_t row;
  uint8_t col;
  const uint8_t *glyph;

  if (c < 32 || c > 126) c = '?';
  if ((x >= g_width) || (y >= g_height)) return;
  if ((x + 8U) > g_width) return;
  if ((y + 16U) > g_height) return;

  fg = apply_brightness(fg);
  bg = apply_brightness(bg);
  glyph = font8x16_ref[(uint8_t)c - 32U];

  st7735_set_window(x, y, 8, 16);
  lcd_cs_low();
  lcd_dc_data();
  for (row = 0; row < 16U; ++row) {
    uint8_t line = glyph[row];
    for (col = 0; col < 8U; ++col) {
      uint16_t color = (line & (uint8_t)(1U << col)) ? fg : bg;
      lcd_write8((uint8_t)(color >> 8));
      lcd_write8((uint8_t)(color & 0xFF));
    }
  }
  lcd_cs_high();
}

void st7735_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
  uint8_t col;
  uint8_t row;
  uint8_t sx;
  uint8_t sy;
  uint8_t bits[5];
  uint16_t w;
  uint16_t h;

  if (c < 32 || c > 126) c = '?';
  if (scale == 0U) scale = 1U;

  w = (uint16_t)(6U * scale);
  h = (uint16_t)(8U * scale);
  if ((x >= g_width) || (y >= g_height)) return;
  if ((x + w) > g_width) return;
  if ((y + h) > g_height) return;

  fg = apply_brightness(fg);
  bg = apply_brightness(bg);
  for (col = 0; col < 5; ++col) {
    bits[col] = font5x7[(int)c - 32][col];
  }

  st7735_set_window(x, y, w, h);
  lcd_cs_low();
  lcd_dc_data();
  for (row = 0; row < 8; ++row) {
    for (sy = 0; sy < scale; ++sy) {
      for (col = 0; col < 5; ++col) {
        uint16_t color = (bits[col] & (uint8_t)(1U << row)) ? fg : bg;
        for (sx = 0; sx < scale; ++sx) {
          lcd_write8((uint8_t)(color >> 8));
          lcd_write8((uint8_t)(color & 0xFF));
        }
      }
      for (sx = 0; sx < scale; ++sx) {
        lcd_write8((uint8_t)(bg >> 8));
        lcd_write8((uint8_t)(bg & 0xFF));
      }
    }
  }
  lcd_cs_high();
}
void st7735_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale){ while (*text != '\0') { st7735_draw_char(x, y, *text, fg, bg, scale); x = (uint16_t)(x + (uint16_t)(6U * scale)); ++text; } }

void st7735_draw_glyph16(uint16_t x, uint16_t y, const uint8_t glyph[32], uint16_t fg, uint16_t bg)
{
  uint8_t row;
  uint8_t bit;
  uint8_t lo;
  uint8_t hi;

  if ((x >= g_width) || (y >= g_height)) return;
  if ((x + 16U) > g_width) return;
  if ((y + 16U) > g_height) return;

  fg = apply_brightness(fg);
  bg = apply_brightness(bg);
  st7735_set_window(x, y, 16, 16);
  lcd_cs_low();
  lcd_dc_data();
  for (row = 0; row < 16U; ++row) {
    lo = glyph[(uint8_t)(row * 2U)];
    hi = glyph[(uint8_t)(row * 2U + 1U)];
    for (bit = 0; bit < 8U; ++bit) {
      uint16_t color = (lo & (uint8_t)(1U << bit)) ? fg : bg;
      lcd_write8((uint8_t)(color >> 8));
      lcd_write8((uint8_t)(color & 0xFF));
    }
    for (bit = 0; bit < 8U; ++bit) {
      uint16_t color = (hi & (uint8_t)(1U << bit)) ? fg : bg;
      lcd_write8((uint8_t)(color >> 8));
      lcd_write8((uint8_t)(color & 0xFF));
    }
  }
  lcd_cs_high();
}
