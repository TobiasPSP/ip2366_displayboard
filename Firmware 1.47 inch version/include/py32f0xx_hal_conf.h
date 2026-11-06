#ifndef PY32F0XX_HAL_CONF_H
#define PY32F0XX_HAL_CONF_H

#include "py32f030x8.h"

#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED

#define HSE_VALUE        8000000U
#define HSI_VALUE        24000000U
#define LSI_VALUE        32000U
#define LSE_VALUE        32768U
#define HSE_STARTUP_TIMEOUT 100U
#define LSE_STARTUP_TIMEOUT 5000U
#define VDD_VALUE        3300U
#define TICK_INT_PRIORITY 0U
#define USE_RTOS         0U
#define PREFETCH_ENABLE  0U

#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#include "py32f0xx_hal_rcc.h"
#include "py32f0xx_hal_gpio.h"
#include "py32f0xx_hal_dma.h"
#include "py32f0xx_hal_spi.h"
#include "py32f0xx_hal_i2c.h"
#include "py32f0xx_hal_adc.h"
#include "py32f0xx_hal_cortex.h"
#include "py32f0xx_hal_flash.h"

#endif
