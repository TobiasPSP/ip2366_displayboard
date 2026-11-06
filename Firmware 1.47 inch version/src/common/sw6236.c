#include "sw6236.h"
#include "board.h"
#include "project_config.h"
#include "thermistor.h"
#include <string.h>

#define REG_SYS_CTL0           0x00U
#define REG_CHIP_VERSION        REG_SYS_CTL0
#define REG_FAST_CHARGE        0x34U
#define REG_MODE_STATUS        0x31U
#define REG_SYSTEM_STATUS      0x33U
#define REG_PORT_ONLINE        0x34U
#define REG_TYPEC_STATUS       0x34U
#define REG_NTC_CURRENT        0x77U
#define REG_WRITE_ENABLE       0x12U
#define REG_EVENT_STATUS       0x19U
#define REG_DISCHARGE_ERROR    0x1AU
#define REG_CHARGE_ERROR       0x1BU
#define REG_LOW_POWER          0x2DU
#define REG_VOUT_LOW           0x32U
#define REG_VOUT_HIGH          0x33U
#define REG_ADC_CONFIG         0x40U
#define REG_ADC_LOW            0x41U
#define REG_ADC_HIGH           0x42U
#define REG_PERCENT_FINAL      0x58U
#define REG_MAX_CAP_LOW        0x5AU
#define REG_MAX_CAP_HIGH       0x5BU
#define REG_PORT_MODE_CONFIG   0xB8U
#define REG_CHARGE_CONFIG1     0xBBU
#define I2C_READ_GATEWAY       0xB0U
#define CAPACITY_CODE_MAX      0x0FFFU
#define CAPACITY_UNIT_CWH      16947UL
#define REG_IP2366_BATV_LO     0x50U
#define REG_IP2366_VSYS_LO     0x52U
#define REG_IP2366_STATE0      0x31U
#define REG_IP2366_STATE2      0x33U
#define REG_IP2366_TYPEC_STATE 0x34U
#define REG_IP2366_FAULT       0x38U
#define REG_IP2366_IBAT_LO     0x6EU
#define REG_IP2366_ISYS_LO     0x70U
#define REG_IP2366_POWER_LO    0x74U
#define REG_IP2366_NTC_CURRENT 0x77U
#define REG_IP2366_NTC_MV_LO   0x78U
#define REG_IP2366_VSET        0x02U

#define IP2366_PORT_C_OUT      0x08U
#define IP2366_PORT_C_IN       0x04U

static bool sw6236_read_reg_direct(uint8_t reg, uint8_t *value)
{
  return sw_i2c_read_reg1(SW6236_I2C_ADDR, reg, value);
}

static bool __attribute__((unused)) sw6236_read_reg_gateway(uint8_t reg, uint8_t *value)
{
  uint8_t tx[2] = {I2C_READ_GATEWAY, reg};
  return sw_i2c_write_then_read(SW6236_I2C_ADDR, tx, 2U, value, 1U);
}

static uint16_t sw6236_ntc_current_ua(uint8_t reg0d)
{
  switch ((reg0d >> 6) & 0x03U) {
    case 0U: return 80U;
    case 1U: return 40U;
    case 2U: return 20U;
    default: return 0U;
  }
}

static uint16_t ip2366_ntc_current_ua(uint8_t reg77)
{
  return (reg77 & 0x80U) ? 80U : 20U;
}

void sw6236_defaults(sw6236_metrics_t *m)
{
  memset(m, 0, sizeof(*m));
  m->set_capacity_mah = DEFAULT_BATTERY_CAPACITY_MAH;
  m->set_battery_mv = DEFAULT_BATTERY_VOLTAGE_MV;
}

bool sw6236_read_reg(uint8_t reg, uint8_t *value)
{
  return sw6236_read_reg_direct(reg, value);
}

bool sw6236_write_reg(uint8_t reg, uint8_t value)
{
  return sw_i2c_write_reg1(SW6236_I2C_ADDR, reg, value);
}

bool sw6236_update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
  uint8_t current;
  if (!sw6236_read_reg(reg, &current)) {
    return false;
  }
  current = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
  return sw6236_write_reg(reg, current);
}

bool sw6236_write_enable(void)
{
  return sw6236_write_reg(REG_LOW_POWER, 0x01U) &&
         sw6236_write_reg(REG_WRITE_ENABLE, 0x20U) &&
         sw6236_write_reg(REG_WRITE_ENABLE, 0x40U) &&
         sw6236_write_reg(REG_WRITE_ENABLE, 0x80U);
}

bool sw6236_write_finish(void)
{
  return sw6236_write_reg(REG_LOW_POWER, 0x00U);
}

bool sw6236_read_adc(sw6236_adc_sel_t sel, uint16_t *raw)
{
  uint8_t lo;
  uint8_t hi;
  uint8_t cfg;

  if (!sw6236_write_reg(REG_ADC_CONFIG, (uint8_t)sel)) {
    return false;
  }
  delay_ms(2);
  if (!sw6236_read_reg(REG_ADC_CONFIG, &cfg) ||
      !sw6236_read_reg(REG_ADC_LOW, &lo) ||
      !sw6236_read_reg(REG_ADC_HIGH, &hi)) {
    return false;
  }
  *raw = (uint16_t)(((uint16_t)(hi & 0x0FU) << 8) | lo);
  return true;
}

static bool read_adc_regs(sw6236_adc_sel_t sel, uint8_t *cfg, uint8_t *lo, uint8_t *hi, uint16_t *raw)
{
  if (!sw6236_write_reg(REG_ADC_CONFIG, (uint8_t)sel)) {
    return false;
  }
  delay_ms(2);
  if (!sw6236_read_reg(REG_ADC_CONFIG, cfg) ||
      !sw6236_read_reg(REG_ADC_LOW, lo) ||
      !sw6236_read_reg(REG_ADC_HIGH, hi)) {
    return false;
  }
  *raw = (uint16_t)(((uint16_t)(*hi & 0x0FU) << 8) | *lo);
  return true;
}

static uint16_t sw6236_median3_u16(uint16_t a, uint16_t b, uint16_t c)
{
  if (a > b) {
    uint16_t t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    uint16_t t = b;
    b = c;
    c = t;
  }
  if (a > b) {
    uint16_t t = a;
    a = b;
    b = t;
  }
  return b;
}

static bool __attribute__((unused)) sw6236_read_adc_stable(sw6236_adc_sel_t sel, uint16_t *raw)
{
  uint16_t a;
  uint16_t b;
  uint16_t c;

  if (!sw6236_read_adc(sel, &a) ||
      !sw6236_read_adc(sel, &b) ||
      !sw6236_read_adc(sel, &c)) {
    return false;
  }

  *raw = sw6236_median3_u16(a, b, c);
  return true;
}

static bool __attribute__((unused)) sw6236_read_adc_stable_regs(sw6236_adc_sel_t sel, uint8_t *cfg, uint8_t *lo, uint8_t *hi, uint16_t *raw)
{
  uint8_t cfg_a;
  uint8_t lo_a;
  uint8_t hi_a;
  uint8_t cfg_b;
  uint8_t lo_b;
  uint8_t hi_b;
  uint8_t cfg_c;
  uint8_t lo_c;
  uint8_t hi_c;
  uint16_t a;
  uint16_t b;
  uint16_t c;

  if (!read_adc_regs(sel, &cfg_a, &lo_a, &hi_a, &a) ||
      !read_adc_regs(sel, &cfg_b, &lo_b, &hi_b, &b) ||
      !read_adc_regs(sel, &cfg_c, &lo_c, &hi_c, &c)) {
    return false;
  }

  *cfg = cfg_c;
  *lo = lo_c;
  *hi = hi_c;
  *raw = sw6236_median3_u16(a, b, c);
  return true;
}

static bool ip2366_read_u16_le(uint8_t lo_reg, uint16_t *value)
{
  uint8_t lo;
  uint8_t hi;

  if (!sw6236_read_reg(lo_reg, &lo)) {
    return false;
  }
  delay_ms(1);
  if (!sw6236_read_reg((uint8_t)(lo_reg + 1U), &hi)) {
    return false;
  }

  *value = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
  return true;
}

static uint8_t ip2366_fast_charge_code(uint8_t typec_state)
{
  if (typec_state & 0x20U) return 3U;
  if (typec_state & 0x04U) return 4U;
  if (typec_state & 0x10U) return 1U;
  if (typec_state & 0x08U) return 2U;
  return 0U;
}

static uint8_t __attribute__((unused)) read_percent(void)
{
  uint8_t percent = 0;
  if (sw6236_read_reg(REG_PERCENT_FINAL, &percent)) {
    percent &= 0x7FU;
    if (percent > 100U) {
      percent = 100U;
    }
  }
  return percent;
}

static uint16_t sw6236_capacity_nominal_mv_from_type(uint8_t type)
{
  switch (type) {
    case 0U: return 3650U;
    case 1U: return 4100U;
    case 2U: return 4200U;
    case 3U: return 4350U;
    case 4U: return 4400U;
    default: return DEFAULT_BATTERY_VOLTAGE_MV;
  }
}

const char *sw6236_battery_type_name(uint8_t type)
{
  switch (type) {
    case 0U: return "3.65V";
    case 1U: return "4.10V";
    case 2U: return "4.20V";
    case 3U: return "4.35V";
    case 4U: return "4.40V";
    default: return "4.20V";
  }
}

bool sw6236_read_battery_type(uint8_t *type)
{
  uint8_t reg02;
  uint16_t mv;

  if (!sw6236_read_reg(REG_IP2366_VSET, &reg02)) {
    return false;
  }

  mv = (uint16_t)(2500U + (uint16_t)reg02 * 10U);
  if (mv < 3875U) *type = 0U;
  else if (mv < 4150U) *type = 1U;
  else if (mv < 4275U) *type = 2U;
  else if (mv < 4375U) *type = 3U;
  else *type = 4U;
  return true;
}

bool sw6236_set_battery_type(uint8_t type)
{
  static const uint16_t mv_table[] = {3650U, 4100U, 4200U, 4350U, 4400U};
  uint8_t verify;
  uint16_t mv;

  if (type >= (uint8_t)(sizeof(mv_table) / sizeof(mv_table[0]))) {
    type = 2U;
  }

  mv = mv_table[type];
  if (!sw6236_write_reg(REG_IP2366_VSET, (uint8_t)((mv - 2500U) / 10U))) {
    return false;
  }
  delay_ms(10);
  return sw6236_read_battery_type(&verify) && (verify == type);
}

static uint16_t sw6236_capacity_nominal_mv(void)
{
  uint8_t type;

  if (sw6236_read_battery_type(&type)) {
    return sw6236_capacity_nominal_mv_from_type(type);
  }
  return 3700U;
}

uint32_t sw6236_capacity_limit_mah(void)
{
  return DEFAULT_BATTERY_CAPACITY_MAH;
}

static bool __attribute__((unused)) sw6236_read_max_capacity_mah(uint32_t *mah)
{
  uint8_t lo;
  uint8_t hi;
  uint16_t code;
  uint32_t mwh;
  uint32_t capacity;
  uint16_t nominal_mv;

  if (!sw6236_read_reg(REG_MAX_CAP_LOW, &lo) ||
      !sw6236_read_reg(REG_MAX_CAP_HIGH, &hi)) {
    return false;
  }

  code = (uint16_t)((uint16_t)lo | ((uint16_t)(hi & 0x0FU) << 8));
  mwh = ((uint32_t)code * CAPACITY_UNIT_CWH + 50UL) / 100UL;
  nominal_mv = sw6236_capacity_nominal_mv();
  capacity = ((mwh * 1000UL) + (nominal_mv / 2U)) / nominal_mv;
  *mah = capacity;
  return true;
}

static bool __attribute__((unused)) sw6236_read_ntc_temperature(sw6236_metrics_t *m)
{
  uint16_t raw;
  uint8_t reg0d;
  uint16_t current_ua;
  uint32_t resistance_ohm;
  int16_t temp_c10;

  if (!sw6236_read_adc(SW6236_ADC_NTC_VOLT, &raw)) {
    return false;
  }
  if (!sw6236_read_reg(REG_NTC_CURRENT, &reg0d)) {
    return false;
  }

  current_ua = sw6236_ntc_current_ua(reg0d);
  if (current_ua == 0U) {
    return false;
  }

  m->ntc_raw = raw;
  m->ntc_mv = (uint16_t)((raw * 11U) / 10U);
  resistance_ohm = ((uint32_t)raw * 1100UL) / (uint32_t)current_ua;
  temp_c10 = thermistor_temp_from_ohms_c10(resistance_ohm);
  if (temp_c10 < -400 || temp_c10 > 1500) {
    return false;
  }

  m->ntc_temp_c10 = temp_c10;
  return true;
}

static bool ip2366_read_ntc_temperature(sw6236_metrics_t *m)
{
  uint8_t reg77;
  uint16_t ntc_mv;
  uint16_t current_ua;
  uint32_t resistance_ohm;
  int16_t temp_c10;

  if (!sw6236_read_reg(REG_IP2366_NTC_CURRENT, &reg77)) {
    return false;
  }
  if (!ip2366_read_u16_le(REG_IP2366_NTC_MV_LO, &ntc_mv) || ntc_mv > 3300U) {
    return false;
  }

  current_ua = ip2366_ntc_current_ua(reg77);
  if (current_ua == 0U) {
    return false;
  }

  resistance_ohm = ((uint32_t)ntc_mv * 1000UL) / (uint32_t)current_ua;
  temp_c10 = thermistor_temp_from_ohms_c10(resistance_ohm);
  if (temp_c10 < -400 || temp_c10 > 1500) {
    return false;
  }

  m->ntc_raw = ntc_mv;
  m->ntc_mv = ntc_mv;
  m->chip_temp_c10 = temp_c10;
  return true;
}

static uint8_t __attribute__((unused)) ip2366_estimate_percent_from_vbat(uint16_t vbat_mv)
{
  uint16_t cell_mv = vbat_mv;
  uint8_t cells;

  if (vbat_mv == 0U) {
    return 0U;
  }
  if (vbat_mv > 5000U) {
    cells = (uint8_t)((vbat_mv + 2100U) / 4200U);
    if (cells == 0U) {
      cells = 1U;
    }
    cell_mv = (uint16_t)(vbat_mv / cells);
  }

  if (cell_mv <= 3200U) return 0U;
  if (cell_mv >= 4200U) return 100U;
  if (cell_mv < 3500U) return (uint8_t)(((uint32_t)(cell_mv - 3200U) * 20UL) / 300UL);
  if (cell_mv < 3700U) return (uint8_t)(20U + (((uint32_t)(cell_mv - 3500U) * 30UL) / 200UL));
  if (cell_mv < 3900U) return (uint8_t)(50U + (((uint32_t)(cell_mv - 3700U) * 30UL) / 200UL));
  return (uint8_t)(80U + (((uint32_t)(cell_mv - 3900U) * 20UL) / 300UL));
}

bool sw6236_read_metrics(sw6236_metrics_t *m)
{
  uint16_t value;
  uint8_t fail_count = 0U;

  m->powered = true;
  m->online = false;
  m->suspect_damage = false;
  m->b0_ok = false;

  if (!sw6236_read_reg(REG_IP2366_STATE0, &m->mode_status)) fail_count++;
  if (!sw6236_read_reg(REG_IP2366_STATE2, &m->system_status)) fail_count++;
  if (!sw6236_read_reg(REG_IP2366_TYPEC_STATE, &m->typec_status)) fail_count++;
  if (!sw6236_read_reg(REG_IP2366_FAULT, &m->discharge_error)) fail_count++;

  if (fail_count >= 3U) {
    m->suspect_damage = true;
    return false;
  }

  m->online = true;
  m->chip_version = 0x66U;
  m->fast_charge = ip2366_fast_charge_code(m->typec_status);
  m->event_status = m->system_status;
  m->charge_error = 0U;
  m->discharge_error = (uint8_t)(((m->discharge_error & 0x20U) ? 0x08U : 0U) |
                                 ((m->discharge_error & 0x10U) ? 0x04U : 0U));
  m->port_online = 0U;
  if (m->typec_status & 0x40U) {
    m->port_online |= IP2366_PORT_C_OUT;
  }
  if (m->typec_status & 0x80U) {
    m->port_online |= IP2366_PORT_C_IN;
  }

  if (ip2366_read_u16_le(REG_IP2366_VSYS_LO, &value) && value <= 30000U) {
    m->vbus_mv = value;
  }
  if (ip2366_read_u16_le(REG_IP2366_ISYS_LO, &value) && value <= 20000U) {
    m->ibus_ma = value;
  }
  if (ip2366_read_u16_le(REG_IP2366_POWER_LO, &value)) {
    m->power_mw = (uint32_t)value * 10UL;
  } else {
    m->power_mw = ((uint32_t)m->vbus_mv * (uint32_t)m->ibus_ma) / 1000UL;
  }
  if (ip2366_read_u16_le(REG_IP2366_BATV_LO, &value) && value <= 30000U) {
    m->vbat_raw = value;
    m->vbat_mv = value;
  }
  if (ip2366_read_u16_le(REG_IP2366_IBAT_LO, &value)) {
    m->ibat_ma = value;
  }
  (void)ip2366_read_ntc_temperature(m);

  if (m->percent > 100U) {
    m->percent = 0U;
  }
  return true;
}

bool sw6236_read_capacity_snapshot(uint32_t *max_capacity_mah, uint32_t *current_capacity_mah, bool *learned)
{
  if (max_capacity_mah != 0) {
    *max_capacity_mah = DEFAULT_BATTERY_CAPACITY_MAH;
  }
  if (current_capacity_mah != 0) {
    *current_capacity_mah = 0U;
  }
  if (learned != 0) {
    *learned = false;
  }

  return false;
}

bool sw6236_set_output_voltage_mv(uint16_t mv)
{
  uint16_t code;
  bool ok;

  if (mv < 4500U) {
    mv = 4500U;
  }
  if (mv > 12030U) {
    mv = 12030U;
  }

  code = (uint16_t)(mv / 10U);
  ok = sw6236_write_enable();
  ok = sw6236_write_reg(REG_VOUT_LOW, (uint8_t)(code & 0xFFU)) && ok;
  ok = sw6236_update_bits(REG_VOUT_HIGH, 0x07U, (uint8_t)((code >> 8) & 0x07U)) && ok;
  ok = sw6236_write_finish() && ok;
  return ok;
}

bool sw6236_set_max_capacity_mah(uint32_t mah)
{
  (void)mah;
  return false;
#if 0
  uint32_t max_mah;
  uint32_t mwh;
  uint16_t code;
  uint16_t nominal_mv;
  bool ok;

  if (mah < 1000U) {
    mah = 1000U;
  }
  max_mah = sw6236_capacity_limit_mah();
  if (mah > max_mah) {
    mah = max_mah;
  }

  nominal_mv = sw6236_capacity_nominal_mv();
  mwh = (mah * (uint32_t)nominal_mv + 500UL) / 1000UL;
  code = (uint16_t)((mwh * 100UL + (CAPACITY_UNIT_CWH / 2UL)) / CAPACITY_UNIT_CWH);
  if (code > CAPACITY_CODE_MAX) {
    code = CAPACITY_CODE_MAX;
  }

  ok = sw6236_write_enable();
  ok = sw6236_write_reg(REG_MAX_CAP_LOW, (uint8_t)(code & 0xFFU)) && ok;
  ok = sw6236_write_reg(REG_MAX_CAP_HIGH, (uint8_t)(0x80U | ((code >> 8) & 0x0FU))) && ok;
  ok = sw6236_write_finish() && ok;
  return ok;
#endif
}

bool sw6236_set_low_current_mode(bool enable)
{
  (void)enable;
  return false;
#if 0
  uint8_t i;
  bool active;

  for (i = 0U; i < 3U; ++i) {
    bool ok = sw6236_write_enable();

    ok = sw6236_update_bits(REG_PORT_MODE_CONFIG, 0x0CU, enable ? 0x0CU : 0x08U) && ok;
    ok = sw6236_write_finish() && ok;
    if (!ok) {
      continue;
    }

    delay_ms(20);
    if (enable) {
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x00U);
      (void)sw6236_write_finish();
      delay_ms(20);
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x08U);
      (void)sw6236_write_finish();
    } else {
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x08U);
      (void)sw6236_write_finish();
      delay_ms(20);
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x00U);
      (void)sw6236_write_finish();
      delay_ms(40);
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x08U);
      (void)sw6236_write_finish();
      delay_ms(20);
      (void)sw6236_write_enable();
      (void)sw6236_write_reg(REG_MODE_STATUS, 0x00U);
      (void)sw6236_write_finish();
    }

    delay_ms(160);
    if (sw6236_read_low_current_active(&active) && (active == enable)) {
      return true;
    }
  }

  return false;
#endif
}

bool sw6236_read_mode_status(uint8_t *mode_status)
{
  return sw6236_read_reg(REG_MODE_STATUS, mode_status);
}

bool sw6236_read_low_current_active(bool *active)
{
  uint8_t mode_status;

  if (!sw6236_read_mode_status(&mode_status)) {
    return false;
  }

  *active = (mode_status & 0x01U) != 0U;
  return true;
}

const char *sw6236_fast_charge_name(uint8_t code)
{
  switch (code & 0x0FU) {
    case 1: return "PD IN";
    case 2: return "QC IN";
    case 3: return "PD OUT";
    case 4: return "QC OUT";
    default: return "NONE";
  }
}

const char *sw6236_port_summary(uint8_t port_online)
{
  if (port_online & 0x08U) return "C OUT";
  if (port_online & 0x04U) return "C IN";
  if (port_online & 0x20U) return "A1";
  if (port_online & 0x10U) return "A2";
  if (port_online & 0x0CU) return "C1";
  if (port_online & 0x03U) return "C2";
  return "--";
}
