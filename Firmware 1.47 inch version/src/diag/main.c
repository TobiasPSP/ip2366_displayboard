#include "board.h"
#include "st7735.h"

int main(void)
{
  board_clock_init();
  board_systick_init();
  board_gpio_init();
  backlight_set(true);
  fan_set(true);
  board_spi_init();

  delay_ms(200);
  st7735_init();
  st7735_set_rotation(1);
  st7735_set_offsets(0, 34);

  while (1) {
    backlight_set(true);
    fan_set(true);
    st7735_fill_screen(COLOR_RED);
    delay_ms(500);
    st7735_fill_screen(COLOR_GREEN);
    delay_ms(500);
    st7735_fill_screen(COLOR_BLUE);
    delay_ms(500);
    st7735_fill_screen(COLOR_WHITE);
    delay_ms(500);
    st7735_fill_screen(COLOR_BLACK);
    st7735_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_WHITE);
    st7735_draw_text(10, 30, "ST7789 1.47", COLOR_GREEN, COLOR_BLACK, 2);
    st7735_draw_text(10, 58, "320x172", COLOR_WHITE, COLOR_BLACK, 2);
    delay_ms(800);
  }
}
