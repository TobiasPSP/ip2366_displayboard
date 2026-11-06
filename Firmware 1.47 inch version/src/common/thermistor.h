#ifndef THERMISTOR_H
#define THERMISTOR_H

#include <stdbool.h>
#include <stdint.h>

int16_t thermistor_temperature_c10(uint16_t adc_code);
int16_t thermistor_temp_from_ohms_c10(uint32_t resistance_ohm);
bool thermistor_should_run_fan_c10(int16_t temp_c10, bool current_state);

#endif
