#include "board.h"

static volatile uint32_t g_ms_ticks = 0;
static volatile key_event_t g_key_event = KEY_EVENT_NONE;
static volatile bool g_key_wakeup = false;
static volatile bool g_sw6236_wakeup = false;

static ADC_HandleTypeDef hadc1;

#define I2C_GPIO_PORT GPIOB
#define I2C_SCL_PIN   GPIO_PIN_6
#define I2C_SDA_PIN   GPIO_PIN_7

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t stable;
  uint8_t debounce;
  uint16_t hold_ticks;
  bool long_sent;
} key_state_t;

static key_state_t g_keys[3] = {
  {GPIOB, GPIO_PIN_0, 1, 0, 0, false},
  {GPIOB, GPIO_PIN_1, 1, 0, 0, false},
  {GPIOB, GPIO_PIN_2, 1, 0, 0, false},
};

static void short_delay_cycles(uint32_t n)
{
  volatile uint32_t i;
  for (i = 0; i < n; ++i) {
    __NOP();
  }
}

static void i2c_gpio_delay(void)
{
  short_delay_cycles(80U);
}

static void i2c_data_ready_delay(void)
{
  short_delay_cycles(1200U);
}

static void i2c_scl_set(bool high)
{
  if (high) {
    I2C_GPIO_PORT->BSRR = I2C_SCL_PIN;
  } else {
    I2C_GPIO_PORT->BRR = I2C_SCL_PIN;
  }
}

static void i2c_sda_set(bool high)
{
  if (high) {
    I2C_GPIO_PORT->BSRR = I2C_SDA_PIN;
  } else {
    I2C_GPIO_PORT->BRR = I2C_SDA_PIN;
  }
}

static bool i2c_sda_is_high(void)
{
  return HAL_GPIO_ReadPin(I2C_GPIO_PORT, I2C_SDA_PIN) == GPIO_PIN_SET;
}

static void i2c_gpio_mode(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);
  i2c_scl_set(true);
  i2c_sda_set(true);
  i2c_gpio_delay();
}

static void i2c_start(void)
{
  i2c_sda_set(true);
  i2c_scl_set(true);
  i2c_gpio_delay();
  i2c_sda_set(false);
  i2c_gpio_delay();
  i2c_scl_set(false);
  i2c_gpio_delay();
}

static void i2c_stop(void)
{
  i2c_sda_set(false);
  i2c_gpio_delay();
  i2c_scl_set(true);
  i2c_gpio_delay();
  i2c_sda_set(true);
  i2c_gpio_delay();
}

static bool i2c_write_byte(uint8_t byte)
{
  uint8_t i;
  bool ack;

  for (i = 0U; i < 8U; ++i) {
    i2c_sda_set((byte & 0x80U) != 0U);
    i2c_gpio_delay();
    i2c_scl_set(true);
    i2c_gpio_delay();
    i2c_scl_set(false);
    byte <<= 1;
  }

  i2c_sda_set(true);
  i2c_gpio_delay();
  i2c_scl_set(true);
  i2c_gpio_delay();
  ack = !i2c_sda_is_high();
  i2c_scl_set(false);
  i2c_gpio_delay();
  return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
  uint8_t i;
  uint8_t byte = 0U;

  i2c_sda_set(true);
  for (i = 0U; i < 8U; ++i) {
    byte <<= 1;
    i2c_gpio_delay();
    i2c_scl_set(true);
    i2c_gpio_delay();
    if (i2c_sda_is_high()) {
      byte |= 1U;
    }
    i2c_scl_set(false);
  }

  i2c_sda_set(!ack);
  i2c_gpio_delay();
  i2c_scl_set(true);
  i2c_gpio_delay();
  i2c_scl_set(false);
  i2c_sda_set(true);
  i2c_gpio_delay();
  return byte;
}

uint32_t HAL_GetTick(void)
{
  return g_ms_ticks;
}

void SysTick_Handler(void)
{
  g_ms_ticks++;
}

uint32_t millis(void)
{
  return g_ms_ticks;
}

void EXTI4_15_IRQHandler(void)
{
  uint32_t pending = EXTI->PR & GPIO_PIN_8;

  if (pending != 0U) {
    EXTI->PR = pending;
    g_sw6236_wakeup = true;
  }
}

void EXTI0_1_IRQHandler(void)
{
  uint32_t pending = EXTI->PR & (GPIO_PIN_0 | GPIO_PIN_1);

  if (pending != 0U) {
    EXTI->PR = pending;
    g_key_wakeup = true;
  }
}

void EXTI2_3_IRQHandler(void)
{
  uint32_t pending = EXTI->PR & GPIO_PIN_2;

  if (pending != 0U) {
    EXTI->PR = pending;
    g_key_wakeup = true;
  }
}

void delay_ms(uint32_t ms)
{
  uint32_t start = millis();
  while ((millis() - start) < ms) {
  }
}

void board_clock_init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  HAL_Init();

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
  osc.HSIDiv = RCC_HSI_DIV1;
  osc.HSEState = RCC_HSE_OFF;
  osc.LSIState = RCC_LSI_OFF;
#if defined(RCC_LSE_SUPPORT)
  osc.LSEState = RCC_LSE_OFF;
#endif
#if defined(RCC_PLL_SUPPORT)
  osc.PLL.PLLState = RCC_PLL_OFF;
#endif
  (void)HAL_RCC_OscConfig(&osc);

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
  SystemCoreClockUpdate();
}

void board_systick_init(void)
{
  SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
  SysTick->VAL = 0U;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void gpio_output(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState initial)
{
  GPIO_InitTypeDef gpio = {0};

  HAL_GPIO_WritePin(port, pins, initial);
  gpio.Pin = pins;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(port, &gpio);
}

void board_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  gpio_output(GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2 | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_8, GPIO_PIN_RESET);

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);
}

void board_spi_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_SPI1_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF0_SPI1;
  HAL_GPIO_Init(GPIOA, &gpio);

  SPI1->CR1 = 0U;
  SPI1->CR2 = SPI_CR2_FRXTH;
  SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_BR_0;
  SPI1->CR1 |= SPI_CR1_SPE;
}

static void i2c_bus_recover_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint8_t i;

  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
  short_delay_cycles(400U);

  for (i = 0U; i < 16U; ++i) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
    short_delay_cycles(300U);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    short_delay_cycles(300U);
  }

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
  short_delay_cycles(300U);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
  short_delay_cycles(300U);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
  short_delay_cycles(300U);
}

void board_i2c_init(void)
{
  __HAL_RCC_I2C_CLK_ENABLE();

  i2c_bus_recover_gpio();
  i2c_gpio_mode();
}

void board_adc_init(void)
{
  ADC_ChannelConfTypeDef channel = {0};

  __HAL_RCC_ADC_CLK_ENABLE();

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV8;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.SamplingTimeCommon = ADC_SAMPLETIME_239CYCLES_5;
  (void)HAL_ADC_Init(&hadc1);

  channel.Channel = ADC_CHANNEL_6;
  channel.Rank = ADC_RANK_CHANNEL_NUMBER;
  channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  (void)HAL_ADC_ConfigChannel(&hadc1, &channel);
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
}

uint16_t adc_read_pa0(void)
{
  uint16_t value = 0U;

  if (HAL_ADC_Start(&hadc1) == HAL_OK) {
    if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK) {
      value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    (void)HAL_ADC_Stop(&hadc1);
  }
  return value;
}

void backlight_set(bool on)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void fan_set(bool on)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void torch_led_set(bool on)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool sw6236_irq_is_high(void)
{
  return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_SET;
}

void sw6236_irq_drive(bool high)
{
  GPIO_InitTypeDef gpio = {0};

  if (high) {
    sw6236_irq_release();
    return;
  }

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
}

void sw6236_irq_release(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
}

void board_lcd_cs_set(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_9;
  } else {
    GPIOA->BRR = GPIO_PIN_9;
  }
}

void board_lcd_dc_set(bool data)
{
  if (data) {
    GPIOA->BSRR = GPIO_PIN_1;
  } else {
    GPIOA->BRR = GPIO_PIN_1;
  }
}

void board_lcd_reset_set(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_2;
  } else {
    GPIOA->BRR = GPIO_PIN_2;
  }
}

void board_spi_write8(uint8_t data)
{
  volatile uint8_t *dr8 = (volatile uint8_t *)&SPI1->DR;

  *dr8 = data;
  while ((SPI1->SR & SPI_SR_TXE) == 0U) {
  }
  while ((SPI1->SR & SPI_SR_BSY) != 0U) {
  }
  (void)*dr8;
  (void)SPI1->SR;
}

void board_spi_write(const uint8_t *data, uint32_t len)
{
  volatile uint8_t *dr8 = (volatile uint8_t *)&SPI1->DR;

  while (len-- > 0U) {
    while ((SPI1->SR & SPI_SR_TXE) == 0U) {
    }
    *dr8 = *data++;
  }
  while ((SPI1->SR & SPI_SR_TXE) == 0U) {
  }
  while ((SPI1->SR & SPI_SR_BSY) != 0U) {
  }
  (void)*dr8;
  (void)SPI1->SR;
}

static void flash_spi_delay(void)
{
  short_delay_cycles(12U);
}

static void flash_cs_set(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_12;
  } else {
    GPIOA->BRR = GPIO_PIN_12;
  }
}

static void flash_sck_set(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_0;
  } else {
    GPIOA->BRR = GPIO_PIN_0;
  }
}

static void flash_mosi_set(bool high)
{
  if (high) {
    GPIOA->BSRR = GPIO_PIN_4;
  } else {
    GPIOA->BRR = GPIO_PIN_4;
  }
}

static bool flash_miso_is_high(void)
{
  return HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_3) == GPIO_PIN_SET;
}

static uint8_t flash_spi_transfer(uint8_t tx)
{
  uint8_t i;
  uint8_t rx = 0U;

  for (i = 0U; i < 8U; ++i) {
    flash_mosi_set((tx & 0x80U) != 0U);
    tx <<= 1;
    flash_spi_delay();
    flash_sck_set(true);
    flash_spi_delay();
    rx <<= 1;
    if (flash_miso_is_high()) {
      rx |= 1U;
    }
    flash_sck_set(false);
    flash_spi_delay();
  }
  return rx;
}

bool board_ext_flash_read_id(uint8_t id[3])
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  flash_cs_set(true);
  flash_sck_set(false);
  flash_mosi_set(false);

  gpio.Pin = GPIO_PIN_12 | GPIO_PIN_0 | GPIO_PIN_4;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOF, &gpio);

  flash_cs_set(false);
  (void)flash_spi_transfer(0x9FU);
  id[0] = flash_spi_transfer(0xFFU);
  id[1] = flash_spi_transfer(0xFFU);
  id[2] = flash_spi_transfer(0xFFU);
  flash_cs_set(true);

  flash_sck_set(false);
  flash_mosi_set(false);
  return !((id[0] == 0x00U && id[1] == 0x00U && id[2] == 0x00U) ||
           (id[0] == 0xFFU && id[1] == 0xFFU && id[2] == 0xFFU));
}

void keys_init(void)
{
  int i;

  g_key_event = KEY_EVENT_NONE;
  for (i = 0; i < 3; ++i) {
    uint8_t raw = (HAL_GPIO_ReadPin(g_keys[i].port, g_keys[i].pin) == GPIO_PIN_SET) ? 1U : 0U;
    g_keys[i].stable = raw;
    g_keys[i].debounce = 0U;
    g_keys[i].hold_ticks = 0U;
    g_keys[i].long_sent = (raw == 0U);
  }
}

static void queue_key_event(key_event_t evt)
{
  if (g_key_event == KEY_EVENT_NONE) {
    g_key_event = evt;
  }
}

void keys_poll(void)
{
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  int i;

  if ((now - last_tick) < 2U) {
    return;
  }
  last_tick = now;

  for (i = 0; i < 3; ++i) {
    key_state_t *key = &g_keys[i];
    uint8_t raw = (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_SET) ? 1U : 0U;

    if (raw == key->stable) {
      key->debounce = 0U;
    } else {
      key->debounce++;
      if (key->debounce >= 2U) {
        key->debounce = 0U;
        key->stable = raw;
        if (raw == 0U) {
          if (i == KEY_ID_S1) {
            queue_key_event(KEY_EVENT_S1_CLICK);
          } else if (i == KEY_ID_S3) {
            queue_key_event(KEY_EVENT_S3_CLICK);
          }
          key->hold_ticks = 0U;
          key->long_sent = false;
        } else {
          if ((i == KEY_ID_S2) && !key->long_sent) {
            queue_key_event(KEY_EVENT_S2_CLICK);
          }
        }
      }
    }

    if (key->stable == 0U) {
      key->hold_ticks++;
      if ((i == KEY_ID_S2) && !key->long_sent && (key->hold_ticks >= 350U)) {
        key->long_sent = true;
        queue_key_event(KEY_EVENT_S2_LONG);
      }
    }
  }
}

key_event_t key_get_event(void)
{
  key_event_t evt = g_key_event;
  g_key_event = KEY_EVENT_NONE;
  return evt;
}

bool key_is_pressed(key_id_t key)
{
  return g_keys[(int)key].stable == 0U;
}

bool keys_any_raw_pressed(void)
{
  return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_RESET ||
         HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET ||
         HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2) == GPIO_PIN_RESET;
}

static void keys_configure_wakeup_exti(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  EXTI->PR = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;
  NVIC_ClearPendingIRQ(EXTI0_1_IRQn);
  NVIC_ClearPendingIRQ(EXTI2_3_IRQn);
  NVIC_EnableIRQ(EXTI0_1_IRQn);
  NVIC_EnableIRQ(EXTI2_3_IRQn);
}

static void keys_restore_input_mode(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t pins = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2;

  NVIC_DisableIRQ(EXTI0_1_IRQn);
  NVIC_DisableIRQ(EXTI2_3_IRQn);
  EXTI->IMR &= ~pins;
  EXTI->EMR &= ~pins;
  EXTI->RTSR &= ~pins;
  EXTI->FTSR &= ~pins;
  EXTI->PR = pins;

  gpio.Pin = (uint16_t)pins;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
}

static void sw6236_configure_wakeup_exti(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  EXTI->PR = GPIO_PIN_8;
  NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
  NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void sw6236_restore_input_mode(void)
{
  NVIC_DisableIRQ(EXTI4_15_IRQn);
  EXTI->IMR &= ~GPIO_PIN_8;
  EXTI->EMR &= ~GPIO_PIN_8;
  EXTI->RTSR &= ~GPIO_PIN_8;
  EXTI->FTSR &= ~GPIO_PIN_8;
  EXTI->PR = GPIO_PIN_8;
  sw6236_irq_release();
}

void board_low_power_wait_for_key(void)
{
  uint32_t systick_ctrl;

  if (keys_any_raw_pressed()) {
    return;
  }

  g_key_wakeup = false;
  keys_configure_wakeup_exti();

  systick_ctrl = SysTick->CTRL;
  SysTick->CTRL = systick_ctrl & ~(SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
#if defined(PWR_CR1_LPR) && defined(SCB_SCR_SLEEPDEEP_Msk)
  __HAL_RCC_PWR_CLK_ENABLE();
  SET_BIT(PWR->CR1, PWR_CR1_LPR);
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
#endif
  __DSB();
  while (!g_key_wakeup && !keys_any_raw_pressed()) {
    __WFI();
  }
  __ISB();
#if defined(PWR_CR1_LPR) && defined(SCB_SCR_SLEEPDEEP_Msk)
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  CLEAR_BIT(PWR->CR1, PWR_CR1_LPR);
#endif
  SysTick->CTRL = systick_ctrl;

  keys_restore_input_mode();
  keys_init();
}

bool board_low_power_wait_for_key_or_sw6236(void)
{
  uint32_t systick_ctrl;
  bool key_wakeup;

  if (keys_any_raw_pressed()) {
    return true;
  }
  if (!sw6236_irq_is_high()) {
    return false;
  }

  g_key_wakeup = false;
  g_sw6236_wakeup = false;
  keys_configure_wakeup_exti();
  sw6236_configure_wakeup_exti();

  systick_ctrl = SysTick->CTRL;
  SysTick->CTRL = systick_ctrl & ~(SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk);
#if defined(PWR_CR1_LPR) && defined(SCB_SCR_SLEEPDEEP_Msk)
  __HAL_RCC_PWR_CLK_ENABLE();
  SET_BIT(PWR->CR1, PWR_CR1_LPR);
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
#endif
  __DSB();
  while (!g_key_wakeup && !keys_any_raw_pressed() && !g_sw6236_wakeup && sw6236_irq_is_high()) {
    __WFI();
  }
  __ISB();
#if defined(PWR_CR1_LPR) && defined(SCB_SCR_SLEEPDEEP_Msk)
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
  CLEAR_BIT(PWR->CR1, PWR_CR1_LPR);
#endif
  SysTick->CTRL = systick_ctrl;

  key_wakeup = g_key_wakeup || keys_any_raw_pressed();
  keys_restore_input_mode();
  keys_init();
  sw6236_restore_input_mode();
  return key_wakeup;
}

bool board_low_power_wait_for_key_or_sw6236_timeout(uint32_t timeout_ms)
{
  uint32_t start;
  bool key_wakeup;

  if (keys_any_raw_pressed()) {
    return true;
  }
  if (!sw6236_irq_is_high()) {
    return false;
  }

  g_key_wakeup = false;
  g_sw6236_wakeup = false;
  keys_configure_wakeup_exti();
  sw6236_configure_wakeup_exti();

  start = millis();
  while (!g_key_wakeup && !keys_any_raw_pressed() && !g_sw6236_wakeup && sw6236_irq_is_high() &&
         ((millis() - start) < timeout_ms)) {
    __WFI();
  }

  key_wakeup = g_key_wakeup || keys_any_raw_pressed();
  keys_restore_input_mode();
  keys_init();
  sw6236_restore_input_mode();
  return key_wakeup;
}

bool sw_i2c_write(uint8_t dev_addr, const uint8_t *data, uint16_t len)
{
  uint16_t i;
  bool ok;

  i2c_start();
  ok = i2c_write_byte((uint8_t)(dev_addr << 1));
  for (i = 0U; ok && (i < len); ++i) {
    ok = i2c_write_byte(data[i]);
  }
  i2c_stop();
  return ok;
}

bool sw_i2c_read(uint8_t dev_addr, uint8_t *data, uint16_t len)
{
  uint16_t i;
  bool ok;

  i2c_start();
  ok = i2c_write_byte((uint8_t)((dev_addr << 1) | 0x01U));
  if (ok) {
    i2c_data_ready_delay();
  }
  for (i = 0U; ok && (i < len); ++i) {
    data[i] = i2c_read_byte((i + 1U) < len);
    if ((i + 1U) < len) {
      delay_ms(1);
    }
  }
  i2c_stop();
  return ok;
}

bool sw_i2c_write_then_read(uint8_t dev_addr, const uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t rx_len)
{
  uint16_t i;
  bool ok;

  if (rx_len == 0U) {
    return sw_i2c_write(dev_addr, tx_data, tx_len);
  }

  i2c_start();
  ok = i2c_write_byte((uint8_t)(dev_addr << 1));
  for (i = 0U; ok && (i < tx_len); ++i) {
    ok = i2c_write_byte(tx_data[i]);
  }
  if (ok) {
    i2c_start();
    ok = i2c_write_byte((uint8_t)((dev_addr << 1) | 0x01U));
  }
  if (ok) {
    i2c_data_ready_delay();
  }
  for (i = 0U; ok && (i < rx_len); ++i) {
    rx_data[i] = i2c_read_byte((i + 1U) < rx_len);
    if ((i + 1U) < rx_len) {
      delay_ms(1);
    }
  }
  i2c_stop();
  return ok;
}

bool sw_i2c_read_reg1(uint8_t dev_addr, uint8_t reg, uint8_t *data1)
{
  return sw_i2c_write_then_read(dev_addr, &reg, 1U, data1, 1U);
}

bool sw_i2c_write_reg1(uint8_t dev_addr, uint8_t reg, uint8_t value)
{
  uint8_t data[2] = {reg, value};
  return sw_i2c_write(dev_addr, data, 2U);
}

bool sw_i2c_read_reg3(uint8_t dev_addr, uint8_t reg, uint8_t data3[3])
{
  return sw_i2c_write_then_read(dev_addr, &reg, 1U, data3, 3U);
}

void board_low_power_prepare(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t systick_ctrl = SysTick->CTRL;

  gpio.Pin = GPIO_PIN_8;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  EXTI->PR = GPIO_PIN_8;
  NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
  NVIC_EnableIRQ(EXTI4_15_IRQn);

  SysTick->CTRL = systick_ctrl & ~SysTick_CTRL_TICKINT_Msk;
  __DSB();
  while (sw6236_irq_is_high()) {
    __WFI();
  }
  __ISB();
  SysTick->CTRL = systick_ctrl;

  NVIC_DisableIRQ(EXTI4_15_IRQn);
  EXTI->PR = GPIO_PIN_8;
  sw6236_irq_release();
}
