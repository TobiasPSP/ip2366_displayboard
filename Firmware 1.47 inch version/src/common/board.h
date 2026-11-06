#ifndef BOARD_H
#define BOARD_H

#include "py32f0xx_hal.h"
#include "project_config.h"

typedef enum {
  KEY_ID_S1 = 0,
  KEY_ID_S2 = 1,
  KEY_ID_S3 = 2
} key_id_t;

typedef enum {
  KEY_EVENT_NONE = 0,
  KEY_EVENT_S1_CLICK,
  KEY_EVENT_S2_CLICK,
  KEY_EVENT_S2_LONG,
  KEY_EVENT_S3_CLICK
} key_event_t;

void board_clock_init(void);
void board_systick_init(void);
void board_gpio_init(void);
void board_spi_init(void);
void board_i2c_init(void);
void board_adc_init(void);
void board_low_power_prepare(void);
void board_low_power_wait_for_key(void);
bool board_low_power_wait_for_key_or_sw6236(void);
bool board_low_power_wait_for_key_or_sw6236_timeout(uint32_t timeout_ms);

void delay_ms(uint32_t ms);
uint32_t millis(void);

void backlight_set(bool on);
void fan_set(bool on);
void torch_led_set(bool on);
bool sw6236_irq_is_high(void);
void sw6236_irq_drive(bool high);
void sw6236_irq_release(void);

void board_lcd_cs_set(bool high);
void board_lcd_dc_set(bool data);
void board_lcd_reset_set(bool high);
void board_spi_write8(uint8_t data);
void board_spi_write(const uint8_t *data, uint32_t len);
bool board_ext_flash_read_id(uint8_t id[3]);

void keys_init(void);
void keys_poll(void);
key_event_t key_get_event(void);
bool key_is_pressed(key_id_t key);
bool keys_any_raw_pressed(void);

uint16_t adc_read_pa0(void);

bool sw_i2c_write(uint8_t dev_addr, const uint8_t *data, uint16_t len);
bool sw_i2c_read(uint8_t dev_addr, uint8_t *data, uint16_t len);
bool sw_i2c_write_then_read(uint8_t dev_addr, const uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t rx_len);
bool sw_i2c_read_reg1(uint8_t dev_addr, uint8_t reg, uint8_t *data1);
bool sw_i2c_write_reg1(uint8_t dev_addr, uint8_t reg, uint8_t value);
bool sw_i2c_read_reg3(uint8_t dev_addr, uint8_t reg, uint8_t data3[3]);

#endif
