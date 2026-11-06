#ifndef AT32F421_CONF_H
#define AT32F421_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(HEXT_VALUE)
#define HEXT_VALUE ((uint32_t)8000000)
#endif

#define HEXT_STARTUP_TIMEOUT ((uint16_t)0x3000)
#define HICK_VALUE ((uint32_t)8000000)
#define LEXT_VALUE ((uint32_t)32768)

#define CRM_MODULE_ENABLED
#define TMR_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define I2C_MODULE_ENABLED
#define USART_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define ADC_MODULE_ENABLED
#define SPI_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define DEBUG_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define EXINT_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define SCFG_MODULE_ENABLED

#include "at32f421.h"
#include "at32f421_crm.h"
#include "at32f421_tmr.h"
#include "at32f421_gpio.h"
#include "at32f421_i2c.h"
#include "at32f421_usart.h"
#include "at32f421_pwc.h"
#include "at32f421_adc.h"
#include "at32f421_spi.h"
#include "at32f421_dma.h"
#include "at32f421_debug.h"
#include "at32f421_flash.h"
#include "at32f421_exint.h"
#include "at32f421_misc.h"
#include "at32f421_scfg.h"

#ifdef __cplusplus
}
#endif

#endif
