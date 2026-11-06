#include "app_settings.h"
#include "project_config.h"
#include "py32f0xx_hal.h"
#include <string.h>

#define SETTINGS_MAGIC 0x32575053UL
#define SETTINGS_VERSION 11U
#define SETTINGS_PREV_VERSION 10U
#define SETTINGS_SLEEP_TIMEOUT_MAX 25U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t capacity_mah;
  uint32_t gauge_capacity_mah;
  uint32_t gauge_remaining_mah;
  uint8_t battery_type;
  uint8_t language;
  uint8_t gauge_learned;
  uint8_t fan_on_temp_c;
  uint8_t fan_off_temp_c;
  uint8_t screen_rotation;
  uint8_t display_prefs;
  int8_t temp_offset_c10;
  uint8_t last_screen;
  uint32_t checksum;
} settings_record_t;

static uint32_t checksum_words(const settings_record_t *record)
{
  const uint32_t *words = (const uint32_t *)record;
  uint32_t sum = 0xA5A55A5AUL;
  uint32_t i;

  for (i = 0; i < ((sizeof(*record) - sizeof(record->checksum)) / sizeof(uint32_t)); ++i) {
    sum ^= words[i] + 0x9E3779B9UL + (sum << 6) + (sum >> 2);
  }
  return sum;
}

void app_settings_defaults(app_settings_t *settings)
{
  settings->capacity_mah = DEFAULT_BATTERY_CAPACITY_MAH;
  settings->gauge_capacity_mah = DEFAULT_BATTERY_CAPACITY_MAH;
  settings->gauge_remaining_mah = 0U;
  settings->battery_type = 2U;
  settings->language = 1U;
  settings->gauge_learned = 0U;
  settings->fan_on_temp_c = 60U;
  settings->fan_off_temp_c = 50U;
  settings->screen_rotation = 0U;
  settings->brightness_percent = 100U;
  settings->sleep_timeout = 0U;
  settings->temp_offset_c10 = 0;
  settings->last_screen = 0U;
  settings->battery_percent_mode = 0U;
  settings->battery_series = 0U;
}

static uint8_t brightness_to_code(uint8_t percent)
{
  if (percent < 20U) percent = 20U;
  if (percent > 100U) percent = 100U;
  return (uint8_t)((percent / 20U) - 1U);
}

static uint8_t brightness_from_code(uint8_t code)
{
  if (code > 4U) code = 4U;
  return (uint8_t)((code + 1U) * 20U);
}

bool app_settings_load(app_settings_t *settings)
{
  const settings_record_t *record = (const settings_record_t *)APP_SETTINGS_FLASH_ADDR;
  uint8_t sleep_timeout;

  app_settings_defaults(settings);
  if (record->magic != SETTINGS_MAGIC ||
      (record->version != SETTINGS_VERSION && record->version != SETTINGS_PREV_VERSION && record->version != 9U && record->version != 8U) ||
      record->length != sizeof(settings_record_t) ||
      record->checksum != checksum_words(record) ||
      record->battery_type > 4U ||
      record->language > 1U ||
      record->gauge_learned > 1U ||
      record->fan_on_temp_c > 100U ||
      record->fan_off_temp_c > 100U ||
      record->screen_rotation > 1U ||
      ((record->display_prefs >> 4) > 4U) ||
      record->temp_offset_c10 < -100 ||
      record->temp_offset_c10 > 100 ||
      (record->last_screen & 0x03U) > 3U ||
      (record->version == SETTINGS_VERSION && (record->last_screen >> 5) > 6U) ||
      record->capacity_mah < 1000UL ||
      record->capacity_mah > 250000UL ||
      record->gauge_capacity_mah < 1000UL ||
      record->gauge_capacity_mah > 250000UL ||
      record->gauge_remaining_mah > record->gauge_capacity_mah) {
    return false;
  }
  sleep_timeout = (uint8_t)(record->display_prefs & 0x03U);
  if (record->version == SETTINGS_VERSION || record->version == SETTINGS_PREV_VERSION) {
    sleep_timeout |= (uint8_t)(((record->last_screen >> 2) & 0x07U) << 2);
  }
  if (sleep_timeout > SETTINGS_SLEEP_TIMEOUT_MAX) {
    return false;
  }

  settings->capacity_mah = record->capacity_mah;
  settings->gauge_capacity_mah = record->gauge_capacity_mah;
  settings->gauge_remaining_mah = record->gauge_remaining_mah;
  settings->battery_type = record->battery_type;
  settings->language = record->language;
  settings->gauge_learned = record->gauge_learned;
  settings->fan_on_temp_c = record->fan_on_temp_c;
  settings->fan_off_temp_c = record->fan_off_temp_c;
  settings->screen_rotation = record->screen_rotation;
  settings->brightness_percent = brightness_from_code((uint8_t)(record->display_prefs >> 4));
  settings->sleep_timeout = sleep_timeout;
  settings->temp_offset_c10 = record->temp_offset_c10;
  settings->last_screen = (uint8_t)(record->last_screen & 0x03U);
  settings->battery_percent_mode = ((record->version == SETTINGS_VERSION || record->version == SETTINGS_PREV_VERSION || record->version == 9U) &&
                                    (record->display_prefs & 0x04U)) ? 1U : 0U;
  settings->battery_series = record->version == SETTINGS_VERSION ? (uint8_t)(record->last_screen >> 5) : 0U;
  return true;
}

bool app_settings_save(const app_settings_t *settings)
{
  uint32_t page[FLASH_PAGE_SIZE / sizeof(uint32_t)];
  settings_record_t record;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  bool ok;

  memset(page, 0xFF, sizeof(page));
  memset(&record, 0, sizeof(record));
  record.magic = SETTINGS_MAGIC;
  record.version = SETTINGS_VERSION;
  record.length = sizeof(record);
  record.capacity_mah = settings->capacity_mah;
  record.gauge_capacity_mah = settings->gauge_capacity_mah < 1000UL ? DEFAULT_BATTERY_CAPACITY_MAH : settings->gauge_capacity_mah;
  record.gauge_remaining_mah = settings->gauge_remaining_mah > record.gauge_capacity_mah ? record.gauge_capacity_mah : settings->gauge_remaining_mah;
  record.battery_type = settings->battery_type > 4U ? 2U : settings->battery_type;
  record.language = settings->language > 1U ? 1U : settings->language;
  record.gauge_learned = settings->gauge_learned > 1U ? 1U : settings->gauge_learned;
  record.fan_on_temp_c = settings->fan_on_temp_c > 100U ? 60U : settings->fan_on_temp_c;
  record.fan_off_temp_c = settings->fan_off_temp_c > 100U ? 50U : settings->fan_off_temp_c;
  record.screen_rotation = settings->screen_rotation > 1U ? 0U : settings->screen_rotation;
  record.display_prefs = (uint8_t)((brightness_to_code(settings->brightness_percent) << 4) |
                                   ((settings->sleep_timeout > SETTINGS_SLEEP_TIMEOUT_MAX ? 0U : settings->sleep_timeout) & 0x03U) |
                                   ((settings->battery_percent_mode > 1U ? 0U : settings->battery_percent_mode) << 2));
  record.temp_offset_c10 = settings->temp_offset_c10 < -100 ? -100 :
                           (settings->temp_offset_c10 > 100 ? 100 : settings->temp_offset_c10);
  record.last_screen = (uint8_t)((settings->last_screen > 3U ? 0U : settings->last_screen) |
                                 ((((settings->sleep_timeout > SETTINGS_SLEEP_TIMEOUT_MAX ? 0U : settings->sleep_timeout) >> 2) & 0x07U) << 2) |
                                 ((settings->battery_series >= 2U && settings->battery_series <= 6U ? settings->battery_series : 0U) << 5));
  record.checksum = checksum_words(&record);
  memcpy(page, &record, sizeof(record));

  erase.TypeErase = FLASH_TYPEERASE_PAGEERASE;
  erase.PageAddress = APP_SETTINGS_FLASH_ADDR;
  erase.NbPages = 1U;

  HAL_FLASH_Unlock();
  ok = (HAL_FLASH_Erase(&erase, &page_error) == HAL_OK);
  if (ok) {
    ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, APP_SETTINGS_FLASH_ADDR, page) == HAL_OK);
  }
  HAL_FLASH_Lock();

  return ok;
}
