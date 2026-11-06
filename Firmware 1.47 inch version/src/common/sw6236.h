#ifndef SW6236_H
#define SW6236_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  SW6236_ADC_VBUS = 0,
  SW6236_ADC_IBUS = 1,
  SW6236_ADC_VBAT = 2,
  SW6236_ADC_CAPACITY = 6,
  SW6236_ADC_CHIP_TEMP = 9,
  SW6236_ADC_NTC_VOLT = 10
} sw6236_adc_sel_t;

typedef struct {
  bool online;
  bool powered;
  uint8_t chip_version;
  uint8_t fast_charge;
  uint8_t mode_status;
  uint8_t system_status;
  uint8_t port_online;
  uint8_t typec_status;
  uint8_t event_status;
  uint8_t discharge_error;
  uint8_t charge_error;
  uint8_t percent;
  uint8_t adc_reg40;
  uint8_t adc_reg41;
  uint8_t adc_reg42;
  uint8_t b0_value;
  bool b0_ok;
  uint16_t vbat_raw;
  uint16_t ntc_raw;
  uint16_t ntc_mv;
  uint16_t vbus_mv;
  uint16_t vbat_mv;
  uint16_t ibus_ma;
  uint16_t ibat_ma;
  uint32_t capacity_mah;
  uint32_t set_capacity_mah;
  uint16_t set_battery_mv;
  int16_t chip_temp_c10;
  int16_t ntc_temp_c10;
  bool suspect_damage;
  uint32_t power_mw;
} sw6236_metrics_t;

void sw6236_defaults(sw6236_metrics_t *m);
bool sw6236_read_reg(uint8_t reg, uint8_t *value);
bool sw6236_write_reg(uint8_t reg, uint8_t value);
bool sw6236_update_bits(uint8_t reg, uint8_t mask, uint8_t value);
bool sw6236_write_enable(void);
bool sw6236_write_finish(void);
bool sw6236_read_adc(sw6236_adc_sel_t sel, uint16_t *raw);
bool sw6236_read_metrics(sw6236_metrics_t *m);
bool sw6236_read_capacity_snapshot(uint32_t *max_capacity_mah, uint32_t *current_capacity_mah, bool *learned);
bool sw6236_set_output_voltage_mv(uint16_t mv);
bool sw6236_set_max_capacity_mah(uint32_t mah);
uint32_t sw6236_capacity_limit_mah(void);
bool sw6236_read_battery_type(uint8_t *type);
bool sw6236_set_battery_type(uint8_t type);
const char *sw6236_battery_type_name(uint8_t type);
bool sw6236_set_low_current_mode(bool enable);
bool sw6236_read_mode_status(uint8_t *mode_status);
bool sw6236_read_low_current_active(bool *active);
const char *sw6236_fast_charge_name(uint8_t code);
const char *sw6236_port_summary(uint8_t port_online);

#endif
