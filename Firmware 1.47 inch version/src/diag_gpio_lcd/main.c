#include "board.h"
#include "st7735.h"

static void force_pa3_pa8(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_3 | GPIO_PIN_8;
  } else {
    GPIOA->BRR = GPIO_PIN_3 | GPIO_PIN_8;
  }
}

int main(void)
{
  board_clock_init();
  board_systick_init();
  board_gpio_init();

  force_pa3_pa8(true);
  delay_ms(1000);
  force_pa3_pa8(false);
  delay_ms(1000);
  force_pa3_pa8(true);
  delay_ms(1000);

  board_spi_init();
  st7735_init();
  st7735_set_rotation(1);
  st7735_set_offsets(0, 34);

  while (1) {
    force_pa3_pa8(true);
    st7735_fill_screen(COLOR_RED);
    delay_ms(700);
    st7735_fill_screen(COLOR_GREEN);
    delay_ms(700);
    st7735_fill_screen(COLOR_BLUE);
    delay_ms(700);
    st7735_fill_screen(COLOR_BLACK);
    st7735_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_WHITE);
    st7735_draw_text(10, 28, "GPIO+LCD TEST", COLOR_GREEN, COLOR_BLACK, 2);
    st7735_draw_text(10, 56, "PA3/PA8 HIGH", COLOR_WHITE, COLOR_BLACK, 2);
    delay_ms(1200);

    force_pa3_pa8(false);
    delay_ms(500);
  }
}
