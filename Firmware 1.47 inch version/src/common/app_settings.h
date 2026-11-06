#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t capacity_mah;
  uint32_t gauge_capacity_mah;
  uint32_t gauge_remaining_mah;
  uint8_t battery_type;
  uint8_t language;
  uint8_t gauge_learned;
  uint8_t fan_on_temp_c;
  uint8_t fan_off_temp_c;
  uint8_t screen_rotation;
  uint8_t brightness_percent;
  uint8_t sleep_timeout;
  int8_t temp_offset_c10;
  uint8_t last_screen;
  uint8_t battery_percent_mode;
  /* 0 means use the startup voltage estimate; 2..6 is user locked. */
  uint8_t battery_series;
} app_settings_t;

void app_settings_defaults(app_settings_t *settings);
bool app_settings_load(app_settings_t *settings);
bool app_settings_save(const app_settings_t *settings);

#endif
