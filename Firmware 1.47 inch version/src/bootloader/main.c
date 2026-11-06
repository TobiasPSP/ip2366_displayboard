#include <stdint.h>

#define APP_BASE        0x08000800UL
#define APP_END         0x08010000UL
#define SRAM_BASE_ADDR  0x20000000UL
#define SRAM_END_ADDR   0x20002000UL

#define SCB_VTOR        (*(volatile uint32_t *)0xE000ED08UL)
#define SYST_CSR        (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR        (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR        (*(volatile uint32_t *)0xE000E018UL)

typedef void (*entry_t)(void);

static int app_is_valid(void)
{
  uint32_t stack = *(volatile uint32_t *)APP_BASE;
  uint32_t reset = *(volatile uint32_t *)(APP_BASE + 4UL);

  return (stack >= SRAM_BASE_ADDR) &&
         (stack <= SRAM_END_ADDR) &&
         (reset >= APP_BASE) &&
         (reset < APP_END);
}

static void jump_to_app(void)
{
  uint32_t stack = *(volatile uint32_t *)APP_BASE;
  uint32_t reset = *(volatile uint32_t *)(APP_BASE + 4UL);

  __asm volatile ("cpsid i");
  SYST_CSR = 0;
  SYST_RVR = 0;
  SYST_CVR = 0;
  SCB_VTOR = APP_BASE;
  __asm volatile ("dsb");
  __asm volatile ("isb");
  __asm volatile ("msr msp, %0" : : "r" (stack) : );
  ((entry_t)reset)();
}

int main(void)
{
  if (app_is_valid()) {
    jump_to_app();
  }

  while (1) {
  }
}
