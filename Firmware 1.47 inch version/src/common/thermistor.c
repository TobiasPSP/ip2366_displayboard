#include "thermistor.h"
#include "project_config.h"

typedef struct {
  int16_t temp_c10;
  uint32_t ohms;
} ntc_point_t;

static const ntc_point_t k_ntc_10k_3950[] = {
  {-200, 105384UL}, {-150, 77898UL}, {-100, 58245UL}, {-50, 44026UL},
  {0, 33621UL},    {50, 25888UL},   {100, 20174UL},  {150, 15899UL},
  {200, 12670UL},  {250, 10000UL},  {300, 8037UL},   {350, 6531UL},
  {400, 5324UL},   {450, 4365UL},   {500, 3600UL},   {550, 2985UL},
  {600, 2488UL},   {650, 2083UL},   {700, 1752UL},   {750, 1480UL},
  {800, 1255UL},   {850, 1070UL},   {900, 915UL},    {950, 787UL},
  {1000, 680UL}
};

int16_t thermistor_temp_from_ohms_c10(uint32_t resistance_ohm)
{
  const uint8_t count = (uint8_t)(sizeof(k_ntc_10k_3950) / sizeof(k_ntc_10k_3950[0]));

  if (resistance_ohm >= k_ntc_10k_3950[0].ohms) {
    return k_ntc_10k_3950[0].temp_c10;
  }
  if (resistance_ohm <= k_ntc_10k_3950[count - 1U].ohms) {
    return k_ntc_10k_3950[count - 1U].temp_c10;
  }

  for (uint8_t i = 0; i < (uint8_t)(count - 1U); ++i) {
    const ntc_point_t *hot = &k_ntc_10k_3950[i + 1U];
    const ntc_point_t *cold = &k_ntc_10k_3950[i];

    if (resistance_ohm <= cold->ohms && resistance_ohm >= hot->ohms) {
      uint32_t ohm_span = cold->ohms - hot->ohms;
      uint32_t ohm_pos = cold->ohms - resistance_ohm;
      int16_t temp_span = (int16_t)(hot->temp_c10 - cold->temp_c10);
      return (int16_t)(cold->temp_c10 + (int32_t)ohm_pos * temp_span / (int32_t)ohm_span);
    }
  }

  return 0;
}

int16_t thermistor_temperature_c10(uint16_t adc_code)
{
  uint32_t resistance;

  if (adc_code >= 4095U) {
    adc_code = 4094U;
  }
  if (adc_code == 0U) {
    adc_code = 1U;
  }

  resistance = (NTC_FIXED_RESISTOR_OHM * (uint32_t)adc_code) / (4095UL - (uint32_t)adc_code);
  return thermistor_temp_from_ohms_c10(resistance);
}

bool thermistor_should_run_fan_c10(int16_t temp_c10, bool current_state)
{
  if (current_state) {
    return temp_c10 >= FAN_OFF_TEMP_C10;
  }
  return temp_c10 >= FAN_ON_TEMP_C10;
}
