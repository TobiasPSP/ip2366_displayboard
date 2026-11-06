#include "boot.h"
#include "project_config.h"
#include "py32f0xx_hal.h"

typedef void (*app_entry_t)(void);

bool boot_is_app_valid(void)
{
  uint32_t stack = *(volatile uint32_t *)APP_FLASH_BASE;
  uint32_t reset = *(volatile uint32_t *)(APP_FLASH_BASE + 4U);
  bool stack_ok = (stack >= 0x20000000UL) && (stack <= 0x20002000UL);
  bool reset_ok = (reset >= APP_FLASH_BASE) && (reset < APP_FLASH_END);
  return stack_ok && reset_ok;
}

void boot_jump_to_app(void)
{
  uint32_t app_stack = *(volatile uint32_t *)APP_FLASH_BASE;
  uint32_t app_reset = *(volatile uint32_t *)(APP_FLASH_BASE + 4U);

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  SCB->VTOR = APP_FLASH_BASE;
  __DSB();
  __ISB();
  __set_MSP(app_stack);

  ((app_entry_t)app_reset)();

  while (1) {
  }
}
