#include "board.h"
#include "app_settings.h"
#include "project_config.h"
#include "st7735.h"
#include "sw6236.h"
#include "thermistor.h"
#include "zh_font.h"
#include <string.h>

typedef enum {
  SCREEN_PAGE1 = 0,
  SCREEN_PAGE2,
  SCREEN_PAGE3,
  SCREEN_PAGE4,
  SCREEN_MENU,
  SCREEN_EDIT_SETTING,
  SCREEN_LANGUAGE,
  SCREEN_ABOUT,
  SCREEN_FACTORY_RESET,
  SCREEN_FIRST_SETUP
} screen_t;

typedef enum {
  LANG_ENGLISH = 0,
  LANG_CHINESE = 1
} language_t;

typedef enum {
  MENU_BATTERY_SERIES = 0,
  MENU_OUTPUT_POWER,
  MENU_BATTERY_CAPACITY,
  MENU_BATTERY_PERCENT_MODE,
  MENU_BATTERY_VOLTAGE,
  MENU_BATTERY_CURRENT,
  MENU_TRICKLE_CURRENT,
  MENU_STOP_CURRENT,
  MENU_RECHARGE_THRESHOLD,
  MENU_LOW_BATTERY_VOLTAGE,
  MENU_OUTPUT_ENABLE,
  MENU_TYPEC_MODE,
  MENU_FAN_TEMP,
  MENU_BRIGHTNESS,
  MENU_SLEEP_TIMEOUT,
  MENU_TEMP_CALIBRATION,
  MENU_SCREEN_ROTATION,
  MENU_FACTORY_RESET,
  MENU_LANGUAGE,
  MENU_ABOUT,
  MENU_COUNT
} menu_item_t;

typedef struct {
  sw6236_metrics_t metrics;
  screen_t screen;
  uint8_t last_data_screen;
  uint8_t menu_index;
  uint8_t menu_top;
  uint32_t set_capacity_mah;
  uint16_t set_battery_mv;
  uint8_t set_battery_type;
  uint8_t current_battery_type;
  uint32_t capacity_max_mah;
  uint32_t capacity_current_mah;
  uint32_t capacity_limit_mah;
  bool capacity_learned;
  bool capacity_snapshot_valid;
  uint32_t last_sample_ms;
  uint32_t last_draw_ms;
  uint32_t last_status_rotate_ms;
  uint8_t status_rotate_index;
  uint8_t edit_menu_index;
  uint8_t edit_value;
  uint8_t edit_current;
  uint8_t language_select;
  uint8_t edit_sub_index;
  uint8_t repeat_key;
  uint32_t repeat_next_ms;
  uint32_t gauge_capacity_cmah;
  uint32_t gauge_remaining_cmah;
  uint32_t gauge_cycle_cmah;
  int32_t gauge_rem_mams;
  int32_t usb_capacity_cmah;
  int32_t usb_capacity_rem_mams;
  bool gauge_initialized;
  bool gauge_was_full;
  bool gauge_was_empty;
  uint32_t last_gauge_save_ms;
  bool dirty;
  bool fan_on;
  uint8_t fan_on_temp_c;
  uint8_t fan_off_temp_c;
  uint32_t fan_started_ms;
  bool flash_ok;
  bool first_setup;
  uint8_t flash_id[3];
  language_t language;
  uint8_t screen_rotated_180;
  uint8_t brightness_percent;
  uint8_t sleep_timeout;
  int8_t temp_offset_c10;
  uint8_t battery_percent_mode;
  uint8_t battery_series;
  uint8_t battery_series_auto;
  uint16_t voltage_percent_q8;
  bool voltage_percent_filter_valid;
  bool display_sleeping;
  bool wake_key_suppressed;
  uint32_t last_activity_ms;
  uint32_t auto_no_vbus_since_ms;
} app_t;

static void save_settings(const app_t *app)
{
  app_settings_t settings;

  settings.capacity_mah = app->set_capacity_mah;
  settings.gauge_capacity_mah = (app->gauge_capacity_cmah + 50UL) / 100UL;
  settings.gauge_remaining_mah = (app->gauge_remaining_cmah + 50UL) / 100UL;
  settings.battery_type = app->set_battery_type;
  settings.language = (uint8_t)app->language;
  settings.gauge_learned = app->capacity_learned ? 1U : 0U;
  settings.fan_on_temp_c = app->fan_on_temp_c;
  settings.fan_off_temp_c = app->fan_off_temp_c;
  settings.screen_rotation = app->screen_rotated_180;
  settings.brightness_percent = app->brightness_percent;
  settings.sleep_timeout = app->sleep_timeout;
  settings.temp_offset_c10 = app->temp_offset_c10;
  settings.last_screen = app->last_data_screen;
  settings.battery_percent_mode = app->battery_percent_mode;
  settings.battery_series = app->battery_series;
  (void)app_settings_save(&settings);
}

static void draw_text16(uint16_t x, uint16_t y, const char *text, uint16_t fg)
{
  zh_font_draw_text16(x, y, text, fg, COLOR_BLACK);
}

static bool is_data_screen(screen_t screen)
{
  return screen >= SCREEN_PAGE1 && screen <= SCREEN_PAGE4;
}

static void remember_data_screen(app_t *app)
{
  if (is_data_screen(app->screen) && app->last_data_screen != (uint8_t)app->screen) {
    app->last_data_screen = (uint8_t)app->screen;
    save_settings(app);
  }
}

static uint8_t g_last_screen = 0xFFU;

static const char *tr(const app_t *app, const char *en, const char *zh)
{
  return app->language == LANG_CHINESE ? zh : en;
}

static void sample_once(app_t *app, uint32_t elapsed_ms);
static void pad_text(char *dst, uint32_t dst_len, const char *src, uint8_t width);
static void cache_copy(char *dst, uint32_t len, const char *src);
static void refresh_battery_type(app_t *app);
static void handle_key_repeat(app_t *app, uint32_t now);
static void format_usb_capacity(char *buf, uint32_t len, int32_t capacity_cmah);
static bool input_active(const sw6236_metrics_t *m);
static bool output_active(const sw6236_metrics_t *m);
static void wake_display(app_t *app, uint32_t now);

#define BATTERY_MAMS_PER_CMAH 36000L
#define BATTERY_GAUGE_SAVE_INTERVAL_MS 60000UL
#define USB_CAPACITY_MAMS_PER_CMAH BATTERY_MAMS_PER_CMAH
#define USB_CAPACITY_POSITIVE_MAX_CMAH 999999000L
#define USB_CAPACITY_NEGATIVE_MAX_CMAH 99999000L
#define BATTERY_PERCENT_MODE_GAUGE 0U
#define BATTERY_PERCENT_MODE_VOLTAGE 1U
#define BATTERY_VOLTAGE_FILTER_ALPHA_SHIFT 2U

#define IP2366_REG_VSET        0x02U
#define IP2366_REG_ISET        0x03U
#define IP2366_REG_TRICKLE     0x06U
#define IP2366_REG_STOP_RECHG  0x08U
#define IP2366_REG_LOW_BAT     0x0AU
#define IP2366_REG_OUTPUT_EN   0x0BU
#define IP2366_REG_OUTPUT_PWR  0x0CU
#define IP2366_REG_TYPEC_MODE  0x22U

#define BATTERY_FULL_VOLTAGE_MAX_CODE 190U

static const char *const k_recharge_text[] = {"V-N*0.05", "V-N*0.1", "V-N*0.2"};
static const char *const k_low_battery_text[] = {"2.50V*N", "2.60V*N", "2.70V*N", "2.80V*N", "2.90V*N", "3.00V*N", "3.10V*N", "3.20V*N"};
static const char *const k_output_power_text[] = {"30W", "45W", "60W", "65W", "100W", "140W"};
#define SLEEP_TIMEOUT_OFF 0U
#define SLEEP_TIMEOUT_AUTO 1U
#define SLEEP_TIMEOUT_MIN_STEP 2U
#define SLEEP_TIMEOUT_MAX_STEP 25U
#define AUTO_SLEEP_VBUS_ACTIVE_MV 3000U
#define AUTO_SLEEP_CURRENT_ACTIVE_MA 50U
#define AUTO_SLEEP_NO_VBUS_DELAY_MS 10000UL
#define AUTO_SLEEP_POLL_MS 1000U
#define DISPLAY_RENDER_INTERVAL_MS 40U
#define DISPLAY_IDLE_VBUS_MV 300U
#define DISPLAY_IDLE_IBUS_MA 30U
#define DISPLAY_IDLE_POWER_MW 300UL

#define TEMP_OFFSET_MIN_C10 (-100)
#define TEMP_OFFSET_MAX_C10 100
#define MENU_VISIBLE_ROWS 5U

static uint32_t append_char(char *buf, uint32_t len, uint32_t pos, char c)
{
  if (len == 0U) {
    return pos;
  }
  if (pos < (len - 1U)) {
    buf[pos] = c;
  }
  return pos + 1U;
}

static uint32_t append_text(char *buf, uint32_t len, uint32_t pos, const char *text)
{
  while ((text != 0) && (*text != '\0')) {
    pos = append_char(buf, len, pos, *text++);
  }
  return pos;
}

static uint32_t append_u32(char *buf, uint32_t len, uint32_t pos, uint32_t value)
{
  char tmp[10];
  uint8_t count = 0;

  do {
    tmp[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U && count < sizeof(tmp));

  while (count > 0U) {
    pos = append_char(buf, len, pos, tmp[--count]);
  }
  return pos;
}

static uint32_t append_u8_2(char *buf, uint32_t len, uint32_t pos, uint8_t value)
{
  pos = append_char(buf, len, pos, (char)('0' + (value / 10U)));
  return append_char(buf, len, pos, (char)('0' + (value % 10U)));
}

static uint32_t __attribute__((unused)) append_hex8(char *buf, uint32_t len, uint32_t pos, uint8_t value)
{
  static const char hex[] = "0123456789ABCDEF";
  pos = append_char(buf, len, pos, hex[(value >> 4) & 0x0FU]);
  return append_char(buf, len, pos, hex[value & 0x0FU]);
}

static void finish_text(char *buf, uint32_t len, uint32_t pos)
{
  if (len == 0U) {
    return;
  }
  buf[(pos < len) ? pos : (len - 1U)] = '\0';
}

static void format_decimal_unit(char *buf, uint32_t len, uint32_t whole, uint8_t frac, uint8_t frac_digits, char unit)
{
  uint32_t pos = 0;

  pos = append_u32(buf, len, pos, whole);
  pos = append_char(buf, len, pos, '.');
  if (frac_digits == 2U) {
    pos = append_u8_2(buf, len, pos, frac);
  } else {
    pos = append_char(buf, len, pos, (char)('0' + (frac % 10U)));
  }
  pos = append_char(buf, len, pos, unit);
  finish_text(buf, len, pos);
}

static void format_ma(char *buf, uint32_t len, uint16_t ma)
{
  uint32_t pos = append_u32(buf, len, 0, ma);
  pos = append_text(buf, len, pos, "mA");
  finish_text(buf, len, pos);
}

static void format_temp_c_value(char *buf, uint32_t len, uint8_t temp_c, bool degree_symbol)
{
  uint32_t pos = append_u32(buf, len, 0, temp_c);
  (void)degree_symbol;
  pos = append_text(buf, len, pos, "\xE2\x84\x83");
  finish_text(buf, len, pos);
}

static void format_temp_offset_value(char *buf, uint32_t len, int8_t temp_c10)
{
  uint32_t pos = 0;
  uint8_t mag;

  if (temp_c10 < 0) {
    pos = append_char(buf, len, pos, '-');
    mag = (uint8_t)(-temp_c10);
  } else {
    pos = append_char(buf, len, pos, '+');
    mag = (uint8_t)temp_c10;
  }
  pos = append_u32(buf, len, pos, mag / 10U);
  pos = append_char(buf, len, pos, '.');
  pos = append_char(buf, len, pos, (char)('0' + (mag % 10U)));
  pos = append_text(buf, len, pos, "\xE2\x84\x83");
  finish_text(buf, len, pos);
}

static void format_percent_value(char *buf, uint32_t len, uint8_t percent)
{
  uint32_t pos;

  if (percent > 100U) percent = 100U;
  pos = append_u32(buf, len, 0, percent);
  pos = append_char(buf, len, pos, '%');
  finish_text(buf, len, pos);
}

static void format_capacity_mah(char *buf, uint32_t len, uint32_t mah)
{
  uint32_t pos = append_u32(buf, len, 0, mah);
  pos = append_text(buf, len, pos, "mAh");
  finish_text(buf, len, pos);
}

static void format_battery_series(char *buf, uint32_t len, uint8_t series)
{
  uint32_t pos = 0;

  if (series >= 2U && series <= 6U) {
    pos = append_u32(buf, len, pos, series);
    pos = append_char(buf, len, pos, 'S');
  } else {
    pos = append_text(buf, len, pos, "--");
  }
  finish_text(buf, len, pos);
}

static uint16_t battery_full_mv_from_type(uint8_t type)
{
  switch (type) {
    case 0U: return 3650U;
    case 1U: return 4100U;
    case 2U: return 4200U;
    case 3U: return 4350U;
    case 4U: return 4400U;
    default: return 4200U;
  }
}

static uint16_t battery_empty_mv_from_type(uint8_t type)
{
  return type == 0U ? 2800U : 3200U;
}

static uint8_t estimate_battery_series_from_voltage(uint16_t vbat_mv, uint8_t battery_type)
{
  uint16_t full_mv = battery_full_mv_from_type(battery_type);
  uint8_t series;

  if (vbat_mv < (uint16_t)(full_mv * 2U - (full_mv / 2U))) {
    return 0U;
  }
  series = (uint8_t)((vbat_mv + (full_mv / 2U)) / full_mv);
  if (series < 2U) series = 2U;
  if (series > 6U) series = 6U;
  return series;
}

static uint8_t configured_battery_series(const app_t *app)
{
  if (app->battery_series >= 2U && app->battery_series <= 6U) {
    return app->battery_series;
  }
  return app->battery_series_auto;
}

static void update_auto_battery_series(app_t *app)
{
  if (app->battery_series == 0U && app->battery_series_auto == 0U) {
    app->battery_series_auto = estimate_battery_series_from_voltage(app->metrics.vbat_mv, app->set_battery_type);
  }
}

static void format_battery_series_status(const app_t *app, char *buf, uint32_t len)
{
  uint32_t pos = 0U;
  uint8_t series = configured_battery_series(app);

  pos = append_text(buf, len, pos, app->battery_series == 0U ? "AUTO " : "LOCK ");
  if (series >= 2U && series <= 6U) {
    pos = append_u32(buf, len, pos, series);
    pos = append_char(buf, len, pos, 'S');
  } else {
    pos = append_text(buf, len, pos, "--");
  }
  finish_text(buf, len, pos);
}

static void format_sleep_timeout_value(char *buf, uint32_t len, uint8_t value)
{
  if (value == SLEEP_TIMEOUT_OFF) {
    cache_copy(buf, len, "OFF");
  } else if (value == SLEEP_TIMEOUT_AUTO) {
    cache_copy(buf, len, "AUTO");
  } else {
    uint8_t seconds = (uint8_t)((value - 1U) * 5U);
    uint32_t pos = append_u32(buf, len, 0, seconds);
    pos = append_char(buf, len, pos, 's');
    finish_text(buf, len, pos);
  }
}

static void format_a10(char *buf, uint32_t len, uint8_t a10)
{
  uint32_t pos = append_u32(buf, len, 0, a10 / 10U);
  pos = append_char(buf, len, pos, '.');
  pos = append_char(buf, len, pos, (char)('0' + (a10 % 10U)));
  pos = append_char(buf, len, pos, 'A');
  finish_text(buf, len, pos);
}

static void __attribute__((unused)) format_battery_full_voltage(char *buf, uint32_t len, uint8_t code)
{
  uint16_t mv;

  if (code > BATTERY_FULL_VOLTAGE_MAX_CODE) {
    code = BATTERY_FULL_VOLTAGE_MAX_CODE;
  }
  mv = (uint16_t)(2500U + (uint16_t)code * 10U);
  format_decimal_unit(buf, len, mv / 1000U, (uint8_t)((mv % 1000U) / 10U), 2U, 'V');
}

static void format_join(char *buf, uint32_t len, const char *left, const char *right)
{
  uint32_t pos = append_text(buf, len, 0, left);
  pos = append_text(buf, len, pos, right);
  finish_text(buf, len, pos);
}

static void __attribute__((unused)) format_mv(char *buf, uint32_t len, uint16_t mv)
{
  char raw[16];
  format_decimal_unit(raw, sizeof(raw), mv / 1000U, (uint8_t)((mv % 1000U) / 10U), 2U, 'V');
  pad_text(buf, len, raw, 6U);
}

static void format_bus_mv(char *buf, uint32_t len, uint16_t mv)
{
  char raw[16];
  if (mv >= 10000U) {
    format_decimal_unit(raw, sizeof(raw), mv / 1000U, (uint8_t)((mv % 1000U) / 100U), 1U, 'V');
  } else {
    format_decimal_unit(raw, sizeof(raw), mv / 1000U, (uint8_t)((mv % 1000U) / 10U), 2U, 'V');
  }
  pad_text(buf, len, raw, 6U);
}

static void format_bus_ma(char *buf, uint32_t len, uint16_t ma)
{
  char raw[16];
  format_decimal_unit(raw, sizeof(raw), ma / 1000U, (uint8_t)((ma % 1000U) / 10U), 2U, 'A');
  pad_text(buf, len, raw, 6U);
}

static void format_power_dynamic(char *buf, uint32_t len, uint32_t mw)
{
  char raw[16];
  if (mw >= 10000UL) {
    format_decimal_unit(raw, sizeof(raw), mw / 1000UL, (uint8_t)((mw % 1000UL) / 100UL), 1U, 'W');
  } else {
    format_decimal_unit(raw, sizeof(raw), mw / 1000UL, (uint8_t)((mw % 1000UL) / 10UL), 2U, 'W');
  }
  pad_text(buf, len, raw, 6U);
}

static uint32_t display_usb_power_mw(const sw6236_metrics_t *m)
{
  if (m->vbus_mv <= DISPLAY_IDLE_VBUS_MV &&
      m->ibus_ma <= DISPLAY_IDLE_IBUS_MA &&
      m->power_mw <= DISPLAY_IDLE_POWER_MW) {
    return 0UL;
  }
  return m->power_mw;
}

static uint32_t display_battery_power_mw(const sw6236_metrics_t *m)
{
  uint32_t power_mw = ((uint32_t)m->vbat_mv * (uint32_t)m->ibat_ma) / 1000UL;

  if (m->ibat_ma <= DISPLAY_IDLE_IBUS_MA && power_mw <= DISPLAY_IDLE_POWER_MW) {
    return 0UL;
  }
  return power_mw;
}

static void format_temp_c10(char *buf, uint32_t len, int16_t temp_c10, bool valid)
{
  uint16_t abs_c10;
  char raw[16];

  if (!valid) {
    uint32_t pos = append_text(raw, sizeof(raw), 0, "--.-");
    pos = append_text(raw, sizeof(raw), pos, "\xE2\x84\x83");
    finish_text(raw, sizeof(raw), pos);
    pad_text(buf, len, raw, 8U);
    return;
  }

  abs_c10 = (uint16_t)(temp_c10 < 0 ? -temp_c10 : temp_c10);
  {
    uint32_t pos = 0;
    if (temp_c10 < 0) {
      pos = append_char(raw, sizeof(raw), pos, '-');
    }
    pos = append_u32(raw, sizeof(raw), pos, abs_c10 / 10U);
    pos = append_char(raw, sizeof(raw), pos, '.');
    pos = append_char(raw, sizeof(raw), pos, (char)('0' + (abs_c10 % 10U)));
    pos = append_text(raw, sizeof(raw), pos, "\xE2\x84\x83");
    finish_text(raw, sizeof(raw), pos);
  }
  pad_text(buf, len, raw, 8U);
}

static void format_temp_c10_compact(char *buf, uint32_t len, int16_t temp_c10, bool valid)
{
  uint16_t abs_c;
  uint32_t pos = 0;

  if (!valid) {
    pos = append_text(buf, len, pos, " --");
    pos = append_text(buf, len, pos, "\xE2\x84\x83");
    finish_text(buf, len, pos);
    return;
  }

  abs_c = (uint16_t)((temp_c10 < 0 ? -temp_c10 : temp_c10) + 5) / 10U;
  if (temp_c10 < 0) {
    pos = append_char(buf, len, pos, '-');
  } else {
    pos = append_char(buf, len, pos, ' ');
  }
  pos = append_u32(buf, len, pos, abs_c);
  pos = append_text(buf, len, pos, "\xE2\x84\x83");
  finish_text(buf, len, pos);
}

static void format_efficiency(char *buf, uint32_t len, const sw6236_metrics_t *m, uint32_t battery_power_mw)
{
  uint32_t eff_tenths;
  uint32_t pos = 0;
  uint32_t input_mw;
  uint32_t output_mw;
  bool charging;

  if (m->power_mw == 0UL || battery_power_mw == 0UL) {
    pos = append_char(buf, len, pos, '-');
    finish_text(buf, len, pos);
    return;
  }

  charging = input_active(m) && !output_active(m);
  input_mw = charging ? m->power_mw : battery_power_mw;
  output_mw = charging ? battery_power_mw : m->power_mw;
  if (input_mw == 0UL || output_mw == 0UL) {
    pos = append_char(buf, len, pos, '-');
    finish_text(buf, len, pos);
    return;
  }
  eff_tenths = (output_mw * 1000UL + (input_mw / 2UL)) / input_mw;
  if (eff_tenths > 980UL) {
    eff_tenths = 980UL;
  }
  pos = append_u32(buf, len, pos, eff_tenths / 10UL);
  pos = append_char(buf, len, pos, '.');
  pos = append_char(buf, len, pos, (char)('0' + (eff_tenths % 10UL)));
  pos = append_char(buf, len, pos, '%');
  finish_text(buf, len, pos);
}

static void format_page2_efficiency(char *buf, uint32_t len, const sw6236_metrics_t *m, uint32_t battery_power_mw)
{
  uint32_t eff;
  uint32_t pos = 0U;
  uint32_t input_mw;
  uint32_t output_mw;
  bool charging;

  if (m->power_mw == 0UL || battery_power_mw == 0UL) {
    pos = append_text(buf, len, pos, "--%");
    finish_text(buf, len, pos);
    return;
  }

  charging = input_active(m) && !output_active(m);
  input_mw = charging ? m->power_mw : battery_power_mw;
  output_mw = charging ? battery_power_mw : m->power_mw;
  if (input_mw == 0UL || output_mw == 0UL) {
    pos = append_text(buf, len, pos, "--%");
    finish_text(buf, len, pos);
    return;
  }
  eff = (output_mw * 100UL + (input_mw / 2UL)) / input_mw;
  if (eff > 98UL) {
    eff = 98UL;
  }
  pos = append_u32(buf, len, pos, eff);
  pos = append_char(buf, len, pos, '%');
  finish_text(buf, len, pos);
}

static void __attribute__((unused)) format_milli_unit_3(char *buf, uint32_t len, uint32_t milli, char unit)
{
  uint32_t pos = 0U;

  pos = append_u32(buf, len, pos, milli / 1000UL);
  pos = append_char(buf, len, pos, '.');
  pos = append_char(buf, len, pos, (char)('0' + ((milli / 100UL) % 10UL)));
  pos = append_char(buf, len, pos, (char)('0' + ((milli / 10UL) % 10UL)));
  pos = append_char(buf, len, pos, (char)('0' + (milli % 10UL)));
  pos = append_char(buf, len, pos, unit);
  finish_text(buf, len, pos);
}

static void format_page3_milli_unit(char *buf, uint32_t len, uint32_t milli, char unit)
{
  uint32_t pos = 0U;
  uint8_t digits = (milli >= 100000UL) ? 1U : ((milli >= 10000UL) ? 2U : 3U);

  pos = append_u32(buf, len, pos, milli / 1000UL);
  pos = append_char(buf, len, pos, '.');
  pos = append_char(buf, len, pos, (char)('0' + ((milli / 100UL) % 10UL)));
  if (digits >= 2U) {
    pos = append_char(buf, len, pos, (char)('0' + ((milli / 10UL) % 10UL)));
  }
  if (digits >= 3U) {
    pos = append_char(buf, len, pos, (char)('0' + (milli % 10UL)));
  }
  pos = append_char(buf, len, pos, unit);
  finish_text(buf, len, pos);
}

static void format_page3_mv(char *buf, uint32_t len, uint16_t mv)
{
  format_decimal_unit(buf, len, mv / 1000U, (uint8_t)((mv % 1000U) / 100U), 1U, 'V');
}

static void format_page3_ma(char *buf, uint32_t len, uint16_t ma)
{
  format_decimal_unit(buf, len, ma / 1000U, (uint8_t)((ma % 1000U) / 100U), 1U, 'A');
}

static void format_page3_power(char *buf, uint32_t len, uint32_t mw)
{
  if (mw >= 100000UL) {
    uint32_t pos = append_u32(buf, len, 0, (mw + 500UL) / 1000UL);
    pos = append_char(buf, len, pos, 'W');
    finish_text(buf, len, pos);
  } else {
    format_decimal_unit(buf, len, mw / 1000UL, (uint8_t)((mw % 1000UL) / 100UL), 1U, 'W');
  }
}

static void format_page3_signed_ma(char *buf, uint32_t len, uint16_t ma, bool negative)
{
  char raw[16];
  uint32_t pos = 0;

  if (negative) {
    pos = append_char(buf, len, pos, '-');
  }
  format_page3_ma(raw, sizeof(raw), ma);
  pos = append_text(buf, len, pos, raw);
  finish_text(buf, len, pos);
}

static void format_page3_signed_power(char *buf, uint32_t len, uint32_t mw, bool negative)
{
  char raw[16];
  uint32_t pos = 0;

  if (negative) {
    pos = append_char(buf, len, pos, '-');
  }
  format_page3_power(raw, sizeof(raw), mw);
  pos = append_text(buf, len, pos, raw);
  finish_text(buf, len, pos);
}

static void right_pad_text(char *buf, uint32_t len, uint8_t width)
{
  size_t used;

  if (len == 0U) {
    return;
  }
  if (width >= len) {
    width = (uint8_t)(len - 1U);
  }
  used = strlen(buf);
  if (used > width) {
    buf[width] = '\0';
    used = width;
  }
  while (used < width) {
    buf[used++] = ' ';
  }
  buf[width] = '\0';
}

static void draw_ref_text(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
  zh_font_draw_text16_ref(x, y, text, color, COLOR_BLACK);
}

static void draw_ref_text_only(uint16_t x, uint16_t y, const char *text, uint16_t color)
{
  if ((text != 0) && (text[0] != '\0')) {
    draw_ref_text(x, y, text, color);
  }
}

static void draw_ref_field(uint16_t x, uint16_t y, uint16_t w, const char *text, uint16_t color)
{
  st7735_fill_rect(x, y, w, 16, COLOR_BLACK);
  if ((text != 0) && (text[0] != '\0')) {
    draw_ref_text(x, y, text, color);
  }
}

static bool is_ascii_text(const char *text)
{
  while (*text != '\0') {
    if (((uint8_t)*text) >= 0x80U) {
      return false;
    }
    ++text;
  }
  return true;
}

static void draw_ascii_delta(uint16_t x, uint16_t y, uint16_t w, const char *old_text, const char *new_text, uint16_t color, uint8_t scale)
{
  uint8_t i;
  uint8_t old_len = (uint8_t)strlen(old_text);
  uint8_t new_len = (uint8_t)strlen(new_text);
  uint8_t char_w = (uint8_t)(6U * scale);
  uint8_t char_h = (uint8_t)(8U * scale);

  if (old_len != new_len) {
    st7735_fill_rect(x, y, w, char_h, COLOR_BLACK);
    st7735_draw_text(x, y, new_text, color, COLOR_BLACK, scale);
    return;
  }

  for (i = 0U; i < new_len; ++i) {
    if (old_text[i] != new_text[i]) {
      char one[2] = {new_text[i], '\0'};
      uint16_t draw_x = (uint16_t)(x + (uint16_t)i * char_w);
      if (new_text[i] == '\0') {
        st7735_fill_rect(draw_x, y, char_w, char_h, COLOR_BLACK);
      } else {
        st7735_draw_text(draw_x, y, one, color, COLOR_BLACK, scale);
      }
    }
  }
}

static void draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
  st7735_fill_rect(x, y, 1, 1, color);
}

static void draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if ((w < 8U) || (h < 8U)) {
    st7735_draw_rect(x, y, w, h, color);
    return;
  }

  st7735_draw_hline((uint16_t)(x + 4U), y, (uint16_t)(w - 8U), color);
  st7735_draw_hline((uint16_t)(x + 4U), (uint16_t)(y + h - 1U), (uint16_t)(w - 8U), color);
  st7735_draw_vline(x, (uint16_t)(y + 4U), (uint16_t)(h - 8U), color);
  st7735_draw_vline((uint16_t)(x + w - 1U), (uint16_t)(y + 4U), (uint16_t)(h - 8U), color);

  draw_pixel((uint16_t)(x + 2U), (uint16_t)(y + 1U), color);
  draw_pixel((uint16_t)(x + 1U), (uint16_t)(y + 2U), color);
  draw_pixel((uint16_t)(x + w - 3U), (uint16_t)(y + 1U), color);
  draw_pixel((uint16_t)(x + w - 2U), (uint16_t)(y + 2U), color);
  draw_pixel((uint16_t)(x + 2U), (uint16_t)(y + h - 2U), color);
  draw_pixel((uint16_t)(x + 1U), (uint16_t)(y + h - 3U), color);
  draw_pixel((uint16_t)(x + w - 3U), (uint16_t)(y + h - 2U), color);
  draw_pixel((uint16_t)(x + w - 2U), (uint16_t)(y + h - 3U), color);
}

static uint16_t metric_char_step(char c)
{
  return (c == '.') ? 9U : 11U;
}

static uint16_t metric_char_x(const char *text, uint8_t index)
{
  uint8_t i;
  uint16_t x = 48U;

  for (i = 0U; text[i] != '\0' && i < index; ++i) {
    x = (uint16_t)(x + metric_char_step(text[i]));
  }
  return x;
}

static void draw_metric_value_delta(uint16_t y, const char *old_value, const char *new_value, uint16_t value_color)
{
  uint8_t i;
  uint8_t old_len = (uint8_t)strlen(old_value);
  uint8_t new_len = (uint8_t)strlen(new_value);
  uint8_t max_len = old_len > new_len ? old_len : new_len;

  if (old_len != new_len) {
    st7735_fill_rect(48, y, 56, 20, COLOR_BLACK);
    old_value = "";
    old_len = 0U;
    max_len = new_len;
  }

  for (i = 0U; i < max_len; ++i) {
    char old_c = (i < old_len) ? old_value[i] : '\0';
    char new_c = (i < new_len) ? new_value[i] : '\0';
    uint16_t x = metric_char_x(new_value, i);

    if (old_c != new_c) {
      if (new_c != '\0') {
        st7735_draw_char(x, (uint16_t)(y + 1U), new_c, value_color, COLOR_BLACK, 2);
      } else {
        st7735_fill_rect(x, (uint16_t)(y + 1U), 12, 16, COLOR_BLACK);
      }
    }
  }
}

static void draw_metric_row(uint16_t y, const char *label, const char *old_value, const char *new_value, uint16_t value_color, bool full_refresh)
{
  if (full_refresh) {
    st7735_fill_rect(5, y, 99, 20, COLOR_BLACK);
    draw_ref_text(6, (uint16_t)(y + 1U), label, value_color);
  }
  draw_metric_value_delta(y, old_value, new_value, value_color);
}

static void __attribute__((unused)) draw_info_box(uint16_t y, const char *text, uint16_t color)
{
  st7735_fill_rect(108, y, 50, 16, COLOR_BLACK);
  st7735_draw_rect(108, y, 50, 16, COLOR_DARK);
  draw_ref_text(110, y, text, color);
}

static uint16_t ref_text_step(char c)
{
  return (c >= 32 && c <= 126) ? 8U : 16U;
}

static uint16_t ref_text_width(const char *text)
{
  const char *p = text;
  uint16_t width = 0U;

  while (*p != '\0') {
    uint8_t c = (uint8_t)*p;
    if (c < 0x80U) {
      width = (uint16_t)(width + ref_text_step((char)c));
      p++;
    } else {
      width = (uint16_t)(width + 16U);
      p += 3;
    }
  }
  return width;
}

static uint8_t text_next_cell(const char *text, uint8_t pos, char *cell, uint8_t cell_len)
{
  uint8_t c = (uint8_t)text[pos];
  uint8_t n;

  if (cell_len == 0U) {
    return 0U;
  }
  cell[0] = '\0';
  if (c == '\0') {
    return 0U;
  }
  n = (c < 0x80U) ? 1U : 3U;
  if (n >= cell_len) {
    n = (uint8_t)(cell_len - 1U);
  }
  for (uint8_t i = 0U; i < n && text[pos + i] != '\0'; ++i) {
    cell[i] = text[pos + i];
    cell[i + 1U] = '\0';
  }
  return n;
}

static void draw_ref_text_delta_cells(uint16_t x, uint16_t y, uint16_t clear_w,
                                      const char *old_text, const char *new_text,
                                      uint16_t color)
{
  uint8_t old_pos = 0U;
  uint8_t new_pos = 0U;
  uint16_t draw_x = x;

  while (old_text[old_pos] != '\0' || new_text[new_pos] != '\0') {
    char old_cell[4];
    char new_cell[4];
    uint8_t old_n = text_next_cell(old_text, old_pos, old_cell, sizeof(old_cell));
    uint8_t new_n = text_next_cell(new_text, new_pos, new_cell, sizeof(new_cell));
    uint16_t old_w = old_n == 0U ? 0U : ref_text_width(old_cell);
    uint16_t new_w = new_n == 0U ? 0U : ref_text_width(new_cell);
    uint16_t cell_w = old_w > new_w ? old_w : new_w;

    if (old_n != new_n || old_w != new_w || strcmp(old_cell, new_cell) != 0) {
      if (new_n != 0U && old_n == new_n && old_w == new_w) {
        draw_ref_text(draw_x, y, new_cell, color);
      } else {
        st7735_fill_rect(draw_x, y, cell_w, 16, COLOR_BLACK);
        if (new_n != 0U) {
          draw_ref_text(draw_x, y, new_cell, color);
        }
      }
    }
    draw_x = (uint16_t)(draw_x + cell_w);
    old_pos = (uint8_t)(old_pos + old_n);
    new_pos = (uint8_t)(new_pos + new_n);
    if ((uint16_t)(draw_x - x) >= clear_w) {
      break;
    }
  }
}

static uint16_t ref_text_char_x(uint16_t start_x, const char *text, uint8_t index)
{
  uint8_t i;
  uint16_t x = start_x;

  for (i = 0U; text[i] != '\0' && i < index; ++i) {
    x = (uint16_t)(x + ref_text_step(text[i]));
  }
  return x;
}

#define INFO_BOX_X       108U
#define INFO_BOX_W       50U
#define INFO_BOX_TEXT_X  109U
#define INFO_BOX_TEXT_W  48U
#define INFO_BOX_RIGHT_X 157U

static void draw_info_text_delta(uint16_t x, uint16_t y, uint16_t clear_x, uint16_t clear_w, const char *old_text, const char *new_text, uint16_t color)
{
  uint8_t i;
  uint8_t old_len = (uint8_t)strlen(old_text);
  uint8_t new_len = (uint8_t)strlen(new_text);
  uint8_t max_len = old_len > new_len ? old_len : new_len;

  if (old_len != new_len) {
    st7735_fill_rect(clear_x, y, clear_w, 16, COLOR_BLACK);
    draw_ref_text(x, y, new_text, color);
    return;
  }

  for (i = 0U; i < max_len; ++i) {
    char old_c = (i < old_len) ? old_text[i] : '\0';
    char new_c = (i < new_len) ? new_text[i] : '\0';
    uint16_t draw_x = ref_text_char_x(x, new_text, i);

    if (old_c != new_c) {
      if (new_c != '\0') {
        char one[2] = {new_c, '\0'};
        draw_ref_text(draw_x, y, one, color);
      } else {
        st7735_fill_rect(draw_x, y, ref_text_step(old_c), 16, COLOR_BLACK);
      }
    }
  }
}

static void draw_info_box_delta(uint16_t y, const char *old_text, const char *new_text, uint16_t color, bool full_refresh)
{
  if (full_refresh) {
    st7735_fill_rect(INFO_BOX_X, y, INFO_BOX_W, 16, COLOR_BLACK);
    st7735_draw_rect(INFO_BOX_X, y, INFO_BOX_W, 16, COLOR_DARK);
    draw_ref_text(INFO_BOX_TEXT_X, y, new_text, color);
    return;
  }
  draw_info_text_delta(INFO_BOX_TEXT_X, y, INFO_BOX_TEXT_X, INFO_BOX_TEXT_W, old_text, new_text, color);
}

static void draw_info_box_ascii_right(uint16_t y, const char *old_text, const char *new_text, uint16_t color, bool full_refresh)
{
  uint16_t old_w = ref_text_width(old_text);
  uint16_t new_w = ref_text_width(new_text);
  uint16_t x = INFO_BOX_RIGHT_X;
  uint16_t old_x = INFO_BOX_RIGHT_X;

  if (new_w < 42U) {
    x = (uint16_t)(INFO_BOX_RIGHT_X - new_w);
  } else {
    x = INFO_BOX_TEXT_X;
  }
  if (old_w < 42U) {
    old_x = (uint16_t)(INFO_BOX_RIGHT_X - old_w);
  } else {
    old_x = INFO_BOX_TEXT_X;
  }

  if (full_refresh || old_x != x || old_w != new_w) {
    st7735_fill_rect(INFO_BOX_X, y, INFO_BOX_W, 16, COLOR_BLACK);
    st7735_draw_rect(INFO_BOX_X, y, INFO_BOX_W, 16, COLOR_DARK);
    draw_ref_text(x, y, new_text, color);
    return;
  }
  draw_info_text_delta(x, y, INFO_BOX_TEXT_X, INFO_BOX_TEXT_W, old_text, new_text, color);
}

static const char *page1_protocol_text(const sw6236_metrics_t *m)
{
  return sw6236_fast_charge_name(m->fast_charge);
}

static void pad_text(char *dst, uint32_t dst_len, const char *src, uint8_t width)
{
  size_t len;

  if (dst_len == 0U) {
    return;
  }
  if (dst_len <= (uint32_t)width) {
    width = (uint8_t)(dst_len - 1U);
  }
  len = strlen(src);
  if (len > width) {
    len = width;
  }
  memcpy(dst, src, len);
  if (len < width) {
    memset(dst + len, ' ', (size_t)(width - len));
  }
  dst[width] = '\0';
}

static void cache_copy(char *dst, uint32_t len, const char *src)
{
  if (len == 0U) {
    return;
  }
  strncpy(dst, src, len - 1U);
  dst[len - 1U] = '\0';
}

static void __attribute__((unused)) refresh_battery_type(app_t *app)
{
  uint8_t type = app->set_battery_type;

  if (sw6236_read_battery_type(&type)) {
    if (type != app->set_battery_type) {
      app->voltage_percent_filter_valid = false;
    }
    app->set_battery_type = type;
    app->current_battery_type = type;
    app->capacity_limit_mah = sw6236_capacity_limit_mah();
  }
}

static void __attribute__((unused)) apply_saved_battery_voltage(app_t *app)
{
  uint8_t i;

  if (app->set_battery_type > 4U) {
    app->set_battery_type = 2U;
  }
  for (i = 0U; i < 5U; ++i) {
    if (sw6236_set_battery_type(app->set_battery_type)) {
      break;
    }
    delay_ms(80);
  }
  app->current_battery_type = app->set_battery_type;
}

static bool text_or_color_changed(bool full_refresh, char *cache, uint32_t len, uint16_t *cache_color, const char *text, uint16_t color)
{
  if (!full_refresh && (*cache_color == color) && (strcmp(cache, text) == 0)) {
    return false;
  }
  cache_copy(cache, len, text);
  *cache_color = color;
  return true;
}

static bool __attribute__((unused)) text_changed(bool full_refresh, char *cache, uint32_t len, const char *text)
{
  if (!full_refresh && (strcmp(cache, text) == 0)) {
    return false;
  }
  cache_copy(cache, len, text);
  return true;
}

static bool __attribute__((unused)) usb_capacity_display_changed(int32_t old_capacity_cmah, int32_t new_capacity_cmah)
{
  char old_text[16];
  char new_text[16];

  format_usb_capacity(old_text, sizeof(old_text), old_capacity_cmah);
  format_usb_capacity(new_text, sizeof(new_text), new_capacity_cmah);
  return strcmp(old_text, new_text) != 0;
}

static void draw_battery_icon_ref(uint16_t x, uint16_t y, uint8_t percent)
{
  uint16_t color = COLOR_GREEN;
  uint16_t fill;

  if (percent < 10U) {
    color = COLOR_RED;
  } else if (percent < 30U) {
    color = COLOR_ORANGE_R;
  } else if (percent < 50U) {
    color = COLOR_ORANGE;
  } else if (percent < 70U) {
    color = COLOR_ORANGE_Y;
  } else if (percent < 80U) {
    color = COLOR_YELLOW;
  }

  st7735_fill_rect(x, y, 32, 16, COLOR_BLACK);
  st7735_draw_rect(x, y + 3U, 27, 11, COLOR_WHITE);
  st7735_fill_rect(x + 27U, y + 6U, 3, 5, COLOR_WHITE);
  fill = (uint16_t)((uint16_t)percent * 23U / 100U);
  st7735_fill_rect(x + 2U, y + 5U, fill, 7, color);
}

static uint16_t __attribute__((unused)) battery_voltage_color(uint16_t mv)
{
  if (mv < 3200U) return COLOR_RED;
  if (mv < 3400U) return COLOR_ORANGE_R;
  if (mv < 3600U) return COLOR_ORANGE;
  if (mv < 3800U) return COLOR_ORANGE_Y;
  if (mv < 4000U) return COLOR_YELLOW;
  return COLOR_GREEN;
}

static uint16_t temp_color(int16_t temp_c10)
{
  if (temp_c10 < 50 || temp_c10 > 450) return COLOR_RED;
  if (temp_c10 <= 100 || temp_c10 >= 400) return COLOR_ORANGE;
  return COLOR_GREEN;
}

static uint8_t interpolate_percent(uint16_t cell_mv,
                                   const uint16_t *mv_table,
                                   const uint8_t *percent_table,
                                   uint8_t count)
{
  uint8_t i;
  uint16_t mv0;
  uint16_t mv1;
  uint8_t p0;
  uint8_t p1;
  uint32_t scaled;

  if (cell_mv <= mv_table[0]) {
    return percent_table[0];
  }
  for (i = 1U; i < count; ++i) {
    if (cell_mv <= mv_table[i]) {
      mv0 = mv_table[i - 1U];
      mv1 = mv_table[i];
      p0 = percent_table[i - 1U];
      p1 = percent_table[i];
      if (mv1 <= mv0) {
        return p1;
      }
      scaled = (uint32_t)(cell_mv - mv0) * (uint32_t)(p1 - p0);
      return (uint8_t)(p0 + ((scaled + ((mv1 - mv0) / 2U)) / (mv1 - mv0)));
    }
  }
  return percent_table[count - 1U];
}

static uint8_t estimate_percent_from_cell_mv(uint16_t cell_mv, uint8_t battery_type)
{
  static const uint8_t pct_table[] = {0U, 10U, 20U, 40U, 60U, 80U, 92U, 100U};
  static const uint16_t lifepo_mv[] = {2800U, 3000U, 3150U, 3250U, 3300U, 3400U, 3550U, 3650U};
  static const uint16_t li410_mv[] = {3200U, 3450U, 3600U, 3700U, 3820U, 3920U, 4020U, 4100U};
  static const uint16_t li420_mv[] = {3200U, 3500U, 3650U, 3750U, 3850U, 3950U, 4050U, 4200U};
  static const uint16_t li435_mv[] = {3200U, 3500U, 3650U, 3800U, 3950U, 4100U, 4250U, 4350U};
  static const uint16_t li440_mv[] = {3200U, 3500U, 3650U, 3800U, 3960U, 4140U, 4300U, 4400U};
  const uint16_t *mv_table = li420_mv;
  uint8_t percent;

  switch (battery_type) {
    case 0U: mv_table = lifepo_mv; break;
    case 1U: mv_table = li410_mv; break;
    case 2U: mv_table = li420_mv; break;
    case 3U: mv_table = li435_mv; break;
    case 4U: mv_table = li440_mv; break;
    default: mv_table = li420_mv; break;
  }

  percent = interpolate_percent(cell_mv, mv_table, pct_table, (uint8_t)(sizeof(pct_table) / sizeof(pct_table[0])));
  return percent > 100U ? 100U : percent;
}

static uint8_t estimate_percent_from_vbat(uint16_t vbat_mv, uint8_t battery_type, uint8_t cells)
{
  uint16_t cell_mv;

  if (vbat_mv == 0U) {
    return 0U;
  }
  if (cells == 0U) {
    return 0U;
  }
  cell_mv = (uint16_t)(vbat_mv / cells);
  if (cell_mv >= battery_full_mv_from_type(battery_type)) {
    return 100U;
  }
  return estimate_percent_from_cell_mv(cell_mv, battery_type);
}

static uint16_t estimate_cell_mv(uint16_t vbat_mv, uint8_t cells)
{
  if (cells == 0U) {
    return 0U;
  }
  return (uint16_t)(vbat_mv / cells);
}

static bool battery_full_detected(const sw6236_metrics_t *m)
{
  return (m->mode_status & 0x10U) != 0U;
}

static bool battery_empty_detected(const app_t *app, const sw6236_metrics_t *m)
{
  return output_active(m) && estimate_cell_mv(m->vbat_mv, configured_battery_series(app)) <= battery_empty_mv_from_type(app->set_battery_type);
}

static int32_t signed_battery_current_ma(const sw6236_metrics_t *m)
{
  if (input_active(m) == output_active(m)) {
    return 0;
  }
  return input_active(m) ? (int32_t)m->ibat_ma : -(int32_t)m->ibat_ma;
}

static void battery_gauge_set_percent(app_t *app, uint8_t percent)
{
  if (percent > 100U) percent = 100U;
  app->gauge_remaining_cmah = ((uint32_t)percent * app->gauge_capacity_cmah + 50UL) / 100UL;
  app->gauge_rem_mams = 0;
}

static uint8_t battery_gauge_percent(const app_t *app)
{
  if (app->gauge_capacity_cmah == 0U) {
    return 0U;
  }
  return (uint8_t)((app->gauge_remaining_cmah * 100UL + (app->gauge_capacity_cmah / 2UL)) / app->gauge_capacity_cmah);
}

static uint8_t battery_voltage_percent_filtered(app_t *app)
{
  uint16_t target_q8 = (uint16_t)estimate_percent_from_vbat(app->metrics.vbat_mv, app->set_battery_type, configured_battery_series(app)) << 8;

  if (!app->voltage_percent_filter_valid) {
    app->voltage_percent_q8 = target_q8;
    app->voltage_percent_filter_valid = true;
  } else {
    int32_t diff = (int32_t)target_q8 - (int32_t)app->voltage_percent_q8;
    app->voltage_percent_q8 = (uint16_t)((int32_t)app->voltage_percent_q8 + (diff >> BATTERY_VOLTAGE_FILTER_ALPHA_SHIFT));
  }
  return (uint8_t)((app->voltage_percent_q8 + 128U) >> 8);
}

static void battery_apply_display_percent(app_t *app)
{
  if (app->battery_percent_mode == BATTERY_PERCENT_MODE_VOLTAGE) {
    app->metrics.percent = battery_voltage_percent_filtered(app);
  } else {
    (void)battery_voltage_percent_filtered(app);
    app->metrics.percent = battery_gauge_percent(app);
  }
  if (app->metrics.percent > 100U) {
    app->metrics.percent = 100U;
  }
}

static void battery_gauge_init(app_t *app, const app_settings_t *settings)
{
  uint32_t cap_mah = settings->gauge_capacity_mah;
  uint32_t rem_mah = settings->gauge_remaining_mah;
  uint8_t initial_percent;

  if (cap_mah < 1000UL || cap_mah > 250000UL) {
    cap_mah = app->set_capacity_mah;
  }
  if (cap_mah < 1000UL) {
    cap_mah = DEFAULT_BATTERY_CAPACITY_MAH;
  }
  if (rem_mah > cap_mah) {
    rem_mah = cap_mah;
  }

  app->gauge_capacity_cmah = cap_mah * 100UL;
  app->capacity_learned = settings->gauge_learned != 0U;
  if (settings->gauge_learned != 0U || rem_mah != 0U) {
    app->gauge_remaining_cmah = rem_mah * 100UL;
  } else {
    initial_percent = estimate_percent_from_vbat(app->metrics.vbat_mv, app->set_battery_type, configured_battery_series(app));
    app->gauge_remaining_cmah = ((uint32_t)initial_percent * app->gauge_capacity_cmah + 50UL) / 100UL;
  }

  app->gauge_rem_mams = 0;
  app->gauge_cycle_cmah = 0U;
  app->gauge_was_full = battery_full_detected(&app->metrics);
  app->gauge_was_empty = battery_empty_detected(app, &app->metrics);
  app->gauge_initialized = true;
  app->capacity_max_mah = (app->gauge_capacity_cmah + 50UL) / 100UL;
  app->capacity_current_mah = (app->gauge_remaining_cmah + 50UL) / 100UL;
  app->voltage_percent_filter_valid = false;
  battery_apply_display_percent(app);
}

static void battery_gauge_update(app_t *app, const sw6236_metrics_t *old_m, const sw6236_metrics_t *new_m, uint32_t elapsed_ms)
{
  int32_t old_ma;
  int32_t new_ma;
  int32_t avg_ma;
  int32_t delta_mams;
  int32_t delta_cmah;
  int32_t remaining;
  bool full_now;
  bool empty_now;

  if (!app->gauge_initialized || elapsed_ms == 0U || app->gauge_capacity_cmah == 0U) {
    return;
  }

  old_ma = signed_battery_current_ma(old_m);
  new_ma = signed_battery_current_ma(new_m);
  avg_ma = (old_ma + new_ma) / 2;
  delta_mams = avg_ma * (int32_t)elapsed_ms;
  delta_cmah = (app->gauge_rem_mams + delta_mams) / BATTERY_MAMS_PER_CMAH;
  app->gauge_rem_mams = (app->gauge_rem_mams + delta_mams) % BATTERY_MAMS_PER_CMAH;

  if (delta_cmah != 0) {
    remaining = (int32_t)app->gauge_remaining_cmah + delta_cmah;
    if (remaining < 0) remaining = 0;
    if ((uint32_t)remaining > app->gauge_capacity_cmah) remaining = (int32_t)app->gauge_capacity_cmah;
    app->gauge_remaining_cmah = (uint32_t)remaining;
    app->gauge_cycle_cmah += (uint32_t)(delta_cmah < 0 ? -delta_cmah : delta_cmah);
  }

  full_now = battery_full_detected(new_m);
  empty_now = battery_empty_detected(app, new_m);
  if (full_now) {
    if (app->gauge_was_empty && app->gauge_cycle_cmah > (app->gauge_capacity_cmah / 2UL)) {
      app->gauge_capacity_cmah = app->gauge_cycle_cmah;
      app->capacity_learned = true;
    }
    battery_gauge_set_percent(app, 100U);
    app->gauge_cycle_cmah = 0U;
    app->gauge_was_full = true;
    app->gauge_was_empty = false;
    save_settings(app);
  } else if (empty_now) {
    if (app->gauge_was_full && app->gauge_cycle_cmah > (app->gauge_capacity_cmah / 2UL)) {
      app->gauge_capacity_cmah = app->gauge_cycle_cmah;
      app->capacity_learned = true;
    }
    battery_gauge_set_percent(app, 0U);
    app->gauge_cycle_cmah = 0U;
    app->gauge_was_empty = true;
    app->gauge_was_full = false;
    save_settings(app);
  }

  app->capacity_max_mah = (app->gauge_capacity_cmah + 50UL) / 100UL;
  app->capacity_current_mah = (app->gauge_remaining_cmah + 50UL) / 100UL;
  battery_apply_display_percent(app);
  if ((millis() - app->last_gauge_save_ms) >= BATTERY_GAUGE_SAVE_INTERVAL_MS) {
    app->last_gauge_save_ms = millis();
    save_settings(app);
  }
}

static bool input_active(const sw6236_metrics_t *m)
{
  return (m->port_online & 0x05U) != 0U;
}

static bool output_active(const sw6236_metrics_t *m)
{
  return (m->port_online & 0x3AU) != 0U;
}

static bool fan_should_run(const app_t *app, int16_t temp_c10, uint32_t now)
{
  int16_t on_c10 = (int16_t)app->fan_on_temp_c * 10;
  int16_t off_c10 = (int16_t)app->fan_off_temp_c * 10;

  if (!app->fan_on) {
    return temp_c10 >= on_c10;
  }
  if (app->fan_on_temp_c < app->fan_off_temp_c) {
    return true;
  }
  if ((now - app->fan_started_ms) < 10000UL) {
    return true;
  }
  return temp_c10 > off_c10;
}

static int32_t signed_transfer_current_ma(const sw6236_metrics_t *m)
{
  bool in = input_active(m);
  bool out = output_active(m);

  if (in == out) {
    return 0;
  }
  return out ? (int32_t)m->ibus_ma : -(int32_t)m->ibus_ma;
}

typedef struct {
  const char *text;
  uint16_t color;
} status_item_t;

static uint8_t build_status_items(const app_t *app, const sw6236_metrics_t *m, status_item_t *items, uint8_t max_items)
{
  uint8_t count = 0U;

#define PUSH_STATUS(_text, _color) \
  do { \
    if (count < max_items) { \
      items[count].text = (_text); \
      items[count].color = (_color); \
      count++; \
    } \
  } while (0)

  if (m->powered && !m->online) PUSH_STATUS(tr(app, "Chip Offline", "芯片失联"), COLOR_RED);
  else if (m->suspect_damage) PUSH_STATUS(tr(app, "Chip Damaged", "芯片损毁"), COLOR_RED);
  if (m->system_status & 0x40U) PUSH_STATUS(tr(app, "Input OVP", "输入过压"), COLOR_RED);
  if (m->discharge_error & 0x08U) PUSH_STATUS(tr(app, "Vsys OC", "Vsys输出过流"), COLOR_RED);
  if (m->discharge_error & 0x04U) PUSH_STATUS(tr(app, "Vsys Short", "Vsys输出短路"), COLOR_RED);
  if (m->chip_temp_c10 >= 600) PUSH_STATUS(tr(app, "Board OT", "主板过温"), COLOR_RED);

#undef PUSH_STATUS

  if (count == 0U) {
    if (input_active(m)) {
      items[0].text = tr(app, "Input OK", "输入正常");
      items[0].color = COLOR_GREEN;
      return 1U;
    }
    if (output_active(m)) {
      items[0].text = tr(app, "Output OK", "输出正常");
      items[0].color = COLOR_GREEN;
      return 1U;
    }
    switch (m->mode_status & 0x07U) {
      case 0x01U:
        items[0].text = tr(app, "Trickle", "涓流充电");
        items[0].color = COLOR_YELLOW;
        break;
      case 0x02U:
        items[0].text = tr(app, "CC Charge", "恒流充电");
        items[0].color = COLOR_GREEN;
        break;
      case 0x03U:
        items[0].text = tr(app, "CV Charge", "恒压充电");
        items[0].color = COLOR_GREEN;
        break;
      case 0x04U:
        items[0].text = tr(app, "Wait", "充电等待");
        items[0].color = COLOR_YELLOW;
        break;
      case 0x05U:
        items[0].text = tr(app, "Full", "电池充满");
        items[0].color = COLOR_GREEN;
        break;
      case 0x06U:
        items[0].text = tr(app, "Timeout", "充电超时");
        items[0].color = COLOR_RED;
        break;
      default:
        items[0].text = tr(app, "Standby", "待机");
        items[0].color = COLOR_GRAY;
        break;
    }
    return 1U;
  }

  return count;
}

static const char *ref_port_text(const app_t *app, const sw6236_metrics_t *m)
{
  if (m->port_online & 0x20U) return tr(app, "USB-A1 OUT", "USB-A1输出");
  if (m->port_online & 0x10U) return tr(app, "USB-A2 OUT", "USB-A2输出");
  if (m->port_online & 0x08U) return tr(app, "USB-C1 OUT", "USB-C1输出");
  if (m->port_online & 0x04U) return tr(app, "USB-C1 IN", "USB-C1输入");
  if (m->port_online & 0x02U) return tr(app, "USB-C2 OUT", "USB-C2输出");
  if (m->port_online & 0x01U) return tr(app, "USB-C2 IN", "USB-C2输入");
  return tr(app, "No Output", "无输出");
}

static const char *ref_protocol_text(const app_t *app, const sw6236_metrics_t *m)
{
  const char *name = sw6236_fast_charge_name(m->fast_charge);
  if ((m->fast_charge & 0x0FU) == 0U) {
    return tr(app, "No Fast Chg", "非快充");
  }
  return name;
}

static void __attribute__((unused)) integrate_usb_capacity(app_t *app, const sw6236_metrics_t *old_m, const sw6236_metrics_t *new_m, uint32_t elapsed_ms)
{
  int32_t old_ma;
  int32_t new_ma;
  int32_t avg_ma;
  int32_t delta_mams;
  int32_t total_mams;

  if (elapsed_ms == 0U) {
    return;
  }

  old_ma = signed_transfer_current_ma(old_m);
  new_ma = signed_transfer_current_ma(new_m);
  if (old_ma == new_ma) {
    avg_ma = old_ma;
  } else {
    int32_t sum = old_ma + new_ma;
    if (sum >= 0) {
      avg_ma = (sum + 1) / 2;
    } else {
      avg_ma = (sum - 1) / 2;
    }
  }

  delta_mams = avg_ma * (int32_t)elapsed_ms;
  total_mams = app->usb_capacity_rem_mams + delta_mams;
  app->usb_capacity_cmah += total_mams / USB_CAPACITY_MAMS_PER_CMAH;
  app->usb_capacity_rem_mams = total_mams % USB_CAPACITY_MAMS_PER_CMAH;
  if (app->usb_capacity_cmah > USB_CAPACITY_POSITIVE_MAX_CMAH ||
      app->usb_capacity_cmah < -USB_CAPACITY_NEGATIVE_MAX_CMAH) {
    app->usb_capacity_cmah = 0;
    app->usb_capacity_rem_mams = 0;
  }
}

static void format_usb_capacity(char *buf, uint32_t len, int32_t capacity_cmah)
{
  bool negative = capacity_cmah < 0;
  uint32_t abs_cmah = (uint32_t)(negative ? -capacity_cmah : capacity_cmah);
  uint32_t abs_mah = abs_cmah / 100U;
  uint32_t pos = 0;

  if (negative) {
    pos = append_char(buf, len, pos, '-');
  }

  if (abs_mah < 1000U) {
    pos = append_u32(buf, len, pos, abs_mah);
    pos = append_text(buf, len, pos, "mAh");
    finish_text(buf, len, pos);
    return;
  }

  pos = append_u32(buf, len, pos, abs_cmah / 100000U);
  pos = append_char(buf, len, pos, '.');
  pos = append_u8_2(buf, len, pos, (uint8_t)((abs_cmah % 100000U) / 1000U));
  pos = append_text(buf, len, pos, "Ah");
  finish_text(buf, len, pos);
}

static void render_page_header(app_t *app, const sw6236_metrics_t *m, bool full_refresh)
{
  char buf[32];
  uint16_t color;
  bool temp_valid;
  bool chip_temp_valid;
  static char cache_chip_temp[32];
  static char cache_temp[32];
  static char cache_percent[16];
  static uint16_t color_chip_temp = 0xFFFFU;
  static uint16_t color_temp = 0xFFFFU;
  static uint16_t color_percent = 0xFFFFU;
  static uint8_t battery_percent = 0xFFU;

  if (full_refresh) {
    cache_chip_temp[0] = '\0';
    cache_temp[0] = '\0';
    cache_percent[0] = '\0';
    color_chip_temp = color_temp = color_percent = 0xFFFFU;
    battery_percent = 0xFFU;
  }

  chip_temp_valid = m->online && m->chip_temp_c10 >= -400 && m->chip_temp_c10 <= 1500;
  format_temp_c10_compact(buf, sizeof(buf), m->chip_temp_c10, chip_temp_valid);
  color = temp_color(m->chip_temp_c10);
  if (text_or_color_changed(full_refresh, cache_chip_temp, sizeof(cache_chip_temp), &color_chip_temp, buf, color)) {
    st7735_fill_rect(0, 0, 48, 16, COLOR_BLACK);
    draw_ref_text_only(4, 0, buf, color);
  }

  temp_valid = m->online && m->ntc_temp_c10 >= -400 && m->ntc_temp_c10 <= 1500;
  format_temp_c10_compact(buf, sizeof(buf), m->ntc_temp_c10, temp_valid);
  color = temp_color(m->ntc_temp_c10);
  if (text_or_color_changed(full_refresh, cache_temp, sizeof(cache_temp), &color_temp, buf, color)) {
    st7735_fill_rect(48, 0, 48, 16, COLOR_BLACK);
    draw_ref_text_only(52, 0, buf, color);
  }

  if (full_refresh || battery_percent != m->percent) {
    draw_battery_icon_ref(96, 0, m->percent);
    battery_percent = m->percent;
  }
  {
    uint32_t pos = 0;
    if (m->percent < 100U) {
      pos = append_char(buf, sizeof(buf), pos, ' ');
      if (m->percent < 10U) {
        pos = append_char(buf, sizeof(buf), pos, ' ');
      }
    }
    pos = append_u32(buf, sizeof(buf), pos, m->percent);
    pos = append_char(buf, sizeof(buf), pos, '%');
    finish_text(buf, sizeof(buf), pos);
  }
  if (text_or_color_changed(full_refresh, cache_percent, sizeof(cache_percent), &color_percent, buf, COLOR_WHITE)) {
    draw_ref_text_only(128, 0, buf, COLOR_WHITE);
  }
}

static void __attribute__((unused)) render_page1_old(app_t *app, bool full_refresh)
{
  char buf[32];
  char right[32];
  status_item_t status_items[16];
  uint16_t status_color;
  uint8_t status_count;
  uint8_t status_index;
  const char *status_text;
  const sw6236_metrics_t *m = &app->metrics;
  static char cache_port[32];
  static char cache_protocol[32];
  static char cache_vbus[32];
  static char cache_ibus[32];
  static char cache_power[32];
  static char cache_energy[32];
  static char cache_status[32];
  static uint16_t color_port = 0xFFFFU;
  static uint16_t color_protocol = 0xFFFFU;
  static uint16_t color_vbus = 0xFFFFU;
  static uint16_t color_ibus = 0xFFFFU;
  static uint16_t color_power = 0xFFFFU;
  static uint16_t color_energy = 0xFFFFU;
  static uint16_t color_status = 0xFFFFU;

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    cache_port[0] = '\0';
    cache_protocol[0] = '\0';
    cache_vbus[0] = '\0';
    cache_ibus[0] = '\0';
    cache_power[0] = '\0';
    cache_energy[0] = '\0';
    cache_status[0] = '\0';
    color_port = color_protocol = 0xFFFFU;
    color_vbus = color_ibus = color_power = color_energy = color_status = 0xFFFFU;
  }
  render_page_header(app, m, full_refresh);

  if (text_or_color_changed(full_refresh, cache_port, sizeof(cache_port), &color_port, ref_port_text(app, m), COLOR_WHITE)) {
    draw_ref_field(0, 16, 88, ref_port_text(app, m), COLOR_WHITE);
  }
  if (text_or_color_changed(full_refresh, cache_protocol, sizeof(cache_protocol), &color_protocol, ref_protocol_text(app, m), COLOR_GREEN)) {
    draw_ref_field(88, 16, 72, ref_protocol_text(app, m), COLOR_GREEN);
  }

  format_bus_mv(right, sizeof(right), m->vbus_mv);
  format_join(buf, sizeof(buf), tr(app, "Volt ", "电压"), right);
  if (text_or_color_changed(full_refresh, cache_vbus, sizeof(cache_vbus), &color_vbus, buf, COLOR_YELLOW)) {
    draw_ref_text_only(0, 32, buf, COLOR_YELLOW);
  }

  format_bus_ma(right, sizeof(right), m->ibus_ma);
  format_join(buf, sizeof(buf), tr(app, "Curr ", "电流"), right);
  if (text_or_color_changed(full_refresh, cache_ibus, sizeof(cache_ibus), &color_ibus, buf, COLOR_WHITE)) {
    draw_ref_text_only(88, 32, buf, COLOR_WHITE);
  }

  format_power_dynamic(right, sizeof(right), display_usb_power_mw(m));
  format_join(buf, sizeof(buf), tr(app, "Power", "功率"), right);
  if (text_or_color_changed(full_refresh, cache_power, sizeof(cache_power), &color_power, buf, COLOR_PURPLE)) {
    draw_ref_text_only(0, 48, buf, COLOR_PURPLE);
  }

  format_usb_capacity(right, sizeof(right), app->usb_capacity_cmah);
  if (text_or_color_changed(full_refresh, cache_energy, sizeof(cache_energy), &color_energy, right, COLOR_GREEN)) {
    draw_ref_text_only(88, 48, right, COLOR_GREEN);
  }

  status_count = build_status_items(app, m, status_items, 16U);
  status_index = (status_count == 0U) ? 0U : (uint8_t)(app->status_rotate_index % status_count);
  status_text = status_items[status_index].text;
  status_color = status_items[status_index].color;

  if (text_or_color_changed(full_refresh, cache_status, sizeof(cache_status), &color_status, status_text, status_color)) {
    draw_ref_field(0, 64, 160, status_text, status_color);
  }
}

static void __attribute__((unused)) render_page1(app_t *app, bool full_refresh)
{
  char line[24];
  char chip_temp[16];
  char board_temp[16];
  char percent[8];
  bool chip_temp_valid;
  bool board_temp_valid;
  uint16_t percent_color;
  const sw6236_metrics_t *m = &app->metrics;
  static char cache_protocol[32];
  static char cache_vbus[32];
  static char cache_ibus[32];
  static char cache_power[32];
  static char cache_chip_temp[16];
  static char cache_board_temp[16];
  static char cache_percent[8];
  static uint16_t color_protocol = 0xFFFFU;
  static uint16_t color_vbus = 0xFFFFU;
  static uint16_t color_ibus = 0xFFFFU;
  static uint16_t color_power = 0xFFFFU;
  static uint16_t color_chip_temp = 0xFFFFU;
  static uint16_t color_board_temp = 0xFFFFU;
  static uint16_t color_percent = 0xFFFFU;

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    cache_protocol[0] = '\0';
    cache_vbus[0] = '\0';
    cache_ibus[0] = '\0';
    cache_power[0] = '\0';
    cache_chip_temp[0] = '\0';
    cache_board_temp[0] = '\0';
    cache_percent[0] = '\0';
    color_protocol = color_vbus = color_ibus = color_power = 0xFFFFU;
    color_chip_temp = color_board_temp = color_percent = 0xFFFFU;
    draw_round_rect(0, 0, 160, 80, COLOR_DARK);
    st7735_draw_vline(106, 5, 70, COLOR_DARK);
  }

  if (m->vbus_mv >= 10000U) {
    format_decimal_unit(line, sizeof(line), m->vbus_mv / 1000U, (uint8_t)((m->vbus_mv % 1000U) / 100U), 1U, 'V');
  } else {
    format_decimal_unit(line, sizeof(line), m->vbus_mv / 1000U, (uint8_t)((m->vbus_mv % 1000U) / 10U), 2U, 'V');
  }
  if (full_refresh || strcmp(cache_vbus, line) != 0 || color_vbus != COLOR_CYAN) {
    draw_metric_row(8, tr(app, "Volt:", "电压:"), cache_vbus, line, COLOR_CYAN, full_refresh);
    cache_copy(cache_vbus, sizeof(cache_vbus), line);
    color_vbus = COLOR_CYAN;
  }

  if (m->ibus_ma >= 1000U) {
    format_decimal_unit(line, sizeof(line), m->ibus_ma / 1000U, (uint8_t)((m->ibus_ma % 1000U) / 100U), 1U, 'A');
  } else {
    format_decimal_unit(line, sizeof(line), m->ibus_ma / 1000U, (uint8_t)((m->ibus_ma % 1000U) / 10U), 2U, 'A');
  }
  if (full_refresh || strcmp(cache_ibus, line) != 0 || color_ibus != COLOR_GREEN) {
    draw_metric_row(32, tr(app, "Curr:", "电流:"), cache_ibus, line, COLOR_GREEN, full_refresh);
    cache_copy(cache_ibus, sizeof(cache_ibus), line);
    color_ibus = COLOR_GREEN;
  }

  if (display_usb_power_mw(m) >= 10000UL) {
    uint32_t power_mw = display_usb_power_mw(m);
    format_decimal_unit(line, sizeof(line), power_mw / 1000UL, (uint8_t)((power_mw % 1000UL) / 100UL), 1U, 'W');
  } else {
    uint32_t power_mw = display_usb_power_mw(m);
    format_decimal_unit(line, sizeof(line), power_mw / 1000UL, (uint8_t)((power_mw % 1000UL) / 10UL), 2U, 'W');
  }
  if (full_refresh || strcmp(cache_power, line) != 0 || color_power != COLOR_ORANGE) {
    draw_metric_row(56, tr(app, "Power:", "功率:"), cache_power, line, COLOR_ORANGE, full_refresh);
    cache_copy(cache_power, sizeof(cache_power), line);
    color_power = COLOR_ORANGE;
  }

  if (full_refresh || strcmp(cache_protocol, page1_protocol_text(m)) != 0 || color_protocol != COLOR_YELLOW) {
    draw_info_box_delta(5, cache_protocol, page1_protocol_text(m), COLOR_YELLOW, full_refresh || color_protocol != COLOR_YELLOW);
    cache_copy(cache_protocol, sizeof(cache_protocol), page1_protocol_text(m));
    color_protocol = COLOR_YELLOW;
  }

  chip_temp_valid = m->online && m->chip_temp_c10 >= -400 && m->chip_temp_c10 <= 1500;
  format_temp_c10(chip_temp, sizeof(chip_temp), m->chip_temp_c10, chip_temp_valid);
  if (full_refresh || strcmp(cache_chip_temp, chip_temp) != 0 || color_chip_temp != temp_color(m->chip_temp_c10)) {
    draw_info_box_delta(23, cache_chip_temp, chip_temp, temp_color(m->chip_temp_c10), full_refresh || color_chip_temp != temp_color(m->chip_temp_c10));
    cache_copy(cache_chip_temp, sizeof(cache_chip_temp), chip_temp);
    color_chip_temp = temp_color(m->chip_temp_c10);
  }

  board_temp_valid = m->ntc_temp_c10 >= -400 && m->ntc_temp_c10 <= 1500;
  format_temp_c10(board_temp, sizeof(board_temp), m->ntc_temp_c10, board_temp_valid);
  if (full_refresh || strcmp(cache_board_temp, board_temp) != 0 || color_board_temp != temp_color(m->ntc_temp_c10)) {
    draw_info_box_delta(41, cache_board_temp, board_temp, temp_color(m->ntc_temp_c10), full_refresh || color_board_temp != temp_color(m->ntc_temp_c10));
    cache_copy(cache_board_temp, sizeof(cache_board_temp), board_temp);
    color_board_temp = temp_color(m->ntc_temp_c10);
  }

  if (m->percent < 10U) {
    percent_color = COLOR_RED;
  } else if (m->percent < 30U) {
    percent_color = COLOR_ORANGE;
  } else {
    percent_color = COLOR_GREEN;
  }
  {
    uint32_t pos = append_u32(percent, sizeof(percent), 0, m->percent);
    pos = append_char(percent, sizeof(percent), pos, '%');
    finish_text(percent, sizeof(percent), pos);
  }
  if (full_refresh || strcmp(cache_percent, percent) != 0 || color_percent != percent_color) {
    draw_info_box_ascii_right(59, cache_percent, percent, percent_color, full_refresh || color_percent != percent_color);
    cache_copy(cache_percent, sizeof(cache_percent), percent);
    color_percent = percent_color;
  }
}

static void format_temp_ascii_147(char *buf, uint32_t len, int16_t temp_c10, bool valid)
{
  uint16_t abs_c10;
  char raw[16];
  uint32_t pos = 0;

  if (!valid) {
    pos = append_text(buf, len, pos, "--.-");
    pos = append_text(buf, len, pos, "\xE2\x84\x83");
    finish_text(buf, len, pos);
    return;
  }

  abs_c10 = (uint16_t)(temp_c10 < 0 ? -temp_c10 : temp_c10);
  if (temp_c10 < 0) {
    pos = append_char(raw, sizeof(raw), pos, '-');
  }
  pos = append_u32(raw, sizeof(raw), pos, abs_c10 / 10U);
  pos = append_char(raw, sizeof(raw), pos, '.');
  pos = append_char(raw, sizeof(raw), pos, (char)('0' + (abs_c10 % 10U)));
  pos = append_text(raw, sizeof(raw), pos, "\xE2\x84\x83");
  finish_text(raw, sizeof(raw), pos);
  cache_copy(buf, len, raw);
}

static void format_percent_147(char *buf, uint32_t len, uint8_t percent)
{
  uint32_t pos = 0;

  if (len < 5U) {
    return;
  }
  if (percent >= 100U) {
    pos = append_text(buf, len, pos, "100%");
  } else if (percent >= 10U) {
    pos = append_char(buf, len, pos, ' ');
    pos = append_u32(buf, len, pos, percent);
    pos = append_char(buf, len, pos, '%');
  } else {
    pos = append_text(buf, len, pos, "  ");
    pos = append_u32(buf, len, pos, percent);
    pos = append_char(buf, len, pos, '%');
  }
  finish_text(buf, len, pos);
}

static const char *ip2366_charge_state_text_147(const app_t *app, uint8_t mode_status)
{
  if (app->language == LANG_ENGLISH) {
    switch (mode_status & 0x07U) {
      case 0x00U: return "Standby";
      case 0x01U: return "Trickle";
      case 0x02U: return "CC";
      case 0x03U: return "CV";
      case 0x04U: return "Wait";
      case 0x05U: return "Full";
      case 0x06U: return "Timeout";
      default: return "Standby";
    }
  }
  switch (mode_status & 0x07U) {
    case 0x00U: return tr(app, "Standby", "待机");
    case 0x01U: return tr(app, "Trickle", "涓流充电");
    case 0x02U: return tr(app, "CC Charge", "恒流充电");
    case 0x03U: return tr(app, "CV Charge", "恒压充电");
    case 0x04U: return tr(app, "Wait", "充电等待");
    case 0x05U: return tr(app, "Full", "电池充满");
    case 0x06U: return tr(app, "Timeout", "充电超时");
    default: return tr(app, "Standby", "待机");
  }
}

static uint16_t ip2366_charge_state_color_147(uint8_t mode_status)
{
  switch (mode_status & 0x07U) {
    case 0x00U: return COLOR_GRAY;
    case 0x01U:
    case 0x04U: return COLOR_YELLOW;
    case 0x06U: return COLOR_RED;
    default: return COLOR_GREEN;
  }
}

static const char *ip2366_usb_state_text_147(const app_t *app, const sw6236_metrics_t *m)
{
  if (app->language == LANG_ENGLISH) {
    if (input_active(m)) return "IN ";
    if (output_active(m)) return "OUT ";
    return "";
  }
  if (input_active(m)) return tr(app, "Input ", "输入");
  if (output_active(m)) return tr(app, "Output ", "输出");
  return "";
}

static void status_text_147(const app_t *app, const sw6236_metrics_t *m, char *buf, uint32_t len, uint16_t *color)
{
  const char *usb_state;
  const char *charge_state;
  uint32_t pos = 0U;

  if (m->powered && !m->online) {
    *color = COLOR_RED;
    cache_copy(buf, len, tr(app, "Offline", "失联"));
    return;
  }
  if (m->system_status & 0x40U) {
    *color = COLOR_RED;
    cache_copy(buf, len, tr(app, "Input OVP", "输入过压"));
    return;
  }
  if (m->discharge_error & 0x08U) {
    *color = COLOR_RED;
    cache_copy(buf, len, tr(app, "Vsys OC", "Vsys输出过流"));
    return;
  }
  if (m->discharge_error & 0x04U) {
    *color = COLOR_RED;
    cache_copy(buf, len, tr(app, "Vsys Short", "Vsys输出短路"));
    return;
  }
  if (m->chip_temp_c10 >= 600) {
    *color = COLOR_RED;
    cache_copy(buf, len, tr(app, "Board OT", "主板过温"));
    return;
  }

  usb_state = ip2366_usb_state_text_147(app, m);
  charge_state = ip2366_charge_state_text_147(app, m->mode_status);
  if (output_active(m) && !input_active(m)) {
    *color = COLOR_GREEN;
    cache_copy(buf, len, tr(app, "OUT OK", "输出正常"));
    return;
  }
  *color = ip2366_charge_state_color_147(m->mode_status);
  if (usb_state[0] != '\0' && *color != COLOR_RED) {
    *color = COLOR_GREEN;
  }
  pos = append_text(buf, len, pos, usb_state);
  pos = append_text(buf, len, pos, charge_state);
  finish_text(buf, len, pos);
}

static void draw_147_safe_ring(void)
{
  draw_round_rect(3, 3, (uint16_t)(SCREEN_WIDTH - 6U), (uint16_t)(SCREEN_HEIGHT - 6U), COLOR_DARK);
  draw_round_rect(5, 5, (uint16_t)(SCREEN_WIDTH - 10U), (uint16_t)(SCREEN_HEIGHT - 10U), COLOR_DARK);
}

static void draw_147_value(uint16_t x, uint16_t y, uint16_t w, const char *value,
                           uint16_t color, char *cache, uint32_t cache_len,
                           bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    if (full_refresh) {
      st7735_fill_rect(x, y, w, 16, COLOR_BLACK);
      st7735_draw_text(x, y, value, color, COLOR_BLACK, 2);
    } else {
      draw_ascii_delta(x, y, w, cache, value, color, 2);
    }
    cache_copy(cache, cache_len, value);
  }
}

static void __attribute__((unused)) draw_147_text_value(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                                        uint16_t color, char *cache, uint32_t cache_len,
                                                        bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    if (full_refresh) {
      st7735_fill_rect(x, y, w, 8, COLOR_BLACK);
      st7735_draw_text(x, y, value, color, COLOR_BLACK, 1);
    } else {
      draw_ascii_delta(x, y, w, cache, value, color, 1);
    }
    cache_copy(cache, cache_len, value);
  }
}

static void draw_147_ascii_scaled(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                  uint16_t color, char *cache, uint32_t cache_len,
                                  uint8_t scale, bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    if (full_refresh) {
      st7735_fill_rect(x, y, w, (uint16_t)(8U * scale), COLOR_BLACK);
      st7735_draw_text(x, y, value, color, COLOR_BLACK, scale);
    } else {
      draw_ascii_delta(x, y, w, cache, value, color, scale);
    }
    cache_copy(cache, cache_len, value);
  }
}

static uint16_t ascii_metric_char_step(char c, uint8_t scale)
{
  uint16_t normal_step = (uint16_t)(6U * scale + (scale == 2U ? 1U : 0U));

  if (c != '.') {
    return normal_step;
  }
  return (scale >= 4U) ? (uint16_t)(normal_step - 10U) :
         ((normal_step > 3U) ? (uint16_t)(normal_step - 3U) : normal_step);
}

static void draw_ascii_metric_char(uint16_t x, uint16_t y, uint16_t cell_w,
                                   char c, uint16_t color, uint8_t scale)
{
  char one[2];
  uint8_t dot_scale;
  uint16_t dot_w;
  uint16_t draw_x;
  uint16_t draw_y;

  if (c == '\0') {
    st7735_fill_rect(x, y, cell_w, (uint16_t)(8U * scale), COLOR_BLACK);
    return;
  }
  one[0] = c;
  one[1] = '\0';
  if (c == '.') {
    dot_scale = scale >= 4U ? (uint8_t)(scale / 2U) : 1U;
    dot_w = (uint16_t)(6U * dot_scale);
    draw_x = x;
    if (cell_w > dot_w) {
      draw_x = (uint16_t)(x + ((cell_w - dot_w) / 2U));
    }
    draw_y = (uint16_t)(y + (uint16_t)(8U * scale) - (uint16_t)(8U * dot_scale));
    st7735_draw_text(draw_x, draw_y, one, color, COLOR_BLACK, dot_scale);
  } else {
    st7735_draw_text(x, y, one, color, COLOR_BLACK, scale);
  }
}

static uint8_t make_ascii_metric_layout(uint16_t x, uint16_t w, const char *text, uint8_t scale,
                                        char chars[16], uint16_t xs[16], uint16_t ws[16])
{
  uint8_t i = 0U;
  uint16_t draw_x = x;

  (void)w;
  while (text[i] != '\0' && i < 15U) {
    chars[i] = text[i];
    xs[i] = draw_x;
    ws[i] = ascii_metric_char_step(text[i], scale);
    draw_x = (uint16_t)(draw_x + ws[i]);
    ++i;
  }
  return i;
}

static bool ascii_metric_same_cell(char c, uint16_t x, uint16_t w,
                                   const char chars[16], const uint16_t xs[16],
                                   const uint16_t ws[16], uint8_t count)
{
  uint8_t i;

  for (i = 0U; i < count; ++i) {
    if (chars[i] == c && xs[i] == x && ws[i] == w) {
      return true;
    }
  }
  return false;
}

static void draw_ascii_metric_delta_scaled(uint16_t x, uint16_t y, uint16_t w,
                                           const char *old_value, const char *new_value,
                                           uint16_t color, uint8_t scale, bool full_refresh)
{
  char old_chars[16];
  char new_chars[16];
  uint16_t old_xs[16];
  uint16_t new_xs[16];
  uint16_t old_ws[16];
  uint16_t new_ws[16];
  uint8_t old_count;
  uint8_t new_count;
  uint8_t i;

  old_count = make_ascii_metric_layout(x, w, old_value, scale, old_chars, old_xs, old_ws);
  new_count = make_ascii_metric_layout(x, w, new_value, scale, new_chars, new_xs, new_ws);

  if (full_refresh || old_value[0] == '\0') {
    st7735_fill_rect(x, y, w, (uint16_t)(8U * scale), COLOR_BLACK);
    for (i = 0U; i < new_count; ++i) {
      draw_ascii_metric_char(new_xs[i], y, new_ws[i], new_chars[i], color, scale);
    }
    return;
  }

  for (i = 0U; i < old_count; ++i) {
    if (!ascii_metric_same_cell(old_chars[i], old_xs[i], old_ws[i], new_chars, new_xs, new_ws, new_count)) {
      draw_ascii_metric_char(old_xs[i], y, old_ws[i], '\0', color, scale);
    }
  }
  for (i = 0U; i < new_count; ++i) {
    if (!ascii_metric_same_cell(new_chars[i], new_xs[i], new_ws[i], old_chars, old_xs, old_ws, old_count)) {
      draw_ascii_metric_char(new_xs[i], y, new_ws[i], new_chars[i], color, scale);
    }
  }
}

static void draw_147_ascii_metric_scaled(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                         uint16_t color, char *cache, uint32_t cache_len,
                                         uint8_t scale, bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    draw_ascii_metric_delta_scaled(x, y, w, cache, value, color, scale, full_refresh);
    cache_copy(cache, cache_len, value);
  }
}

static void __attribute__((unused)) draw_147_card(uint16_t x, uint16_t y, uint16_t w, const char *label, const char *value,
                                                  uint16_t color, char *cache, uint32_t cache_len, bool full_refresh)
{
  if (full_refresh) {
    draw_round_rect(x, y, w, 48, COLOR_DARK);
    draw_ref_text((uint16_t)(x + 7U), (uint16_t)(y + 5U), label, COLOR_GRAY);
  }
  draw_147_value((uint16_t)(x + 7U), (uint16_t)(y + 25U), (uint16_t)(w - 14U),
                 value, color, cache, cache_len, full_refresh);
}

typedef struct {
  char vbus[24];
  char ibus[24];
  char power[24];
  char power_big[16];
  char battery_power[24];
  char vbat[24];
  char ibat[24];
  char percent[12];
  char chip_temp[16];
  char board_temp[16];
  char protocol[20];
  char status[32];
  uint16_t percent_color;
  uint16_t chip_temp_color;
  uint16_t board_temp_color;
  uint16_t status_color;
} ui147_values_t;

static void build_ui147_values(app_t *app, ui147_values_t *v)
{
  const sw6236_metrics_t *m = &app->metrics;
  bool battery_charging = input_active(m) && !output_active(m);
  bool chip_temp_valid = m->online && m->chip_temp_c10 >= -400 && m->chip_temp_c10 <= 1500;
  bool board_temp_valid = m->ntc_temp_c10 >= -400 && m->ntc_temp_c10 <= 1500;
  uint32_t battery_power_mw = display_battery_power_mw(m);

  format_bus_mv(v->vbus, sizeof(v->vbus), m->vbus_mv);
  format_bus_ma(v->ibus, sizeof(v->ibus), m->ibus_ma);
  format_power_dynamic(v->power, sizeof(v->power), display_usb_power_mw(m));
  format_page3_power(v->power_big, sizeof(v->power_big), display_usb_power_mw(m));
  format_power_dynamic(v->battery_power, sizeof(v->battery_power), battery_power_mw);
  format_page3_mv(v->vbat, sizeof(v->vbat), m->vbat_mv);
  pad_text(v->vbat, sizeof(v->vbat), v->vbat, 6U);
  format_page3_signed_ma(v->ibat, sizeof(v->ibat), m->ibat_ma, battery_charging);
  pad_text(v->ibat, sizeof(v->ibat), v->ibat, 6U);
  format_percent_147(v->percent, sizeof(v->percent), m->percent);
  format_temp_ascii_147(v->chip_temp, sizeof(v->chip_temp), m->chip_temp_c10, chip_temp_valid);
  format_temp_ascii_147(v->board_temp, sizeof(v->board_temp), m->ntc_temp_c10, board_temp_valid);
  pad_text(v->protocol, sizeof(v->protocol), page1_protocol_text(m), 6U);
  status_text_147(app, m, v->status, sizeof(v->status), &v->status_color);
  v->percent_color = (m->percent < 10U) ? COLOR_RED : ((m->percent < 30U) ? COLOR_ORANGE : COLOR_GREEN);
  v->chip_temp_color = chip_temp_valid ? temp_color(m->chip_temp_c10) : COLOR_RED;
  v->board_temp_color = board_temp_valid ? temp_color(m->ntc_temp_c10) : COLOR_RED;
}

static void reset_ui147_caches(char *caches[], uint8_t count)
{
  uint8_t i;
  for (i = 0U; i < count; ++i) {
    caches[i][0] = '\0';
  }
}

static void trim_copy_147(char *dst, uint32_t len, const char *src)
{
  size_t n;
  if (len == 0U) {
    return;
  }
  cache_copy(dst, len, src);
  n = strlen(dst);
  while (n > 0U && dst[n - 1U] == ' ') {
    dst[--n] = '\0';
  }
}

static void __attribute__((unused)) draw_147_small_card(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                                        const char *label, const char *value, uint16_t color,
                                                        char *cache, uint32_t cache_len, bool full_refresh)
{
  if (full_refresh) {
    draw_round_rect(x, y, w, h, COLOR_DARK);
    draw_ref_text((uint16_t)(x + 6U), (uint16_t)(y + 5U), label, COLOR_GRAY);
  }
  draw_147_ascii_scaled((uint16_t)(x + 6U), (uint16_t)(y + h - 18U),
                        (uint16_t)(w - 10U), value, color, cache, cache_len, 2U, full_refresh);
}

static void draw_page1_ref_value(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                 uint16_t color, char *cache, uint32_t cache_len,
                                 bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    if (!full_refresh && cache[0] != '\0' && ref_text_width(cache) == ref_text_width(value)) {
      if (is_ascii_text(cache) && is_ascii_text(value)) {
        draw_info_text_delta(x, y, x, w, cache, value, color);
      } else {
        draw_ref_text_delta_cells(x, y, w, cache, value, color);
      }
    } else {
      st7735_fill_rect(x, y, w, 16, COLOR_BLACK);
      draw_ref_text(x, y, value, color);
    }
    cache_copy(cache, cache_len, value);
  }
}

static uint16_t draw_page4_value_text(uint16_t x, uint16_t y, const char *value, uint16_t color)
{
  const char *p = value;

  while ((p != 0) && (*p != '\0')) {
    if (((uint8_t)*p) < 0x80U) {
      char one[2] = {*p, '\0'};
      st7735_draw_text(x, y, one, color, COLOR_BLACK, 2U);
      x = (uint16_t)(x + 12U);
      p++;
    } else {
      char one[4] = {0, 0, 0, 0};
      one[0] = p[0];
      if (p[1] != '\0') one[1] = p[1];
      if (p[2] != '\0') one[2] = p[2];
      draw_ref_text(x, y, one, color);
      x = (uint16_t)(x + 16U);
      p += (p[1] == '\0') ? 1 : ((p[2] == '\0') ? 2 : 3);
    }
  }
  return x;
}

static void draw_page4_value(uint16_t x, uint16_t y, uint16_t w, const char *value,
                             uint16_t color, char *cache, uint32_t cache_len,
                             bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    if (!full_refresh && cache[0] != '\0' && strlen(cache) == strlen(value) &&
        is_ascii_text(cache) && is_ascii_text(value)) {
      draw_ascii_delta(x, y, w, cache, value, color, 2U);
    } else {
      st7735_fill_rect(x, y, w, 16, COLOR_BLACK);
      (void)draw_page4_value_text(x, y, value, color);
    }
    cache_copy(cache, cache_len, value);
  }
}

static void draw_page4_value_compact(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                     uint16_t color, char *cache, uint32_t cache_len,
                                     bool full_refresh)
{
  if (full_refresh || strcmp(cache, value) != 0) {
    draw_ascii_metric_delta_scaled(x, y, w, cache, value, color, 2U, full_refresh);
    cache_copy(cache, cache_len, value);
  }
}

static void copy_ascii_prefix(char *dst, uint32_t len, const char *src)
{
  uint32_t pos = 0U;

  if (len == 0U) {
    return;
  }
  while (*src != '\0' && ((uint8_t)*src) < 0x80U && pos < (len - 1U)) {
    dst[pos++] = *src++;
  }
  dst[pos] = '\0';
}

static bool text_has_non_ascii(const char *text)
{
  while (*text != '\0') {
    if (((uint8_t)*text) >= 0x80U) {
      return true;
    }
    ++text;
  }
  return false;
}

static void draw_page4_temp_value_compact(uint16_t x, uint16_t y, uint16_t w, const char *value,
                                          uint16_t color, char *cache, uint32_t cache_len,
                                          bool full_refresh)
{
  char old_num[12];
  char new_num[12];
  uint16_t unit_x = (uint16_t)(x + 52U);

  if (full_refresh || strcmp(cache, value) != 0) {
    copy_ascii_prefix(old_num, sizeof(old_num), cache);
    copy_ascii_prefix(new_num, sizeof(new_num), value);
    draw_ascii_metric_delta_scaled(x, y, w > 18U ? (uint16_t)(w - 18U) : w,
                                   old_num, new_num, color, 2U, full_refresh);
    if (full_refresh || cache[0] == '\0') {
      st7735_fill_rect(unit_x, y, 18, 16, COLOR_BLACK);
      draw_ref_text(unit_x, y, "\xE2\x84\x83", color);
    }
    cache_copy(cache, cache_len, value);
  }
}

static void draw_page4_metric_value(bool compact, uint16_t x, uint16_t y, uint16_t w,
                                    const char *value, uint16_t color,
                                    char *cache, uint32_t cache_len, bool full_refresh)
{
  if (compact) {
    if (text_has_non_ascii(value)) {
      draw_page4_temp_value_compact(x, y, w, value, color, cache, cache_len, full_refresh);
    } else {
      draw_page4_value_compact(x, y, w, value, color, cache, cache_len, full_refresh);
    }
  } else {
    draw_page4_value(x, y, w, value, color, cache, cache_len, full_refresh);
  }
}

static void draw_page1_cell_label(uint16_t x, uint16_t y, const char *label)
{
  draw_ref_text(x, y, label, COLOR_GRAY);
}

static void render_page1_147(app_t *app, bool full_refresh)
{
  ui147_values_t v;
  bool english = app->language == LANG_ENGLISH;
  static char cache_vbus[24];
  static char cache_ibus[24];
  static char cache_power[24];
  static char cache_battery_power[24];
  static char cache_vbat[24];
  static char cache_ibat[24];
  static char cache_percent_top[12];
  static char cache_chip_temp[16];
  static char cache_board_temp[16];
  static char cache_status[32];
  static char cache_protocol[20];
  static uint16_t cache_percent_top_color;
  static uint16_t cache_chip_temp_color;
  static uint16_t cache_board_temp_color;
  static uint16_t cache_status_color;
  static uint8_t cache_battery_icon_percent;

  if (full_refresh) {
    char *caches[] = {
      cache_vbus, cache_ibus, cache_power, cache_vbat, cache_ibat, cache_percent_top,
      cache_battery_power, cache_chip_temp, cache_board_temp, cache_status, cache_protocol
    };
    st7735_fill_screen(COLOR_BLACK);
    reset_ui147_caches(caches, (uint8_t)(sizeof(caches) / sizeof(caches[0])));
    cache_percent_top_color = 0U;
    cache_chip_temp_color = 0U;
    cache_board_temp_color = 0U;
    cache_status_color = 0U;
    cache_battery_icon_percent = 0xFFU;

    draw_147_safe_ring();
    draw_ref_text(16, 12, tr(app, "Protocol", "协议"), COLOR_GRAY);
    st7735_draw_hline(14, 34, 292, COLOR_DARK);
    st7735_draw_hline(14, 74, 292, COLOR_DARK);
    st7735_draw_hline(14, 114, 292, COLOR_DARK);

    draw_page1_cell_label(16, 42, tr(app, "Input Volt", "输入电压"));
    draw_page1_cell_label(116, 42, tr(app, "Input Curr", "输入电流"));
    draw_page1_cell_label(214, 42, tr(app, "USB Power", "USB端功率"));
    draw_page1_cell_label(16, 82, tr(app, "Batt Volt", "电池电压"));
    draw_page1_cell_label(116, 82, tr(app, "Batt Curr", "电池电流"));
    draw_page1_cell_label(214, 82, tr(app, "Batt Power", "电池端功率"));
    draw_page1_cell_label(16, 122, tr(app, "Board Temp", "主板温度"));
    draw_page1_cell_label(116, 122, tr(app, "NTC Temp", "NTC温度"));
    draw_page1_cell_label(210, 122, tr(app, "Status", "状态"));
  }

  build_ui147_values(app, &v);

  draw_page1_ref_value(english ? 88U : 56U, 12, english ? 104U : 144U,
                       v.protocol, COLOR_YELLOW, cache_protocol, sizeof(cache_protocol), full_refresh);
  if (full_refresh || cache_battery_icon_percent != app->metrics.percent) {
    draw_battery_icon_ref(english ? 216U : 218U, 12, app->metrics.percent);
    cache_battery_icon_percent = app->metrics.percent;
  }
  if (full_refresh || strcmp(cache_percent_top, v.percent) != 0 || cache_percent_top_color != v.percent_color) {
    if (cache_percent_top_color != v.percent_color) {
      cache_percent_top[0] = '\0';
    }
    draw_page1_ref_value(english ? 256U : 258U, 12, 48, v.percent, v.percent_color, cache_percent_top, sizeof(cache_percent_top), true);
    cache_percent_top_color = v.percent_color;
  }

  draw_page1_ref_value(16, 60, 82, v.vbus, COLOR_CYAN, cache_vbus, sizeof(cache_vbus), full_refresh);
  draw_page1_ref_value(116, 60, 82, v.ibus, COLOR_GREEN, cache_ibus, sizeof(cache_ibus), full_refresh);
  draw_page1_ref_value(214, 60, 82, v.power, COLOR_ORANGE, cache_power, sizeof(cache_power), full_refresh);
  draw_page1_ref_value(16, 100, 82, v.vbat, COLOR_CYAN, cache_vbat, sizeof(cache_vbat), full_refresh);
  draw_page1_ref_value(116, 100, 82, v.ibat, COLOR_GREEN, cache_ibat, sizeof(cache_ibat), full_refresh);
  draw_page1_ref_value(214, 100, 82, v.battery_power, COLOR_ORANGE, cache_battery_power, sizeof(cache_battery_power), full_refresh);

  if (full_refresh || strcmp(cache_chip_temp, v.chip_temp) != 0 || cache_chip_temp_color != v.chip_temp_color) {
    if (cache_chip_temp_color != v.chip_temp_color) {
      cache_chip_temp[0] = '\0';
    }
    draw_page1_ref_value(16, 140, 88, v.chip_temp, v.chip_temp_color, cache_chip_temp, sizeof(cache_chip_temp), full_refresh);
    cache_chip_temp_color = v.chip_temp_color;
  }
  if (full_refresh || strcmp(cache_board_temp, v.board_temp) != 0 || cache_board_temp_color != v.board_temp_color) {
    if (cache_board_temp_color != v.board_temp_color) {
      cache_board_temp[0] = '\0';
    }
    draw_page1_ref_value(116, 140, 88, v.board_temp, v.board_temp_color, cache_board_temp, sizeof(cache_board_temp), full_refresh);
    cache_board_temp_color = v.board_temp_color;
  }

  if (full_refresh || strcmp(cache_status, v.status) != 0 || cache_status_color != v.status_color) {
    if (cache_status_color != v.status_color) {
      cache_status[0] = '\0';
    }
    draw_page1_ref_value(english ? 204U : 210U, 140, english ? 106U : 96U,
                         v.status, v.status_color, cache_status, sizeof(cache_status), true);
    cache_copy(cache_status, sizeof(cache_status), v.status);
    cache_status_color = v.status_color;
  }
}

static void render_page2_147(app_t *app, bool full_refresh)
{
  ui147_values_t v;
  const sw6236_metrics_t *m = &app->metrics;
  char vbus[16], ibus[16], vbat[16], ibat[16], usb_power[16], battery_power[16];
  char main_temp[16], ntc_temp[16], percent[12], protocol[20], efficiency[12];
  uint32_t battery_power_mw;
  bool main_temp_valid;
  bool ntc_temp_valid;
  static char cache_protocol[20];
  static char cache_vbus[16];
  static char cache_ibus[16];
  static char cache_vbat[16];
  static char cache_ibat[16];
  static char cache_usb_power[16];
  static char cache_battery_power[16];
  static char cache_main_temp[16];
  static char cache_ntc_temp[16];
  static char cache_status[32];
  static char cache_percent[12];
  static char cache_efficiency[12];
  static uint16_t cache_status_color;
  static uint16_t cache_percent_color;
  static uint16_t cache_main_temp_color;
  static uint16_t cache_ntc_temp_color;
  static uint8_t cache_battery_icon_percent;
  bool english = app->language == LANG_ENGLISH;

  if (full_refresh) {
    char *caches[] = {
      cache_protocol, cache_vbus, cache_ibus, cache_vbat, cache_ibat, cache_usb_power,
      cache_battery_power, cache_main_temp, cache_ntc_temp, cache_status, cache_percent,
      cache_efficiency
    };
    st7735_fill_screen(COLOR_BLACK);
    reset_ui147_caches(caches, (uint8_t)(sizeof(caches) / sizeof(caches[0])));
    cache_status_color = 0U;
    cache_percent_color = 0U;
    cache_main_temp_color = 0U;
    cache_ntc_temp_color = 0U;
    cache_battery_icon_percent = 0xFFU;
    draw_147_safe_ring();
    st7735_draw_hline(14, 34, 292, COLOR_DARK);
    st7735_draw_hline(14, 136, 292, COLOR_DARK);
    st7735_draw_vline(106, 42, 84, COLOR_DARK);
    st7735_draw_vline(214, 42, 84, COLOR_DARK);

    draw_ref_text(16, 12, tr(app, "Board", "主板"), COLOR_GRAY);
    draw_ref_text(english ? 112U : 96U, 12, "NTC", COLOR_GRAY);

    draw_ref_text(18, 44, tr(app, "USB Volt", "USB电压"), COLOR_CYAN);
    draw_ref_text(18, 94, tr(app, "USB Curr", "USB电流"), COLOR_GREEN);
    draw_ref_text(224, 44, tr(app, "Batt Volt", "电池电压"), COLOR_CYAN);
    draw_ref_text(224, 94, tr(app, "Batt Curr", "电池电流"), COLOR_GREEN);
    draw_ref_text(124, 44, tr(app, "USB Power", "USB功率"), COLOR_ORANGE);
    draw_ref_text(124, 90, tr(app, "Batt Power", "电池功率"), COLOR_ORANGE);

    draw_ref_text(16, 146, tr(app, "Status", "状态"), COLOR_GRAY);
    draw_ref_text(220, 146, tr(app, "Eff", "效率"), COLOR_GREEN);
  }

  build_ui147_values(app, &v);
  battery_power_mw = display_battery_power_mw(m);
  main_temp_valid = m->online && m->chip_temp_c10 >= -400 && m->chip_temp_c10 <= 1500;
  ntc_temp_valid = m->ntc_temp_c10 >= -400 && m->ntc_temp_c10 <= 1500;
  trim_copy_147(protocol, sizeof(protocol), v.protocol);
  trim_copy_147(vbus, sizeof(vbus), v.vbus);
  trim_copy_147(ibus, sizeof(ibus), v.ibus);
  trim_copy_147(vbat, sizeof(vbat), v.vbat);
  trim_copy_147(ibat, sizeof(ibat), v.ibat);
  trim_copy_147(percent, sizeof(percent), v.percent);
  format_temp_c10_compact(main_temp, sizeof(main_temp), m->chip_temp_c10, main_temp_valid);
  format_temp_c10_compact(ntc_temp, sizeof(ntc_temp), m->ntc_temp_c10, ntc_temp_valid);
  format_page3_power(usb_power, sizeof(usb_power), display_usb_power_mw(m));
  format_page3_power(battery_power, sizeof(battery_power), battery_power_mw);
  format_page2_efficiency(efficiency, sizeof(efficiency), m, battery_power_mw);

  if (full_refresh || strcmp(cache_main_temp, main_temp) != 0 || cache_main_temp_color != v.chip_temp_color) {
    if (cache_main_temp_color != v.chip_temp_color) cache_main_temp[0] = '\0';
    draw_page1_ref_value(english ? 62U : 50U, 12, 42, main_temp, v.chip_temp_color, cache_main_temp, sizeof(cache_main_temp), full_refresh);
    cache_main_temp_color = v.chip_temp_color;
  }
  if (full_refresh || strcmp(cache_ntc_temp, ntc_temp) != 0 || cache_ntc_temp_color != v.board_temp_color) {
    if (cache_ntc_temp_color != v.board_temp_color) cache_ntc_temp[0] = '\0';
    draw_page1_ref_value(english ? 142U : 126U, 12, 42, ntc_temp, v.board_temp_color, cache_ntc_temp, sizeof(cache_ntc_temp), full_refresh);
    cache_ntc_temp_color = v.board_temp_color;
  }
  draw_page1_ref_value(english ? 186U : 174U, 12, english ? 56U : 68U,
                       protocol, COLOR_YELLOW, cache_protocol, sizeof(cache_protocol), full_refresh);
  if (full_refresh || cache_battery_icon_percent != app->metrics.percent) {
    draw_battery_icon_ref(244, 11, app->metrics.percent);
    cache_battery_icon_percent = app->metrics.percent;
  }
  if (full_refresh || strcmp(cache_percent, percent) != 0 || cache_percent_color != v.percent_color) {
    if (cache_percent_color != v.percent_color) cache_percent[0] = '\0';
    draw_page1_ref_value(276, 12, 32, percent, v.percent_color, cache_percent, sizeof(cache_percent), full_refresh);
    cache_percent_color = v.percent_color;
  }

  draw_147_ascii_scaled(18, 64, 80, vbus, COLOR_CYAN, cache_vbus, sizeof(cache_vbus), 2U, full_refresh);
  draw_147_ascii_scaled(18, 114, 80, ibus, COLOR_GREEN, cache_ibus, sizeof(cache_ibus), 2U, full_refresh);
  draw_147_ascii_scaled(224, 64, 80, vbat, COLOR_CYAN, cache_vbat, sizeof(cache_vbat), 2U, full_refresh);
  draw_147_ascii_scaled(224, 114, 80, ibat, COLOR_GREEN, cache_ibat, sizeof(cache_ibat), 2U, full_refresh);
  draw_147_ascii_scaled(112, 62, 98, usb_power, COLOR_ORANGE, cache_usb_power, sizeof(cache_usb_power), 3U, full_refresh);
  draw_147_ascii_scaled(112, 110, 98, battery_power, COLOR_ORANGE, cache_battery_power, sizeof(cache_battery_power), 3U, full_refresh);

  if (full_refresh || strcmp(cache_status, v.status) != 0 || cache_status_color != v.status_color) {
    if (cache_status_color != v.status_color) {
      cache_status[0] = '\0';
    }
    draw_page1_ref_value(56, 146, english ? 154U : 112U,
                         v.status, v.status_color, cache_status, sizeof(cache_status), true);
    cache_copy(cache_status, sizeof(cache_status), v.status);
    cache_status_color = v.status_color;
  }
  draw_page1_ref_value(258, 146, 48, efficiency, COLOR_GREEN, cache_efficiency, sizeof(cache_efficiency), full_refresh);
}

static void render_page3_147(app_t *app, bool full_refresh)
{
  const sw6236_metrics_t *m = &app->metrics;
  char usb_v[16], usb_i[16], usb_p[16];
  static char cache_usb_v[16], cache_usb_i[16], cache_usb_p[16];
  bool english = app->language == LANG_ENGLISH;

  if (full_refresh) {
    char *caches[] = {
      cache_usb_v, cache_usb_i, cache_usb_p
    };
    st7735_fill_screen(COLOR_BLACK);
    reset_ui147_caches(caches, (uint8_t)(sizeof(caches) / sizeof(caches[0])));
    draw_147_safe_ring();
    st7735_draw_hline(18, 57, 284, COLOR_DARK);
    st7735_draw_hline(18, 104, 284, COLOR_DARK);
    if (english) {
      st7735_draw_text(10, 25, "USB", COLOR_CYAN, COLOR_BLACK, 3U);
      st7735_draw_text(68, 25, "Volt", COLOR_CYAN, COLOR_BLACK, 3U);
      st7735_draw_text(10, 73, "USB", COLOR_GREEN, COLOR_BLACK, 3U);
      st7735_draw_text(68, 73, "Curr", COLOR_GREEN, COLOR_BLACK, 3U);
      st7735_draw_text(10, 121, "USB", COLOR_ORANGE, COLOR_BLACK, 3U);
      st7735_draw_text(68, 121, "Power", COLOR_ORANGE, COLOR_BLACK, 3U);
    } else {
      st7735_draw_text(16, 25, "USB", COLOR_CYAN, COLOR_BLACK, 3U);
      zh_font_draw_text16_ref_scaled(72, 17, tr(app, "Volt", "电压"), COLOR_CYAN, COLOR_BLACK, 2U);
      st7735_draw_text(16, 73, "USB", COLOR_GREEN, COLOR_BLACK, 3U);
      zh_font_draw_text16_ref_scaled(72, 65, tr(app, "Curr", "电流"), COLOR_GREEN, COLOR_BLACK, 2U);
      st7735_draw_text(16, 121, "USB", COLOR_ORANGE, COLOR_BLACK, 3U);
      zh_font_draw_text16_ref_scaled(72, 113, tr(app, "Power", "功率"), COLOR_ORANGE, COLOR_BLACK, 2U);
    }
  }

  format_page3_milli_unit(usb_v, sizeof(usb_v), m->vbus_mv, 'V');
  format_page3_milli_unit(usb_i, sizeof(usb_i), m->ibus_ma, 'A');
  format_page3_milli_unit(usb_p, sizeof(usb_p), display_usb_power_mw(m), 'W');

  if (english) {
    draw_147_ascii_metric_scaled(180, 18, 134, usb_v, COLOR_CYAN, cache_usb_v, sizeof(cache_usb_v), 4U, full_refresh);
    draw_147_ascii_metric_scaled(180, 66, 134, usb_i, COLOR_GREEN, cache_usb_i, sizeof(cache_usb_i), 4U, full_refresh);
    draw_147_ascii_metric_scaled(180, 114, 134, usb_p, COLOR_ORANGE, cache_usb_p, sizeof(cache_usb_p), 4U, full_refresh);
  } else {
    draw_147_ascii_scaled(156, 18, 148, usb_v, COLOR_CYAN, cache_usb_v, sizeof(cache_usb_v), 4U, full_refresh);
    draw_147_ascii_scaled(156, 66, 148, usb_i, COLOR_GREEN, cache_usb_i, sizeof(cache_usb_i), 4U, full_refresh);
    draw_147_ascii_scaled(156, 114, 148, usb_p, COLOR_ORANGE, cache_usb_p, sizeof(cache_usb_p), 4U, full_refresh);
  }
}

static void render_page4_147(app_t *app, bool full_refresh)
{
  ui147_values_t v;
  char protocol[20], percent[12], chip_temp[16], board_temp[16], max_power[16];
  char vbus[24], ibus[24], power[24], vbat[24], ibat[24];
  uint8_t power_reg;
  static char cache_protocol_top[20], cache_status[32], cache_percent_top[12];
  static char cache_vbus[24], cache_ibus[24], cache_power[24], cache_vbat[24];
  static char cache_ibat[24], cache_chip_temp[16], cache_board_temp[16], cache_max_power[16];
  static uint16_t cache_status_color, cache_percent_color, cache_chip_temp_color, cache_board_temp_color;
  static uint8_t cache_battery_icon_percent;
  bool english = app->language == LANG_ENGLISH;

  if (full_refresh) {
    char *caches[] = {
      cache_protocol_top, cache_status, cache_percent_top, cache_vbus, cache_ibus, cache_power,
      cache_vbat, cache_ibat, cache_chip_temp, cache_board_temp, cache_max_power
    };
    st7735_fill_screen(COLOR_BLACK);
    reset_ui147_caches(caches, (uint8_t)(sizeof(caches) / sizeof(caches[0])));
    cache_status_color = 0U;
    cache_percent_color = 0U;
    cache_chip_temp_color = 0U;
    cache_board_temp_color = 0U;
    cache_battery_icon_percent = 0xFFU;
    draw_147_safe_ring();
    draw_ref_text(english ? 10U : 26U, 43, tr(app, "USB Volt", "USB电压"), COLOR_GRAY);
    draw_ref_text(english ? 162U : 166U, 43, tr(app, "USB Curr", "USB电流"), COLOR_GRAY);
    draw_ref_text(english ? 10U : 26U, 72, tr(app, "USB Power", "USB功率"), COLOR_GRAY);
    draw_ref_text(english ? 162U : 166U, 72, tr(app, "Batt Volt", "电池电压"), COLOR_GRAY);
    draw_ref_text(english ? 10U : 26U, 101, tr(app, "Batt Curr", "电池电流"), COLOR_GRAY);
    draw_ref_text(english ? 162U : 166U, 101, tr(app, "Board Temp", "主板温度"), COLOR_GRAY);
    draw_ref_text(english ? 10U : 26U, 130, tr(app, "NTC Temp", "NTC温度"), COLOR_GRAY);
    draw_ref_text(english ? 162U : 166U, 130, tr(app, "Max Power", "最大功率"), COLOR_GRAY);
    draw_round_rect(16, 37, 288, 24, COLOR_DARK);
    draw_round_rect(16, 66, 288, 24, COLOR_DARK);
    draw_round_rect(16, 95, 288, 24, COLOR_DARK);
    draw_round_rect(16, 124, 288, 24, COLOR_DARK);
  }

  build_ui147_values(app, &v);
  trim_copy_147(protocol, sizeof(protocol), v.protocol);
  trim_copy_147(percent, sizeof(percent), v.percent);
  trim_copy_147(vbus, sizeof(vbus), v.vbus);
  trim_copy_147(ibus, sizeof(ibus), v.ibus);
  trim_copy_147(power, sizeof(power), v.power);
  trim_copy_147(vbat, sizeof(vbat), v.vbat);
  trim_copy_147(ibat, sizeof(ibat), v.ibat);
  trim_copy_147(chip_temp, sizeof(chip_temp), v.chip_temp);
  trim_copy_147(board_temp, sizeof(board_temp), v.board_temp);
  if (sw6236_read_reg(IP2366_REG_OUTPUT_PWR, &power_reg)) {
    power_reg = (uint8_t)((power_reg >> 5) & 0x07U);
    if (power_reg > 5U) power_reg = 5U;
    cache_copy(max_power, sizeof(max_power), k_output_power_text[power_reg]);
  } else {
    cache_copy(max_power, sizeof(max_power), "--");
  }

  draw_page1_ref_value(16, 13, 64, protocol, COLOR_YELLOW, cache_protocol_top, sizeof(cache_protocol_top), full_refresh);
  if (full_refresh || strcmp(cache_status, v.status) != 0 || cache_status_color != v.status_color) {
    if (cache_status_color != v.status_color) cache_status[0] = '\0';
    draw_page1_ref_value(94, 13, 112, v.status, v.status_color, cache_status, sizeof(cache_status), full_refresh);
    cache_copy(cache_status, sizeof(cache_status), v.status);
    cache_status_color = v.status_color;
  }
  if (full_refresh || strcmp(cache_percent_top, percent) != 0 || cache_percent_color != v.percent_color) {
    if (cache_percent_color != v.percent_color) cache_percent_top[0] = '\0';
    draw_page1_ref_value(258, 13, 48, percent, v.percent_color, cache_percent_top, sizeof(cache_percent_top), full_refresh);
    cache_percent_color = v.percent_color;
  }
  if (full_refresh || cache_battery_icon_percent != app->metrics.percent) {
    draw_battery_icon_ref(218, 13, app->metrics.percent);
    cache_battery_icon_percent = app->metrics.percent;
  }

  draw_page4_metric_value(english, english ? 86U : 92U, 41, english ? 76U : 72U, vbus, COLOR_CYAN, cache_vbus, sizeof(cache_vbus), full_refresh);
  draw_page4_metric_value(english, english ? 244U : 232U, 41, english ? 76U : 72U, ibus, COLOR_GREEN, cache_ibus, sizeof(cache_ibus), full_refresh);
  draw_page4_metric_value(english, english ? 86U : 92U, 70, english ? 76U : 72U, power, COLOR_ORANGE, cache_power, sizeof(cache_power), full_refresh);
  draw_page4_metric_value(english, english ? 244U : 232U, 70, english ? 76U : 72U, vbat, COLOR_CYAN, cache_vbat, sizeof(cache_vbat), full_refresh);
  draw_page4_metric_value(english, english ? 86U : 92U, 99, english ? 76U : 72U, ibat, COLOR_GREEN, cache_ibat, sizeof(cache_ibat), full_refresh);
  if (full_refresh || strcmp(cache_chip_temp, chip_temp) != 0 || cache_chip_temp_color != v.chip_temp_color) {
    if (cache_chip_temp_color != v.chip_temp_color) cache_chip_temp[0] = '\0';
    draw_page4_metric_value(english, english ? 244U : 232U, 99, english ? 76U : 72U, chip_temp, v.chip_temp_color, cache_chip_temp, sizeof(cache_chip_temp), full_refresh);
    cache_chip_temp_color = v.chip_temp_color;
  }
  if (full_refresh || strcmp(cache_board_temp, board_temp) != 0 || cache_board_temp_color != v.board_temp_color) {
    if (cache_board_temp_color != v.board_temp_color) cache_board_temp[0] = '\0';
    draw_page4_metric_value(english, english ? 86U : 92U, 128, english ? 76U : 72U, board_temp, v.board_temp_color, cache_board_temp, sizeof(cache_board_temp), full_refresh);
    cache_board_temp_color = v.board_temp_color;
  }
  draw_page4_metric_value(english, english ? 244U : 232U, 128, english ? 76U : 72U, max_power, COLOR_ORANGE, cache_max_power, sizeof(cache_max_power), full_refresh);
}

static void __attribute__((unused)) render_page2(app_t *app, bool full_refresh)
{
  char line[24];
  const sw6236_metrics_t *m = &app->metrics;
  static char cache_vbus[24];
  static char cache_ibus[24];
  static char cache_power[24];

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    cache_vbus[0] = '\0';
    cache_ibus[0] = '\0';
    cache_power[0] = '\0';
  }

  render_page_header(app, m, full_refresh);
  if (full_refresh) {
    st7735_draw_hline(0, 14, 160, COLOR_DARK);
  }

  if (full_refresh) {
    draw_ref_text(4, 18, tr(app, "Volt", "电压"), COLOR_CYAN);
  }
  format_bus_mv(line, sizeof(line), m->vbus_mv);
  if (full_refresh || strcmp(cache_vbus, line) != 0) {
    draw_ascii_delta(52, 18, 104, cache_vbus, line, COLOR_CYAN, 2);
    cache_copy(cache_vbus, sizeof(cache_vbus), line);
  }

  if (full_refresh) {
    draw_ref_text(4, 40, tr(app, "Curr", "电流"), COLOR_GREEN);
  }
  format_bus_ma(line, sizeof(line), m->ibus_ma);
  if (full_refresh || strcmp(cache_ibus, line) != 0) {
    draw_ascii_delta(52, 40, 104, cache_ibus, line, COLOR_GREEN, 2);
    cache_copy(cache_ibus, sizeof(cache_ibus), line);
  }

  if (full_refresh) {
    draw_ref_text(4, 62, tr(app, "Power", "功率"), COLOR_ORANGE);
  }
  format_power_dynamic(line, sizeof(line), display_usb_power_mw(m));
  if (full_refresh || strcmp(cache_power, line) != 0) {
    draw_ascii_delta(52, 62, 104, cache_power, line, COLOR_ORANGE, 2);
    cache_copy(cache_power, sizeof(cache_power), line);
  }
}

static void draw_ref_text_delta_fixed(uint16_t x, uint16_t y, char *cache, uint32_t cache_len, const char *text, uint16_t color, bool full_refresh)
{
  uint8_t i;
  uint8_t len = (uint8_t)strlen(text);

  if (full_refresh || cache[0] == '\0' || strlen(cache) != len) {
    st7735_fill_rect(x, y, (uint16_t)(len * 8U), 16, COLOR_BLACK);
    draw_ref_text(x, y, text, color);
    cache_copy(cache, cache_len, text);
    return;
  }

  for (i = 0U; i < len; ++i) {
    if (cache[i] != text[i]) {
      char one[2] = {text[i], '\0'};
      uint16_t draw_x = (uint16_t)(x + (uint16_t)i * 8U);
      st7735_fill_rect(draw_x, y, 8, 16, COLOR_BLACK);
      draw_ref_text(draw_x, y, one, color);
    }
  }
  cache_copy(cache, cache_len, text);
}

static void __attribute__((unused)) render_page3(app_t *app, bool full_refresh)
{
  char usb_v[16];
  char usb_i[16];
  char usb_p[16];
  char bat_v[16];
  char bat_i[16];
  char bat_p[16];
  char eff[8];
  uint32_t battery_power_mw;
  bool battery_charging;
  const sw6236_metrics_t *m = &app->metrics;
  static char cache_usb_v[16];
  static char cache_usb_i[16];
  static char cache_usb_p[16];
  static char cache_bat_v[16];
  static char cache_bat_i[16];
  static char cache_bat_p[16];
  static char cache_eff[8];

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    cache_usb_v[0] = '\0';
    cache_usb_i[0] = '\0';
    cache_usb_p[0] = '\0';
    cache_bat_v[0] = '\0';
    cache_bat_i[0] = '\0';
    cache_bat_p[0] = '\0';
    cache_eff[0] = '\0';

    draw_round_rect(0, 0, 160, 80, COLOR_DARK);
    st7735_draw_vline(80, 1, 61, COLOR_DARK);
    st7735_draw_hline(2, 62, 156, COLOR_DARK);
    draw_ref_text(24, 1, "USB", COLOR_CYAN);
    draw_ref_text(100, 1, tr(app, "BAT", "电池"), COLOR_YELLOW);
    draw_ref_text(2, 17, tr(app, "Volt", "电压"), COLOR_CYAN);
    draw_ref_text(2, 33, tr(app, "Curr", "电流"), COLOR_GREEN);
    draw_ref_text(2, 49, tr(app, "Power", "功率"), COLOR_ORANGE);
    draw_ref_text(82, 17, tr(app, "Volt", "电压"), COLOR_CYAN);
    draw_ref_text(82, 33, tr(app, "Curr", "电流"), COLOR_GREEN);
    draw_ref_text(82, 49, tr(app, "Power", "功率"), COLOR_ORANGE);
    draw_ref_text(42, 63, tr(app, "Eff", "效率"), COLOR_GREEN);
  }

  format_page3_mv(usb_v, sizeof(usb_v), m->vbus_mv);
  format_page3_ma(usb_i, sizeof(usb_i), m->ibus_ma);
  format_page3_power(usb_p, sizeof(usb_p), display_usb_power_mw(m));
  format_page3_mv(bat_v, sizeof(bat_v), m->vbat_mv);
  battery_charging = input_active(m) && !output_active(m);
  format_page3_signed_ma(bat_i, sizeof(bat_i), m->ibat_ma, battery_charging);
  battery_power_mw = display_battery_power_mw(m);
  format_page3_signed_power(bat_p, sizeof(bat_p), battery_power_mw, battery_charging);
  format_efficiency(eff, sizeof(eff), m, battery_power_mw);
  right_pad_text(usb_v, sizeof(usb_v), 5U);
  right_pad_text(usb_i, sizeof(usb_i), 5U);
  right_pad_text(usb_p, sizeof(usb_p), 5U);
  right_pad_text(bat_v, sizeof(bat_v), 5U);
  right_pad_text(bat_i, sizeof(bat_i), 5U);
  right_pad_text(bat_p, sizeof(bat_p), 5U);
  right_pad_text(eff, sizeof(eff), 6U);

  draw_ref_text_delta_fixed(38, 17, cache_usb_v, sizeof(cache_usb_v), usb_v, COLOR_CYAN, full_refresh);
  draw_ref_text_delta_fixed(38, 33, cache_usb_i, sizeof(cache_usb_i), usb_i, COLOR_GREEN, full_refresh);
  draw_ref_text_delta_fixed(38, 49, cache_usb_p, sizeof(cache_usb_p), usb_p, COLOR_ORANGE, full_refresh);
  draw_ref_text_delta_fixed(118, 17, cache_bat_v, sizeof(cache_bat_v), bat_v, COLOR_CYAN, full_refresh);
  draw_ref_text_delta_fixed(118, 33, cache_bat_i, sizeof(cache_bat_i), bat_i, COLOR_GREEN, full_refresh);
  draw_ref_text_delta_fixed(118, 49, cache_bat_p, sizeof(cache_bat_p), bat_p, COLOR_ORANGE, full_refresh);
  draw_ref_text_delta_fixed(82, 63, cache_eff, sizeof(cache_eff), eff, COLOR_GREEN, full_refresh);
}

static void draw_menu_value(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, const char *value)
{
  st7735_fill_rect(x, (uint16_t)(y + 2U), 90, 19, bg);
  zh_font_draw_text16_ref(x, (uint16_t)(y + 3U), value, fg, bg);
}

static void __attribute__((unused)) draw_ascii_fit_width(uint16_t x, uint16_t y, uint16_t w, const char *text, uint16_t color)
{
  uint16_t len = (uint16_t)strlen(text);
  uint16_t step = len > 1U ? (uint16_t)((w - 8U) / (len - 1U)) : 8U;

  if (step > 10U) {
    step = 10U;
  }
  st7735_fill_rect(x, y, w, 16, COLOR_BLACK);
  for (uint16_t i = 0U; i < len; ++i) {
    st7735_draw_char8x16((uint16_t)(x + i * step), y, text[i], color, COLOR_BLACK);
  }
}

static bool ip2366_read_menu_value(uint8_t index, uint8_t *value)
{
  uint8_t reg;

  switch (index) {
    case MENU_BATTERY_VOLTAGE:
      if (!sw6236_read_reg(IP2366_REG_VSET, &reg)) return false;
      if (reg > BATTERY_FULL_VOLTAGE_MAX_CODE) reg = BATTERY_FULL_VOLTAGE_MAX_CODE;
      *value = reg;
      return true;
    case MENU_BATTERY_CURRENT:
      if (!sw6236_read_reg(IP2366_REG_ISET, &reg)) return false;
      if (reg < 10U) reg = 10U;
      if (reg > 97U) reg = 97U;
      *value = reg;
      return true;
    case MENU_TRICKLE_CURRENT:
      if (!sw6236_read_reg(IP2366_REG_TRICKLE, &reg)) return false;
      if (reg < 2U) reg = 2U;
      if (reg > 20U) reg = 20U;
      *value = reg;
      return true;
    case MENU_STOP_CURRENT:
      if (!sw6236_read_reg(IP2366_REG_STOP_RECHG, &reg)) return false;
      reg = (uint8_t)((reg >> 4) & 0x0FU);
      if (reg < 1U) reg = 1U;
      *value = reg;
      return true;
    case MENU_RECHARGE_THRESHOLD:
      if (!sw6236_read_reg(IP2366_REG_STOP_RECHG, &reg)) return false;
      reg = (uint8_t)((reg >> 2) & 0x03U);
      *value = (reg == 0U) ? 0U : (uint8_t)(reg - 1U);
      return true;
    case MENU_LOW_BATTERY_VOLTAGE:
      if (!sw6236_read_reg(IP2366_REG_LOW_BAT, &reg)) return false;
      *value = (uint8_t)((reg >> 5) & 0x07U);
      return true;
    case MENU_OUTPUT_ENABLE:
      if (!sw6236_read_reg(IP2366_REG_OUTPUT_EN, &reg)) return false;
      *value = (reg & 0x80U) ? 1U : 0U;
      return true;
    case MENU_OUTPUT_POWER:
      if (!sw6236_read_reg(IP2366_REG_OUTPUT_PWR, &reg)) return false;
      reg = (uint8_t)((reg >> 5) & 0x07U);
      if (reg > 5U) reg = 5U;
      *value = reg;
      return true;
    case MENU_TYPEC_MODE:
      if (!sw6236_read_reg(IP2366_REG_TYPEC_MODE, &reg)) return false;
      reg = (uint8_t)((reg >> 6) & 0x03U);
      *value = (reg == 0U) ? 0U : ((reg == 1U) ? 1U : 2U);
      return true;
    default:
      return false;
  }
}

static bool ip2366_write_menu_value(uint8_t index, uint8_t value)
{
  uint8_t reg;
  uint8_t current_limit;

  switch (index) {
    case MENU_BATTERY_VOLTAGE:
      if (value > BATTERY_FULL_VOLTAGE_MAX_CODE) value = BATTERY_FULL_VOLTAGE_MAX_CODE;
      return sw6236_write_reg(IP2366_REG_VSET, value);
    case MENU_BATTERY_CURRENT:
      if (!ip2366_read_menu_value(MENU_STOP_CURRENT, &current_limit)) current_limit = 2U;
      if (value < (uint8_t)(((uint16_t)current_limit + 1U) / 2U)) value = (uint8_t)(((uint16_t)current_limit + 1U) / 2U);
      if (value < 10U) value = 10U;
      if (value > 97U) value = 97U;
      return sw6236_write_reg(IP2366_REG_ISET, value);
    case MENU_TRICKLE_CURRENT:
      if (value < 2U) value = 2U;
      if (value > 20U) value = 20U;
      return sw6236_write_reg(IP2366_REG_TRICKLE, value);
    case MENU_STOP_CURRENT:
      if (!ip2366_read_menu_value(MENU_BATTERY_CURRENT, &current_limit)) current_limit = 97U;
      if (value < 1U) value = 1U;
      if (value > 15U) value = 15U;
      if ((uint16_t)value * 50U > (uint16_t)current_limit * 100U) value = (uint8_t)(((uint16_t)current_limit * 2U) / 1U);
      if (value > 15U) value = 15U;
      return sw6236_update_bits(IP2366_REG_STOP_RECHG, 0xF0U, (uint8_t)(value << 4));
    case MENU_RECHARGE_THRESHOLD:
      if (value > 2U) value = 2U;
      return sw6236_update_bits(IP2366_REG_STOP_RECHG, 0x0CU, (uint8_t)((value + 1U) << 2));
    case MENU_LOW_BATTERY_VOLTAGE:
      return sw6236_update_bits(IP2366_REG_LOW_BAT, 0xE0U, (uint8_t)((value & 0x07U) << 5));
    case MENU_OUTPUT_ENABLE:
      return sw6236_update_bits(IP2366_REG_OUTPUT_EN, 0x80U, value ? 0x80U : 0x00U);
    case MENU_OUTPUT_POWER:
      if (value > 5U) value = 5U;
      return sw6236_update_bits(IP2366_REG_OUTPUT_PWR, 0xE0U, (uint8_t)(value << 5));
    case MENU_TYPEC_MODE:
      reg = (value == 0U) ? 0U : ((value == 1U) ? 1U : 3U);
      return sw6236_update_bits(IP2366_REG_TYPEC_MODE, 0xC0U, (uint8_t)(reg << 6));
    default:
      return false;
  }
}

static void apply_ip2366_default_settings(void)
{
  (void)ip2366_write_menu_value(MENU_BATTERY_CURRENT, 97U);
  (void)ip2366_write_menu_value(MENU_TRICKLE_CURRENT, 4U);
  (void)ip2366_write_menu_value(MENU_STOP_CURRENT, 2U);
  (void)ip2366_write_menu_value(MENU_RECHARGE_THRESHOLD, 1U);
  (void)ip2366_write_menu_value(MENU_LOW_BATTERY_VOLTAGE, 4U);
  (void)ip2366_write_menu_value(MENU_OUTPUT_ENABLE, 1U);
  (void)ip2366_write_menu_value(MENU_TYPEC_MODE, 2U);
}

static void format_menu_setting_value(const app_t *app, uint8_t index, uint8_t value, char *buf, uint32_t len)
{
  switch (index) {
    case MENU_BATTERY_SERIES:
      format_battery_series(buf, len, value);
      break;
    case MENU_BATTERY_CAPACITY:
      format_capacity_mah(buf, len, (uint32_t)value * 1000UL);
      break;
    case MENU_BATTERY_PERCENT_MODE:
      cache_copy(buf, len, value == BATTERY_PERCENT_MODE_VOLTAGE ? tr(app, "Voltage", "电压比例") : tr(app, "Gauge", "库仑计"));
      break;
    case MENU_BATTERY_VOLTAGE:
      format_battery_full_voltage(buf, len, value);
      break;
    case MENU_BATTERY_CURRENT:
      format_a10(buf, len, value);
      break;
    case MENU_TRICKLE_CURRENT:
    case MENU_STOP_CURRENT:
      format_ma(buf, len, (uint16_t)value * 50U);
      break;
    case MENU_RECHARGE_THRESHOLD:
      if (value > 2U) value = 1U;
      cache_copy(buf, len, k_recharge_text[value]);
      break;
    case MENU_LOW_BATTERY_VOLTAGE:
      cache_copy(buf, len, k_low_battery_text[value & 0x07U]);
      break;
    case MENU_OUTPUT_ENABLE:
      cache_copy(buf, len, value ? "ON" : "OFF");
      break;
    case MENU_OUTPUT_POWER:
      if (value > 5U) value = 5U;
      cache_copy(buf, len, k_output_power_text[value]);
      break;
    case MENU_TYPEC_MODE:
      if (value > 2U) value = 2U;
      cache_copy(buf, len, value == 0U ? "UFP" : (value == 1U ? "DFP" : "DRP"));
      break;
    case MENU_FAN_TEMP:
      format_temp_c_value(buf, len, value, false);
      break;
    case MENU_LANGUAGE:
      cache_copy(buf, len, value == LANG_CHINESE ? "ZH" : "EN");
      break;
    case MENU_BRIGHTNESS:
      format_percent_value(buf, len, (uint8_t)((value + 1U) * 20U));
      break;
    case MENU_SLEEP_TIMEOUT:
      format_sleep_timeout_value(buf, len, value);
      break;
    case MENU_TEMP_CALIBRATION:
      format_temp_offset_value(buf, len, (int8_t)((int16_t)value - 100));
      break;
    case MENU_SCREEN_ROTATION:
      cache_copy(buf, len, value ? "180deg" : "0deg");
      break;
    case MENU_FACTORY_RESET:
      cache_copy(buf, len, tr(app, "Hold", "长按"));
      break;
    default:
      cache_copy(buf, len, "");
      break;
  }
}

static const char *typec_mode_note(const app_t *app, uint8_t value)
{
  if (value == 0U) return tr(app, "IN only", "仅输入");
  if (value == 1U) return tr(app, "OUT only", "仅输出");
  return tr(app, "IN/OUT", "可充可放");
}
static void adjust_menu_setting(app_t *app, int8_t direction)
{
  uint8_t min = 0U;
  uint8_t max = 0U;

  switch (app->edit_menu_index) {
    case MENU_BATTERY_SERIES: min = 2U; max = 6U; break;
    case MENU_BATTERY_VOLTAGE: max = BATTERY_FULL_VOLTAGE_MAX_CODE; break;
    case MENU_BATTERY_CAPACITY: min = 1U; max = 250U; break;
    case MENU_BATTERY_PERCENT_MODE: max = 1U; break;
    case MENU_BATTERY_CURRENT: min = 10U; max = 97U; break;
    case MENU_TRICKLE_CURRENT: min = 2U; max = 20U; break;
    case MENU_STOP_CURRENT: min = 1U; max = 15U; break;
    case MENU_RECHARGE_THRESHOLD: max = 2U; break;
    case MENU_LOW_BATTERY_VOLTAGE: max = 7U; break;
    case MENU_OUTPUT_ENABLE: max = 1U; break;
    case MENU_OUTPUT_POWER: max = 5U; break;
    case MENU_TYPEC_MODE: max = 2U; break;
    case MENU_FAN_TEMP: max = 100U; break;
    case MENU_BRIGHTNESS: max = 4U; break;
    case MENU_SLEEP_TIMEOUT: max = SLEEP_TIMEOUT_MAX_STEP; break;
    case MENU_TEMP_CALIBRATION: max = 200U; break;
    case MENU_SCREEN_ROTATION: max = 1U; break;
    default: break;
  }

  if (direction < 0) {
    if (app->edit_value > min) {
      app->edit_value--;
      app->dirty = true;
    }
  } else if (direction > 0) {
    if (app->edit_value < max) {
      app->edit_value++;
      app->dirty = true;
    }
  }
}

static const char *menu_label(const app_t *app, uint8_t index)
{
  switch (index) {
    case MENU_BATTERY_SERIES: return tr(app, "Cells", "电池串数");
    case MENU_OUTPUT_POWER: return tr(app, "In/Out Power", "输入输出功率");
    case MENU_BATTERY_CAPACITY: return tr(app, "Capacity", "电池容量");
    case MENU_BATTERY_PERCENT_MODE: return tr(app, "Batt Percent", "电量显示方式");
    case MENU_BATTERY_VOLTAGE: return tr(app, "Full Volt", "满电电压");
    case MENU_BATTERY_CURRENT: return tr(app, "Batt Curr", "电池端电流");
    case MENU_TRICKLE_CURRENT: return tr(app, "Trickle", "涓流充电电流");
    case MENU_STOP_CURRENT: return tr(app, "Stop Curr", "停充电流");
    case MENU_RECHARGE_THRESHOLD: return tr(app, "Recharge", "再充阈值");
    case MENU_LOW_BATTERY_VOLTAGE: return tr(app, "Low Volt", "电池低电压");
    case MENU_OUTPUT_ENABLE: return tr(app, "Output En", "输出使能");
    case MENU_TYPEC_MODE: return tr(app, "C Mode", "C口模式");
    case MENU_FAN_TEMP: return tr(app, "Fan Temp", "风扇温度");
    case MENU_BRIGHTNESS: return tr(app, "Brightness", "屏幕亮度");
    case MENU_SLEEP_TIMEOUT: return tr(app, "Auto Sleep", "自动息屏");
    case MENU_TEMP_CALIBRATION: return tr(app, "Temp Offset", "温度校准");
    case MENU_SCREEN_ROTATION: return tr(app, "Rotation", "屏幕方向");
    case MENU_FACTORY_RESET: return tr(app, "Factory", "恢复出厂");
    case MENU_LANGUAGE: return tr(app, "Language", "语言");
    case MENU_ABOUT: return tr(app, "About", "关于");
    default: return "";
  }
}
static const char *menu_value(const app_t *app, uint8_t index, char *buf, uint32_t len)
{
  uint8_t value;

  switch (index) {
    case MENU_BATTERY_SERIES:
      format_battery_series_status(app, buf, len);
      return buf;
    case MENU_BATTERY_CAPACITY:
      format_capacity_mah(buf, len, app->set_capacity_mah);
      return buf;
    case MENU_BATTERY_PERCENT_MODE:
      format_menu_setting_value(app, index, app->battery_percent_mode, buf, len);
      return buf;
    case MENU_BATTERY_VOLTAGE:
    case MENU_BATTERY_CURRENT:
    case MENU_TRICKLE_CURRENT:
    case MENU_STOP_CURRENT:
    case MENU_RECHARGE_THRESHOLD:
    case MENU_LOW_BATTERY_VOLTAGE:
    case MENU_OUTPUT_ENABLE:
    case MENU_OUTPUT_POWER:
    case MENU_TYPEC_MODE:
      if (ip2366_read_menu_value(index, &value)) {
        format_menu_setting_value(app, index, value, buf, len);
      } else {
        cache_copy(buf, len, "--");
      }
      return buf;
    case MENU_FAN_TEMP:
      format_menu_setting_value(app, index, app->fan_on_temp_c, buf, len);
      return buf;
    case MENU_BRIGHTNESS:
      format_percent_value(buf, len, app->brightness_percent);
      return buf;
    case MENU_SLEEP_TIMEOUT:
      format_sleep_timeout_value(buf, len, app->sleep_timeout);
      return buf;
    case MENU_TEMP_CALIBRATION:
      format_temp_offset_value(buf, len, app->temp_offset_c10);
      return buf;
    case MENU_SCREEN_ROTATION:
      cache_copy(buf, len, app->screen_rotated_180 ? "180deg" : "0deg");
      return buf;
    case MENU_FACTORY_RESET:
      return tr(app, "Hold", "长按");
    case MENU_LANGUAGE:
      return app->language == LANG_CHINESE ? "ZH" : "EN";
    default:
      return "";
  }
}

static bool menu_is_external_read(uint8_t index)
{
  (void)index;
  return false;
}

static bool menu_is_ip2366_edit(uint8_t index)
{
  return index == MENU_OUTPUT_POWER ||
         index == MENU_BATTERY_VOLTAGE ||
         index == MENU_BATTERY_CURRENT ||
         index == MENU_TRICKLE_CURRENT ||
         index == MENU_STOP_CURRENT ||
         index == MENU_RECHARGE_THRESHOLD ||
         index == MENU_LOW_BATTERY_VOLTAGE ||
         index == MENU_OUTPUT_ENABLE ||
         index == MENU_TYPEC_MODE;
}

static bool menu_is_local_edit(uint8_t index)
{
  return index == MENU_BATTERY_SERIES ||
         index == MENU_BATTERY_CAPACITY ||
         index == MENU_BATTERY_PERCENT_MODE ||
         index == MENU_BRIGHTNESS ||
         index == MENU_SLEEP_TIMEOUT ||
         index == MENU_TEMP_CALIBRATION ||
         index == MENU_SCREEN_ROTATION;
}

static void begin_local_edit(app_t *app, uint8_t index)
{
  app->edit_menu_index = index;
  switch (index) {
    case MENU_BATTERY_SERIES:
      app->edit_value = configured_battery_series(app);
      if (app->edit_value < 2U || app->edit_value > 6U) app->edit_value = 2U;
      app->edit_current = app->edit_value;
      break;
    case MENU_BATTERY_CAPACITY:
      app->edit_value = (uint8_t)(app->set_capacity_mah / 1000UL);
      if (app->edit_value < 1U) app->edit_value = 10U;
      app->edit_current = app->edit_value;
      break;
    case MENU_BATTERY_PERCENT_MODE:
      app->edit_value = app->battery_percent_mode > 1U ? 0U : app->battery_percent_mode;
      app->edit_current = app->edit_value;
      break;
    case MENU_BRIGHTNESS:
      app->edit_value = (uint8_t)((app->brightness_percent / 20U) - 1U);
      if (app->edit_value > 4U) app->edit_value = 4U;
      app->edit_current = app->edit_value;
      break;
    case MENU_SLEEP_TIMEOUT:
      app->edit_value = app->sleep_timeout > SLEEP_TIMEOUT_MAX_STEP ? SLEEP_TIMEOUT_OFF : app->sleep_timeout;
      app->edit_current = app->edit_value;
      break;
    case MENU_TEMP_CALIBRATION:
      app->edit_value = (uint8_t)((int16_t)app->temp_offset_c10 + 100);
      app->edit_current = app->edit_value;
      break;
    case MENU_SCREEN_ROTATION:
      app->edit_value = app->screen_rotated_180 ? 1U : 0U;
      app->edit_current = app->edit_value;
      break;
    default:
      app->edit_value = 0U;
      app->edit_current = 0U;
      break;
  }
  app->screen = SCREEN_EDIT_SETTING;
}

static void apply_local_edit(app_t *app)
{
  switch (app->edit_menu_index) {
    case MENU_BATTERY_SERIES:
      if (app->edit_value >= 2U && app->edit_value <= 6U) {
        app->battery_series = app->edit_value;
        app->battery_series_auto = app->edit_value;
        app->voltage_percent_filter_valid = false;
        battery_gauge_set_percent(app, estimate_percent_from_vbat(app->metrics.vbat_mv,
                                                                  app->set_battery_type,
                                                                  app->battery_series));
        app->gauge_was_full = battery_full_detected(&app->metrics);
        app->gauge_was_empty = battery_empty_detected(app, &app->metrics);
        battery_apply_display_percent(app);
      }
      break;
    case MENU_BATTERY_CAPACITY:
      app->set_capacity_mah = (uint32_t)app->edit_value * 1000UL;
      app->gauge_capacity_cmah = app->set_capacity_mah * 100UL;
      if (app->gauge_remaining_cmah > app->gauge_capacity_cmah) {
        app->gauge_remaining_cmah = app->gauge_capacity_cmah;
      }
      break;
    case MENU_BATTERY_PERCENT_MODE:
      app->battery_percent_mode = app->edit_value > 1U ? BATTERY_PERCENT_MODE_GAUGE : app->edit_value;
      battery_apply_display_percent(app);
      break;
    case MENU_BRIGHTNESS:
      app->brightness_percent = (uint8_t)((app->edit_value + 1U) * 20U);
      st7735_set_brightness(app->brightness_percent);
      g_last_screen = 0xFFU;
      break;
    case MENU_SLEEP_TIMEOUT:
      app->sleep_timeout = app->edit_value > SLEEP_TIMEOUT_MAX_STEP ? SLEEP_TIMEOUT_OFF : app->edit_value;
      break;
    case MENU_TEMP_CALIBRATION:
      app->temp_offset_c10 = (int8_t)((int16_t)app->edit_value - 100);
      break;
    case MENU_SCREEN_ROTATION:
      app->screen_rotated_180 = app->edit_value ? 1U : 0U;
      st7735_set_rotation(app->screen_rotated_180 ? 2U : 1U);
      st7735_set_offsets(0, 34);
      g_last_screen = 0xFFU;
      break;
    default:
      break;
  }
  save_settings(app);
}

static void factory_reset_app(app_t *app)
{
  app_settings_t defaults;

  app_settings_defaults(&defaults);
  apply_ip2366_default_settings();
  refresh_battery_type(app);

  app->set_capacity_mah = defaults.capacity_mah;
  app->set_battery_type = defaults.battery_type;
  app->current_battery_type = defaults.battery_type;
  app->fan_on_temp_c = defaults.fan_on_temp_c;
  app->fan_off_temp_c = defaults.fan_off_temp_c;
  app->language = defaults.language == LANG_CHINESE ? LANG_CHINESE : LANG_ENGLISH;
  app->language_select = (uint8_t)app->language;
  app->screen_rotated_180 = defaults.screen_rotation;
  app->brightness_percent = defaults.brightness_percent;
  app->sleep_timeout = defaults.sleep_timeout;
  app->temp_offset_c10 = defaults.temp_offset_c10;
  app->battery_percent_mode = defaults.battery_percent_mode;
  app->battery_series = defaults.battery_series;
  app->battery_series_auto = 0U;
  update_auto_battery_series(app);
  app->voltage_percent_filter_valid = false;
  app->capacity_learned = false;
  app->gauge_initialized = false;
  app->gauge_remaining_cmah = 0U;
  app->gauge_capacity_cmah = defaults.gauge_capacity_mah * 100UL;
  app->gauge_cycle_cmah = 0U;
  app->gauge_rem_mams = 0;
  app->usb_capacity_cmah = 0;
  app->usb_capacity_rem_mams = 0;
  app->display_sleeping = false;
  app->last_data_screen = 0U;
  st7735_set_brightness(app->brightness_percent);
  st7735_set_rotation(app->screen_rotated_180 ? 2U : 1U);
  st7735_set_offsets(0, 34);
  battery_gauge_init(app, &defaults);
  save_settings(app);
  g_last_screen = 0xFFU;
}

static void draw_menu_row(app_t *app, uint8_t item, uint8_t visible)
{
  char value[24];
  uint16_t y = (uint16_t)(36U + visible * 25U);
  bool selected = item == app->menu_index;
  uint16_t bg = selected ? RGB565(18, 42, 48) : COLOR_BLACK;
  uint16_t label_fg = selected ? COLOR_WHITE : COLOR_GRAY;
  uint16_t value_fg = selected ? COLOR_YELLOW : (menu_is_external_read(item) ? COLOR_ORANGE : COLOR_WHITE);
  uint16_t mark = menu_is_external_read(item) ? COLOR_ORANGE :
                  (menu_is_local_edit(item) ? COLOR_GREEN :
                   (menu_is_ip2366_edit(item) ? COLOR_CYAN : COLOR_PURPLE));

  st7735_fill_rect(14, y, 286, 23, COLOR_BLACK);
  if (selected) {
    st7735_fill_rect(18, y, 278, 23, bg);
  } else {
    st7735_draw_hline(20, (uint16_t)(y + 22U), 272, COLOR_DARK);
  }
  st7735_fill_rect(22, (uint16_t)(y + 5U), 4, 13, mark);
  zh_font_draw_text16_ref(34, (uint16_t)(y + 3U), menu_label(app, item), label_fg, bg);
  draw_menu_value(202, y, value_fg, bg, menu_value(app, item, value, sizeof(value)));
  if (selected) {
    st7735_draw_rect(18, y, 278, 23, COLOR_CYAN);
  }
}

static void draw_menu_scrollbar(uint8_t menu_top)
{
  uint16_t thumb_y;
  uint16_t thumb_h;

  st7735_fill_rect(302, 34, 10, 128, COLOR_BLACK);
  st7735_draw_vline(306, 36, 123, COLOR_DARK);
  thumb_h = (uint16_t)((MENU_VISIBLE_ROWS * 123U) / MENU_COUNT);
  if (thumb_h < 14U) thumb_h = 14U;
  thumb_y = (uint16_t)(36U + ((uint32_t)menu_top * (123U - thumb_h)) / (MENU_COUNT - MENU_VISIBLE_ROWS));
  st7735_fill_rect(304, thumb_y, 5, thumb_h, COLOR_CYAN);
}

static void render_menu(app_t *app, bool full_refresh)
{
  uint8_t visible;
  static uint8_t cache_index = 0xFFU;
  static uint8_t cache_top = 0xFFU;

  if (app->menu_index < app->menu_top) {
    app->menu_top = app->menu_index;
  } else if (app->menu_index >= (uint8_t)(app->menu_top + MENU_VISIBLE_ROWS)) {
    app->menu_top = (uint8_t)(app->menu_index - (MENU_VISIBLE_ROWS - 1U));
  }
  if (app->menu_top > (uint8_t)(MENU_COUNT - MENU_VISIBLE_ROWS)) {
    app->menu_top = MENU_COUNT - MENU_VISIBLE_ROWS;
  }

  if (full_refresh || cache_top == 0xFFU) {
    st7735_fill_screen(COLOR_BLACK);
    draw_147_safe_ring();
    draw_ref_text(18, 11, tr(app, "Menu", "用户菜单"), COLOR_CYAN);
    draw_ref_text(236, 11, tr(app, "S2 OK", "S2确认"), COLOR_GRAY);
    cache_index = 0xFFU;
    cache_top = 0xFFU;
  }

  if (full_refresh || cache_top != app->menu_top) {
    for (visible = 0; visible < MENU_VISIBLE_ROWS; ++visible) {
      draw_menu_row(app, (uint8_t)(app->menu_top + visible), visible);
    }
    draw_menu_scrollbar(app->menu_top);
  } else if (cache_index != app->menu_index) {
    if (cache_index >= app->menu_top && cache_index < (uint8_t)(app->menu_top + MENU_VISIBLE_ROWS)) {
      draw_menu_row(app, cache_index, (uint8_t)(cache_index - app->menu_top));
    }
    if (app->menu_index >= app->menu_top && app->menu_index < (uint8_t)(app->menu_top + MENU_VISIBLE_ROWS)) {
      draw_menu_row(app, app->menu_index, (uint8_t)(app->menu_index - app->menu_top));
    }
  }
  cache_index = app->menu_index;
  cache_top = app->menu_top;
}
static void render_about(app_t *app)
{
  st7735_fill_screen(COLOR_BLACK);
  draw_147_safe_ring();
  draw_ref_text(18, 14, tr(app, "IP2366 1.47 LCD FM V0.0.1", "IP2366 1.47寸彩屏 FM V0.0.1"), COLOR_CYAN);
  draw_ref_text(18, 42, tr(app, "Scan QR for board info", "关于主板说明请扫主板上的二维码查看"), COLOR_WHITE);
  draw_ref_text(18, 70, tr(app, "FW updates QQ 438586091", "后续固件升级请加QQ群438586091"), COLOR_GREEN);
  draw_ref_text(18, 96, "Technical support for", COLOR_GRAY);
  draw_ref_text(18, 114, "overseas users: please", COLOR_GRAY);
  draw_ref_text(18, 132, "contact us via WeChat ID:", COLOR_GRAY);
  draw_ref_text(18, 150, "my0708bangong", COLOR_YELLOW);
}

static void render_factory_reset(app_t *app)
{
  st7735_fill_screen(COLOR_BLACK);
  draw_147_safe_ring();
  draw_ref_text(18, 14, tr(app, "Factory Reset", "恢复出厂设置"), COLOR_RED);
  draw_ref_text(24, 44, tr(app, "Hold S2 to reset", "长按S2确认恢复"), COLOR_WHITE);
  draw_ref_text(24, 72, tr(app, "Factory defaults", "恢复到出厂默认设置"), COLOR_ORANGE);
  draw_ref_text(24, 94, tr(app, "Cells/power/type", "电池节数和输出输入功率"), COLOR_ORANGE);
  draw_ref_text(24, 116, tr(app, "Use external resistors", "电池类型按外置电阻重新配置"), COLOR_ORANGE);
  draw_ref_text(24, 148, tr(app, "S2 click: back", "短按S2返回"), COLOR_GRAY);
}
static void render_dual_choice_screen(app_t *app,
                                      bool full_refresh,
                                      const char *title,
                                      const char *left_label,
                                      const char *right_label,
                                      uint16_t left_outline,
                                      uint16_t right_outline,
                                      uint8_t selected,
                                      const char *status_text,
                                      uint16_t status_color,
                                      uint8_t *cache_select,
                                      bool *cache_status)
{
  uint16_t left_bg = selected ? left_outline : COLOR_BLACK;
  uint16_t left_fg = selected ? COLOR_BLACK : left_outline;
  uint16_t right_bg = selected ? COLOR_BLACK : right_outline;
  uint16_t right_fg = selected ? right_outline : COLOR_BLACK;

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    draw_147_safe_ring();
    draw_ref_text(18, 13, title, COLOR_CYAN);
    draw_round_rect(30, 52, 104, 42, left_outline);
    draw_round_rect(182, 52, 104, 42, right_outline);
    draw_ref_text(28, 128, tr(app, "S1/S3 Select  S2 OK", "S1/S3选择  S2确认"), COLOR_GRAY);
    *cache_select = 0xFFU;
    *cache_status = false;
  }

  if (full_refresh || *cache_select != selected) {
    st7735_fill_rect(33, 55, 98, 36, left_bg);
    zh_font_draw_text16_ref(48, 65, left_label, left_fg, left_bg);
    st7735_fill_rect(185, 55, 98, 36, right_bg);
    zh_font_draw_text16_ref(206, 65, right_label, right_fg, right_bg);
    *cache_select = selected;
    *cache_status = false;
  }

  if (full_refresh || !*cache_status) {
    st7735_fill_rect(70, 105, 190, 18, COLOR_BLACK);
    draw_ref_text(94, 105, status_text, status_color);
    *cache_status = true;
  }
}

static void render_language(app_t *app, bool full_refresh)
{
  static uint8_t cache_select = 0xFFU;
  static bool cache_status = false;
  const char *right_label = tr(app, "Chinese", "中文");
  const char *status_text = app->language_select == LANG_ENGLISH ? "English UI" :
                            tr(app, "Chinese UI", "中文界面");

  render_dual_choice_screen(app, full_refresh,
                            tr(app, "Language", "语言"),
                            "English",
                            right_label,
                            COLOR_GREEN, COLOR_CYAN,
                            app->language_select == LANG_CHINESE ? 0U : 1U,
                            status_text,
                            app->language_select == LANG_ENGLISH ? COLOR_GREEN : COLOR_CYAN,
                            &cache_select, &cache_status);
}

static void render_battery_percent_mode(app_t *app, bool full_refresh)
{
  static uint8_t cache_select = 0xFFU;
  static bool cache_status = false;
  const char *status_text = app->edit_value == BATTERY_PERCENT_MODE_VOLTAGE ? "VBAT %" : "Gauge %";

  render_dual_choice_screen(app, full_refresh,
                            tr(app, "Batt Percent", "电量显示方式"),
                            tr(app, "Gauge", "库仑计"),
                            tr(app, "Voltage", "电压比例"),
                            COLOR_GREEN, COLOR_CYAN,
                            app->edit_value == BATTERY_PERCENT_MODE_GAUGE ? 1U : 0U,
                            status_text,
                            app->edit_value == BATTERY_PERCENT_MODE_GAUGE ? COLOR_GREEN : COLOR_CYAN,
                            &cache_select, &cache_status);
}

static void adjust_fan_setting(app_t *app, int8_t direction)
{
  uint8_t *value = app->edit_sub_index == 0U ? &app->edit_value : &app->edit_current;

  if (direction < 0) {
    if (*value > 0U) {
      (*value)--;
      app->dirty = true;
    }
  } else if (direction > 0) {
    if (*value < 100U) {
      (*value)++;
      app->dirty = true;
    }
  }
}

static uint8_t edit_ascii_scale(const char *text)
{
  uint8_t len = (uint8_t)strlen(text);

  if (len <= 9U) return 4U;
  if (len <= 12U) return 3U;
  return 2U;
}

static uint8_t edit_value_slots(uint8_t index)
{
  switch (index) {
    case MENU_BATTERY_SERIES: return 8U;
    case MENU_BATTERY_CAPACITY: return 9U;
    case MENU_BATTERY_PERCENT_MODE: return 8U;
    case MENU_BATTERY_VOLTAGE: return 5U;
    case MENU_BATTERY_CURRENT: return 4U;
    case MENU_TRICKLE_CURRENT: return 6U;
    case MENU_STOP_CURRENT: return 5U;
    case MENU_RECHARGE_THRESHOLD: return 8U;
    case MENU_LOW_BATTERY_VOLTAGE: return 7U;
    case MENU_OUTPUT_ENABLE: return 3U;
    case MENU_TYPEC_MODE: return 3U;
    case MENU_BRIGHTNESS: return 4U;
    case MENU_SLEEP_TIMEOUT: return 4U;
    case MENU_TEMP_CALIBRATION: return 6U;
    case MENU_SCREEN_ROTATION: return 6U;
    default: return 8U;
  }
}

static void make_right_aligned_slots(char *dst, uint8_t slots, const char *text)
{
  uint8_t len = (uint8_t)strlen(text);
  uint8_t copy_len = len > slots ? slots : len;
  uint8_t start = (uint8_t)(slots - copy_len);

  memset(dst, ' ', slots);
  memcpy(dst + start, text + (len - copy_len), copy_len);
  dst[slots] = '\0';
}

static void draw_ascii_slots_delta(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   uint8_t slots, const char *old_text, const char *new_text,
                                   uint16_t color, uint16_t bg, uint8_t scale,
                                   bool full_refresh)
{
  char old_slots[16];
  char new_slots[16];
  uint16_t cell_w = (uint16_t)(6U * scale);
  uint16_t text_h = (uint16_t)(8U * scale);
  uint16_t block_w;
  uint16_t draw_x;
  uint16_t value_y = (uint16_t)(y + ((h - text_h) / 2U));
  uint8_t i;

  if (slots > 15U) slots = 15U;
  block_w = (uint16_t)((uint16_t)slots * cell_w);
  draw_x = x;
  if (w > block_w) {
    draw_x = (uint16_t)(x + ((w - block_w) / 2U));
  }

  if (full_refresh || old_text[0] == '\0') {
    st7735_fill_rect(x, y, w, h, bg);
    memset(old_slots, ' ', slots);
    old_slots[slots] = '\0';
  } else {
    make_right_aligned_slots(old_slots, slots, old_text);
  }
  make_right_aligned_slots(new_slots, slots, new_text);

  for (i = 0U; i < slots; ++i) {
    if (full_refresh || old_slots[i] != new_slots[i]) {
      uint16_t cx = (uint16_t)(draw_x + (uint16_t)i * cell_w);
      st7735_fill_rect(cx, value_y, cell_w, text_h, bg);
      if (new_slots[i] != ' ') {
        char one[2] = {new_slots[i], '\0'};
        st7735_draw_text(cx, value_y, one, color, bg, scale);
      }
    }
  }
}

static void draw_setting_value_panel(uint8_t index, const char *old_value,
                                     const char *new_value, bool full_refresh)
{
  uint8_t scale = edit_ascii_scale(new_value);
  uint8_t slots = index == MENU_BATTERY_SERIES ? 2U : edit_value_slots(index);

  draw_ascii_slots_delta(52, 52, 216, 40, slots, old_value, new_value,
                         COLOR_WHITE, COLOR_BLACK, scale, full_refresh);
}

static void draw_setting_arrows(void)
{
  st7735_draw_text(32, 68, "<", COLOR_GRAY, COLOR_BLACK, 2);
  st7735_draw_text(280, 68, ">", COLOR_GRAY, COLOR_BLACK, 2);
}

static void format_temp_digits(char *buf, uint32_t len, uint8_t temp_c)
{
  uint32_t pos = append_u32(buf, len, 0, temp_c);
  finish_text(buf, len, pos);
}

static void draw_temp_value_delta(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                  const char *old_digits, const char *new_digits,
                                  uint16_t color, uint16_t bg, bool full_refresh)
{
  uint16_t block_w = (uint16_t)(3U * 18U + 16U);
  uint16_t draw_x = (uint16_t)(x + ((w - block_w) / 2U));

  draw_ascii_slots_delta(draw_x, y, 54, h, 3U, old_digits, new_digits,
                         color, bg, 3U, full_refresh);
  if (full_refresh) {
    zh_font_draw_text16_ref((uint16_t)(draw_x + 58U), (uint16_t)(y + 9U), "\xE2\x84\x83", color, bg);
  }
}

static void copy_without_degree(char *dst, uint32_t len, const char *src)
{
  uint32_t pos = 0U;

  if (len == 0U) return;
  while (*src != '\0' && ((uint8_t)*src) < 0x80U && pos < (len - 1U)) {
    dst[pos++] = *src++;
  }
  dst[pos] = '\0';
}

static void draw_temp_offset_value_delta(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                         const char *old_value, const char *new_value,
                                         uint16_t color, uint16_t bg, uint8_t scale,
                                         bool full_refresh)
{
  char old_digits[10];
  char new_digits[10];
  uint16_t cell_w = (uint16_t)(6U * scale);
  uint16_t block_w = (uint16_t)(6U * cell_w + 16U);
  uint16_t draw_x = (uint16_t)(x + ((w - block_w) / 2U));
  uint16_t unit_y = (uint16_t)(y + ((h - 16U) / 2U));

  copy_without_degree(old_digits, sizeof(old_digits), old_value);
  copy_without_degree(new_digits, sizeof(new_digits), new_value);
  draw_ascii_slots_delta(draw_x, y, (uint16_t)(6U * cell_w), h, 6U, old_digits, new_digits,
                         color, bg, scale, full_refresh);
  if (full_refresh) {
    zh_font_draw_text16_ref((uint16_t)(draw_x + 6U * cell_w + 4U), unit_y, "\xE2\x84\x83", color, bg);
  }
}

static void render_fan_setting(app_t *app, bool full_refresh)
{
  static uint8_t cache_on = 0xFFU;
  static uint8_t cache_off = 0xFFU;
  static uint8_t cache_select = 0xFFU;
  static char cache_on_text[8];
  static char cache_off_text[8];
  char text[8];
  uint16_t on_border = app->edit_sub_index == 0U ? COLOR_GREEN : COLOR_DARK;
  uint16_t off_border = app->edit_sub_index == 1U ? COLOR_CYAN : COLOR_DARK;
  uint16_t on_bg = app->edit_sub_index == 0U ? RGB565(4, 42, 22) : COLOR_BLACK;
  uint16_t off_bg = app->edit_sub_index == 1U ? RGB565(18, 42, 48) : COLOR_BLACK;

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    draw_147_safe_ring();
    draw_ref_text(18, 13, menu_label(app, MENU_FAN_TEMP), COLOR_CYAN);
    draw_ref_text(24, 124, "S1/S3 +/-", COLOR_GRAY);
    draw_ref_text(154, 124, tr(app, "S2 Switch", "S2切换"), COLOR_GRAY);
    draw_ref_text(228, 124, tr(app, "Hold Save", "长按保存"), COLOR_GRAY);
    cache_on = 0xFFU;
    cache_off = 0xFFU;
    cache_select = 0xFFU;
    cache_on_text[0] = '\0';
    cache_off_text[0] = '\0';
  }

  if (full_refresh || cache_select != app->edit_sub_index) {
    st7735_fill_rect(24, 42, 128, 72, on_bg);
    draw_round_rect(24, 42, 128, 72, on_border);
    draw_ref_text(42, 50, tr(app, "On", "启动"), app->edit_sub_index == 0U ? COLOR_GREEN : COLOR_GRAY);

    st7735_fill_rect(168, 42, 128, 72, off_bg);
    draw_round_rect(168, 42, 128, 72, off_border);
    draw_ref_text(186, 50, tr(app, "Off", "关闭"), app->edit_sub_index == 1U ? COLOR_CYAN : COLOR_GRAY);
    cache_on = 0xFFU;
    cache_off = 0xFFU;
    cache_on_text[0] = '\0';
    cache_off_text[0] = '\0';
    cache_select = app->edit_sub_index;
  }

  if (full_refresh || cache_on != app->edit_value) {
    format_temp_digits(text, sizeof(text), app->edit_value);
    draw_temp_value_delta(34, 74, 108, 34, cache_on_text, text,
                          COLOR_GREEN, on_bg, full_refresh || cache_on_text[0] == '\0');
    cache_copy(cache_on_text, sizeof(cache_on_text), text);
    cache_on = app->edit_value;
  }
  if (full_refresh || cache_off != app->edit_current) {
    format_temp_digits(text, sizeof(text), app->edit_current);
    draw_temp_value_delta(178, 74, 108, 34, cache_off_text, text,
                          COLOR_CYAN, off_bg, full_refresh || cache_off_text[0] == '\0');
    cache_copy(cache_off_text, sizeof(cache_off_text), text);
    cache_off = app->edit_current;
  }
}

static void draw_output_power_option(uint8_t i, uint8_t selected_value, uint8_t current_value)
{
  uint16_t x = (uint16_t)(24U + (i % 3U) * 96U);
  uint16_t y = (uint16_t)(42U + (i / 3U) * 38U);
  bool selected = i == selected_value;
  bool active = i == current_value;
  uint16_t border = active ? COLOR_GREEN : (selected ? COLOR_CYAN : COLOR_DARK);
  uint16_t bg = active ? RGB565(4, 42, 22) : (selected ? RGB565(18, 42, 48) : COLOR_BLACK);
  uint16_t fg = active ? COLOR_GREEN : (selected ? COLOR_YELLOW : COLOR_WHITE);

  st7735_fill_rect(x, y, 80, 30, bg);
  st7735_draw_rect(x, y, 80, 30, border);
  st7735_draw_text((uint16_t)(x + 16U), (uint16_t)(y + 7U), k_output_power_text[i], fg, bg, 2);
}

static void render_output_power_setting(app_t *app, bool full_refresh)
{
  static uint8_t cache_select = 0xFFU;
  static uint8_t cache_current = 0xFFU;
  uint8_t i;
  uint8_t current = app->edit_current;
  char current_text[24];

  if (ip2366_read_menu_value(MENU_OUTPUT_POWER, &current)) {
    app->edit_current = current;
  } else {
    current = 0xFFU;
  }

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    draw_147_safe_ring();
    draw_ref_text(18, 13, menu_label(app, MENU_OUTPUT_POWER), COLOR_CYAN);
    draw_ref_text(24, 125, tr(app, "Current Power", "当前功率"), COLOR_GRAY);
    draw_ref_text(24, 148, tr(app, "S1/S3 Select  S2 OK  Hold Back", "S1/S3选择  S2确认  长按返回"), COLOR_GRAY);
    cache_select = 0xFFU;
    cache_current = 0xFFU;
  }

  if (full_refresh || cache_select == 0xFFU || cache_current == 0xFFU) {
    for (i = 0U; i < 6U; ++i) {
      draw_output_power_option(i, app->edit_value, current);
    }
  } else if (cache_select != app->edit_value || cache_current != current) {
    if (cache_select < 6U) {
      draw_output_power_option(cache_select, app->edit_value, current);
    }
    if (app->edit_value < 6U && app->edit_value != cache_select) {
      draw_output_power_option(app->edit_value, app->edit_value, current);
    }
    if (cache_current < 6U && cache_current != cache_select && cache_current != app->edit_value) {
      draw_output_power_option(cache_current, app->edit_value, current);
    }
    if (current < 6U && current != cache_select && current != app->edit_value && current != cache_current) {
      draw_output_power_option(current, app->edit_value, current);
    }
  }

  if (full_refresh || cache_current != current) {
    if (current <= 5U) {
      cache_copy(current_text, sizeof(current_text), k_output_power_text[current]);
    } else {
      cache_copy(current_text, sizeof(current_text), "--");
    }
    st7735_fill_rect(116, 123, 94, 20, COLOR_BLACK);
    st7735_draw_text(116, 125, current_text, COLOR_GREEN, COLOR_BLACK, 2);
  }

  cache_select = app->edit_value;
  cache_current = current;
}

static void render_edit_setting(app_t *app, bool full_refresh)
{
  static uint8_t cache_value = 0xFFU;
  static uint8_t cache_current = 0xFFU;
  static char cache_value_text[24];
  static char cache_current_text[24];
  char value[24];
  char current[24];

  if (app->edit_menu_index == MENU_FAN_TEMP) {
    render_fan_setting(app, full_refresh);
    return;
  }
  if (app->edit_menu_index == MENU_OUTPUT_POWER) {
    render_output_power_setting(app, full_refresh);
    return;
  }
  if (app->edit_menu_index == MENU_BATTERY_PERCENT_MODE) {
    render_battery_percent_mode(app, full_refresh);
    return;
  }

  format_menu_setting_value(app, app->edit_menu_index, app->edit_value, value, sizeof(value));
  if (app->edit_menu_index == MENU_BATTERY_SERIES) {
    format_battery_series_status(app, current, sizeof(current));
  } else if (menu_is_local_edit(app->edit_menu_index)) {
    format_menu_setting_value(app, app->edit_menu_index, app->edit_current, current, sizeof(current));
  } else if (ip2366_read_menu_value(app->edit_menu_index, &app->edit_current)) {
    format_menu_setting_value(app, app->edit_menu_index, app->edit_current, current, sizeof(current));
  } else {
    cache_copy(current, sizeof(current), "--");
  }

  if (full_refresh) {
    st7735_fill_screen(COLOR_BLACK);
    draw_147_safe_ring();
    draw_ref_text(18, 13, menu_label(app, app->edit_menu_index), COLOR_CYAN);
    st7735_draw_hline(18, 34, 284, COLOR_DARK);
    draw_round_rect(24, 44, 272, 66, COLOR_DARK);
    draw_setting_arrows();
    draw_round_rect(24, 118, 272, 24, COLOR_DARK);
    draw_ref_text(34, 122, tr(app, "Current", "当前"), COLOR_GRAY);
    draw_ref_text(24, 148,
                  app->edit_menu_index == MENU_BATTERY_SERIES ?
                  tr(app, "S1/S3 choose  S2 lock  Hold Back", "S1/S3选择  S2确认锁定  长按返回") :
                  tr(app, "S1/S3 +/-  S2 OK  Hold Back", "S1/S3 +/-  S2确认  长按返回"), COLOR_GRAY);
    cache_value = 0xFFU;
    cache_current = 0xFFU;
    cache_value_text[0] = '\0';
    cache_current_text[0] = '\0';
  }

  if (full_refresh || cache_value != app->edit_value || strcmp(cache_value_text, value) != 0) {
    if (app->edit_menu_index == MENU_TEMP_CALIBRATION) {
      draw_temp_offset_value_delta(52, 52, 216, 40, cache_value_text, value,
                                   COLOR_WHITE, COLOR_BLACK, 3U, full_refresh);
    } else {
      draw_setting_value_panel(app->edit_menu_index, cache_value_text, value, full_refresh);
    }
    draw_setting_arrows();
    if (app->edit_menu_index == MENU_TYPEC_MODE) {
      st7735_fill_rect(198, 90, 80, 16, COLOR_BLACK);
      draw_ref_text(198, 90, typec_mode_note(app, app->edit_value), COLOR_GREEN);
    }
    cache_copy(cache_value_text, sizeof(cache_value_text), value);
    cache_value = app->edit_value;
  }

  if (full_refresh || cache_current != app->edit_current || strcmp(cache_current_text, current) != 0) {
    if (app->edit_menu_index == MENU_TEMP_CALIBRATION) {
      draw_temp_offset_value_delta(110, 121, 156, 18, cache_current_text, current,
                                   COLOR_GREEN, COLOR_BLACK, 2U, full_refresh);
    } else {
      uint8_t slots = app->edit_menu_index == MENU_BATTERY_SERIES ? (uint8_t)strlen(current) : edit_value_slots(app->edit_menu_index);
      draw_ascii_slots_delta(110, 121, 156, 18, slots,
                             cache_current_text, current, COLOR_GREEN, COLOR_BLACK, 2U, full_refresh);
    }
    cache_copy(cache_current_text, sizeof(cache_current_text), current);
    cache_current = app->edit_current;
  }
}
static void render_first_setup(app_t *app)
{
  st7735_fill_screen(COLOR_BLACK);
  draw_text16(0, 0, tr(app, "First Setup", "首次设置"), COLOR_CYAN);
  draw_text16(0, 22, tr(app, "Hold S2", "请长按S2"), COLOR_YELLOW);
  draw_text16(0, 44, tr(app, "Set Batt Volt", "设置电池电压"), COLOR_WHITE);
}

static void render(app_t *app)
{
  bool full_refresh = g_last_screen != (uint8_t)app->screen;

  switch (app->screen) {
    case SCREEN_PAGE1: render_page1_147(app, full_refresh); break;
    case SCREEN_PAGE2: render_page2_147(app, full_refresh); break;
    case SCREEN_PAGE3: render_page3_147(app, full_refresh); break;
    case SCREEN_PAGE4: render_page4_147(app, full_refresh); break;
    case SCREEN_MENU: render_menu(app, full_refresh); break;
    case SCREEN_EDIT_SETTING: render_edit_setting(app, full_refresh); break;
    case SCREEN_LANGUAGE: render_language(app, full_refresh); break;
    case SCREEN_ABOUT: render_about(app); break;
    case SCREEN_FACTORY_RESET: render_factory_reset(app); break;
    case SCREEN_FIRST_SETUP: render_first_setup(app); break;
    default: break;
  }
  g_last_screen = (uint8_t)app->screen;
  app->dirty = false;
}

static void move_menu_selection(app_t *app, int8_t direction)
{
  if (direction < 0) {
    if (app->menu_index > 0U) {
      app->menu_index--;
      app->dirty = true;
    }
  } else if (direction > 0) {
    if (app->menu_index < (MENU_COUNT - 1U)) {
      app->menu_index++;
      app->dirty = true;
    }
  }
}

static void handle_key(app_t *app, key_event_t evt)
{
  if (evt == KEY_EVENT_NONE) {
    return;
  }
  app->last_activity_ms = millis();
  if (app->display_sleeping) {
    wake_display(app, app->last_activity_ms);
    return;
  }

  switch (app->screen) {
    case SCREEN_PAGE1:
    case SCREEN_PAGE2:
    case SCREEN_PAGE3:
    case SCREEN_PAGE4:
      if (evt == KEY_EVENT_S2_LONG) {
        app->screen = SCREEN_MENU;
      } else if (evt == KEY_EVENT_S1_CLICK) {
        app->screen = (app->screen == SCREEN_PAGE1) ? SCREEN_PAGE4 : (screen_t)((uint8_t)app->screen - 1U);
      } else if (evt == KEY_EVENT_S3_CLICK) {
        app->screen = (app->screen == SCREEN_PAGE4) ? SCREEN_PAGE1 : (screen_t)((uint8_t)app->screen + 1U);
      }
      remember_data_screen(app);
      app->dirty = true;
      break;

    case SCREEN_MENU:
      if (evt == KEY_EVENT_S1_CLICK) {
        move_menu_selection(app, -1);
      } else if (evt == KEY_EVENT_S3_CLICK) {
        move_menu_selection(app, 1);
      } else if (evt == KEY_EVENT_S2_CLICK) {
        if (menu_is_external_read(app->menu_index)) {
          app->dirty = true;
        } else if (app->menu_index == MENU_FAN_TEMP) {
          app->edit_menu_index = app->menu_index;
          app->edit_value = app->fan_on_temp_c;
          app->edit_current = app->fan_off_temp_c;
          app->edit_sub_index = 0U;
          app->screen = SCREEN_EDIT_SETTING;
        } else if (menu_is_local_edit(app->menu_index)) {
          begin_local_edit(app, app->menu_index);
        } else if (menu_is_ip2366_edit(app->menu_index)) {
          app->edit_menu_index = app->menu_index;
          if (!ip2366_read_menu_value(app->edit_menu_index, &app->edit_value)) {
            app->edit_value = 0U;
          }
          app->edit_current = app->edit_value;
          app->screen = SCREEN_EDIT_SETTING;
        } else if (app->menu_index == MENU_FACTORY_RESET) {
          app->screen = SCREEN_FACTORY_RESET;
        } else if (app->menu_index == MENU_LANGUAGE) {
          app->language_select = (uint8_t)app->language;
          app->screen = SCREEN_LANGUAGE;
        } else {
          app->screen = SCREEN_ABOUT;
        }
      } else if (evt == KEY_EVENT_S2_LONG) {
        app->screen = SCREEN_PAGE1;
        remember_data_screen(app);
      }
      app->dirty = true;
      break;

    case SCREEN_EDIT_SETTING:
      if (evt == KEY_EVENT_S1_CLICK) {
        if (app->edit_menu_index == MENU_FAN_TEMP) {
          adjust_fan_setting(app, -1);
        } else {
          adjust_menu_setting(app, -1);
        }
      } else if (evt == KEY_EVENT_S3_CLICK) {
        if (app->edit_menu_index == MENU_FAN_TEMP) {
          adjust_fan_setting(app, 1);
        } else {
          adjust_menu_setting(app, 1);
        }
      } else if (evt == KEY_EVENT_S2_CLICK) {
        if (app->edit_menu_index == MENU_FAN_TEMP) {
          app->edit_sub_index ^= 1U;
        } else if (app->edit_menu_index == MENU_OUTPUT_POWER) {
          bool write_ok = ip2366_write_menu_value(app->edit_menu_index, app->edit_value);
          delay_ms(80);
          if (write_ok) {
            (void)ip2366_read_menu_value(app->edit_menu_index, &app->edit_current);
          }
          sample_once(app, 0U);
        } else if (menu_is_local_edit(app->edit_menu_index)) {
          apply_local_edit(app);
          sample_once(app, 0U);
          app->screen = SCREEN_MENU;
        } else {
          bool write_ok = ip2366_write_menu_value(app->edit_menu_index, app->edit_value);
          delay_ms(80);
          if (write_ok) {
            (void)ip2366_read_menu_value(app->edit_menu_index, &app->edit_current);
            if (app->edit_menu_index == MENU_BATTERY_VOLTAGE) {
              refresh_battery_type(app);
            }
          }
          sample_once(app, 0U);
          app->screen = SCREEN_MENU;
        }
      } else if (evt == KEY_EVENT_S2_LONG) {
        if (app->edit_menu_index == MENU_FAN_TEMP) {
          app->fan_on_temp_c = app->edit_value;
          app->fan_off_temp_c = app->edit_current;
          save_settings(app);
        } else if (menu_is_local_edit(app->edit_menu_index) && app->edit_menu_index != MENU_BATTERY_SERIES) {
          apply_local_edit(app);
        }
        app->screen = SCREEN_MENU;
      }
      app->dirty = true;
      break;

    case SCREEN_LANGUAGE:
      if (evt == KEY_EVENT_S1_CLICK || evt == KEY_EVENT_S3_CLICK) {
        app->language_select ^= 1U;
      } else if (evt == KEY_EVENT_S2_CLICK) {
        app->language = (language_t)app->language_select;
        save_settings(app);
        app->screen = SCREEN_MENU;
      } else if (evt == KEY_EVENT_S2_LONG) {
        app->screen = SCREEN_MENU;
      }
      app->dirty = true;
      break;

    case SCREEN_ABOUT:
      if (evt == KEY_EVENT_S2_CLICK || evt == KEY_EVENT_S2_LONG) {
        app->screen = SCREEN_MENU;
        app->dirty = true;
      }
      break;

    case SCREEN_FACTORY_RESET:
      if (evt == KEY_EVENT_S2_LONG) {
        factory_reset_app(app);
        app->screen = SCREEN_PAGE1;
        remember_data_screen(app);
      } else if (evt == KEY_EVENT_S2_CLICK) {
        app->screen = SCREEN_MENU;
      }
      app->dirty = true;
      break;

    case SCREEN_FIRST_SETUP:
      if (evt == KEY_EVENT_S2_LONG) {
        app->edit_menu_index = MENU_BATTERY_CURRENT;
        if (!ip2366_read_menu_value(app->edit_menu_index, &app->edit_value)) {
          app->edit_value = 97U;
        }
        app->edit_current = app->edit_value;
        app->screen = SCREEN_EDIT_SETTING;
        app->dirty = true;
      }
      break;

  }
}

static void handle_key_repeat(app_t *app, uint32_t now)
{
  uint8_t key = 0xFFU;
  int8_t direction = 0;

  if (app->screen != SCREEN_EDIT_SETTING && app->screen != SCREEN_MENU) {
    app->repeat_key = 0xFFU;
    return;
  }

  if (key_is_pressed(KEY_ID_S3)) {
    key = (uint8_t)KEY_ID_S3;
    direction = 1;
  } else if (key_is_pressed(KEY_ID_S1)) {
    key = (uint8_t)KEY_ID_S1;
    direction = -1;
  }

  if (key == 0xFFU) {
    app->repeat_key = 0xFFU;
    return;
  }
  app->last_activity_ms = now;

  if (app->repeat_key != key) {
    app->repeat_key = key;
    app->repeat_next_ms = now + 450U;
    return;
  }

  if ((uint32_t)(now - app->repeat_next_ms) < 0x80000000UL) {
    if (app->screen == SCREEN_MENU) {
      move_menu_selection(app, direction);
    } else {
      if (app->edit_menu_index == MENU_FAN_TEMP) {
        adjust_fan_setting(app, direction);
      } else {
        adjust_menu_setting(app, direction);
      }
    }
    app->repeat_next_ms = now + (app->screen == SCREEN_MENU ? 140U : 80U);
  }
}

static bool any_key_pressed(void)
{
  return key_is_pressed(KEY_ID_S1) || key_is_pressed(KEY_ID_S2) || key_is_pressed(KEY_ID_S3);
}

static bool auto_sleep_selected(const app_t *app)
{
  return app->sleep_timeout == SLEEP_TIMEOUT_AUTO;
}

static bool vbus_voltage_active(const sw6236_metrics_t *m)
{
  return m->vbus_mv >= AUTO_SLEEP_VBUS_ACTIVE_MV;
}

static bool auto_power_activity_active(const sw6236_metrics_t *m)
{
  return vbus_voltage_active(m) ||
         m->ibus_ma >= AUTO_SLEEP_CURRENT_ACTIVE_MA ||
         m->ibat_ma >= AUTO_SLEEP_CURRENT_ACTIVE_MA ||
         input_active(m) ||
         output_active(m);
}

static void wake_display(app_t *app, uint32_t now)
{
  st7735_display_enable(true);
  app->display_sleeping = false;
  app->last_activity_ms = now;
  if (auto_sleep_selected(app) && !auto_power_activity_active(&app->metrics)) {
    app->auto_no_vbus_since_ms = now;
  }
  g_last_screen = 0xFFU;
  app->dirty = true;
}

static void wait_for_display_wakeup(app_t *app)
{
  board_low_power_wait_for_key();
  wake_display(app, millis());
  app->wake_key_suppressed = true;
}

static bool metrics_changed_for_screen(const sw6236_metrics_t *old_m, const sw6236_metrics_t *new_m)
{
  uint16_t old_vbus_disp = old_m->vbus_mv >= 10000U ? (uint16_t)(old_m->vbus_mv / 100U) : (uint16_t)(old_m->vbus_mv / 20U);
  uint16_t new_vbus_disp = new_m->vbus_mv >= 10000U ? (uint16_t)(new_m->vbus_mv / 100U) : (uint16_t)(new_m->vbus_mv / 20U);
  uint16_t old_ibus_disp = (uint16_t)(old_m->ibus_ma / 20U);
  uint16_t new_ibus_disp = (uint16_t)(new_m->ibus_ma / 20U);
  uint16_t old_ibat_disp = (uint16_t)(old_m->ibat_ma / 20U);
  uint16_t new_ibat_disp = (uint16_t)(new_m->ibat_ma / 20U);
  uint16_t old_vbat_disp = (uint16_t)(old_m->vbat_mv / 10U);
  uint16_t new_vbat_disp = (uint16_t)(new_m->vbat_mv / 10U);
  uint32_t old_power_mw = display_usb_power_mw(old_m);
  uint32_t new_power_mw = display_usb_power_mw(new_m);
  uint16_t old_power_disp = old_power_mw >= 10000UL ? (uint16_t)(old_power_mw / 200UL) : (uint16_t)(old_power_mw / 50UL);
  uint16_t new_power_disp = new_power_mw >= 10000UL ? (uint16_t)(new_power_mw / 200UL) : (uint16_t)(new_power_mw / 50UL);

  return old_m->online != new_m->online ||
         old_m->powered != new_m->powered ||
         old_m->fast_charge != new_m->fast_charge ||
         old_m->mode_status != new_m->mode_status ||
         old_m->port_online != new_m->port_online ||
         old_m->event_status != new_m->event_status ||
         old_m->discharge_error != new_m->discharge_error ||
         old_m->charge_error != new_m->charge_error ||
         old_m->percent != new_m->percent ||
         old_vbus_disp != new_vbus_disp ||
         old_vbat_disp != new_vbat_disp ||
         old_ibus_disp != new_ibus_disp ||
         old_ibat_disp != new_ibat_disp ||
         old_m->capacity_mah != new_m->capacity_mah ||
         (old_m->chip_temp_c10 / 2) != (new_m->chip_temp_c10 / 2) ||
         (old_m->ntc_temp_c10 / 2) != (new_m->ntc_temp_c10 / 2) ||
         old_power_disp != new_power_disp ||
         old_m->b0_ok != new_m->b0_ok ||
         old_m->b0_value != new_m->b0_value ||
         old_m->suspect_damage != new_m->suspect_damage;
}

static void sample_once(app_t *app, uint32_t elapsed_ms)
{
  int16_t ntc_temp_c10 = (int16_t)(thermistor_temperature_c10(adc_read_pa0()) + app->temp_offset_c10);
  uint32_t now = millis();
  bool next_fan = fan_should_run(app, ntc_temp_c10, now);
  sw6236_metrics_t old_metrics = app->metrics;
  uint8_t old_auto_series = app->battery_series_auto;

  if (next_fan != app->fan_on) {
    app->fan_on = next_fan;
    if (app->fan_on) {
      app->fan_started_ms = now;
    }
    fan_set(app->fan_on);
  }

  (void)sw6236_read_metrics(&app->metrics);
  update_auto_battery_series(app);
  app->metrics.ntc_temp_c10 = ntc_temp_c10;
  app->metrics.set_capacity_mah = app->set_capacity_mah;
  app->metrics.set_battery_mv = app->set_battery_mv;
  if (app->gauge_initialized) {
    battery_gauge_update(app, &old_metrics, &app->metrics, elapsed_ms);
  }
  if (is_data_screen(app->screen) && metrics_changed_for_screen(&old_metrics, &app->metrics)) {
    app->dirty = true;
  }
  if (old_auto_series != app->battery_series_auto) {
    app->dirty = true;
  }
}

int main(void)
{
  app_t app;
  app_settings_t settings;
  bool settings_valid;

#if defined(APPLICATION) && defined(USER_VECT_TAB_OFFSET)
  SCB->VTOR = FLASH_BASE | USER_VECT_TAB_OFFSET;
  __DSB();
  __ISB();
#endif
  __enable_irq();

  memset(&app, 0, sizeof(app));
  sw6236_defaults(&app.metrics);
  settings_valid = app_settings_load(&settings);
  if (!settings_valid) {
    app_settings_defaults(&settings);
  }
  app.last_data_screen = settings.last_screen > 3U ? 0U : settings.last_screen;
  app.screen = (screen_t)app.last_data_screen;
  app.set_capacity_mah = settings.capacity_mah;
  app.capacity_learned = settings.gauge_learned != 0U;
  app.set_battery_mv = DEFAULT_BATTERY_VOLTAGE_MV;
  app.set_battery_type = settings.battery_type;
  app.current_battery_type = settings.battery_type;
  app.fan_on_temp_c = settings.fan_on_temp_c > 100U ? 60U : settings.fan_on_temp_c;
  app.fan_off_temp_c = settings.fan_off_temp_c > 100U ? 50U : settings.fan_off_temp_c;
  app.language = settings.language == LANG_CHINESE ? LANG_CHINESE : LANG_ENGLISH;
  app.language_select = (uint8_t)app.language;
  app.screen_rotated_180 = settings.screen_rotation ? 1U : 0U;
  app.brightness_percent = settings.brightness_percent < 20U ? 100U : settings.brightness_percent;
  if (app.brightness_percent > 100U) app.brightness_percent = 100U;
  app.sleep_timeout = settings.sleep_timeout > SLEEP_TIMEOUT_MAX_STEP ? SLEEP_TIMEOUT_OFF : settings.sleep_timeout;
  app.temp_offset_c10 = settings.temp_offset_c10;
  app.battery_percent_mode = settings.battery_percent_mode > 1U ? BATTERY_PERCENT_MODE_GAUGE : settings.battery_percent_mode;
  app.battery_series = (settings.battery_series >= 2U && settings.battery_series <= 6U) ? settings.battery_series : 0U;
  app.voltage_percent_filter_valid = false;
  app.first_setup = false;
  app.repeat_key = 0xFFU;
  app.dirty = true;

  board_clock_init();
  board_systick_init();
  board_gpio_init();
  board_spi_init();

  backlight_set(false);
  fan_set(false);
  torch_led_set(false);
  delay_ms(300);

  st7735_init();
  st7735_set_brightness(app.brightness_percent);
  st7735_set_rotation(app.screen_rotated_180 ? 2U : 1U);
  st7735_set_offsets(0, 34);

  board_i2c_init();
  if (!settings_valid) {
    apply_ip2366_default_settings();
  }
  refresh_battery_type(&app);
  board_adc_init();
  keys_init();
  app.flash_ok = board_ext_flash_read_id(app.flash_id);

  sample_once(&app, 0U);
  update_auto_battery_series(&app);
  battery_gauge_init(&app, &settings);
  if (!settings_valid) {
    save_settings(&app);
  }
  render(&app);
  backlight_set(true);
  app.last_sample_ms = millis();
  app.last_draw_ms = app.last_sample_ms;
  app.last_activity_ms = app.last_sample_ms;
  app.auto_no_vbus_since_ms = app.last_sample_ms;

  while (1) {
    uint32_t now = millis();
    status_item_t status_items[16];
    uint8_t status_count;
    bool key_down;

    keys_poll();
    key_down = any_key_pressed();
    if (app.wake_key_suppressed) {
      (void)key_get_event();
      if (keys_any_raw_pressed()) {
        app.last_activity_ms = now;
      } else {
        keys_init();
        app.wake_key_suppressed = false;
      }
    } else {
      handle_key(&app, key_get_event());
      if (key_down) {
        app.last_activity_ms = now;
        if (app.display_sleeping) {
          wake_display(&app, now);
        }
      }
      handle_key_repeat(&app, now);
    }

    if (app.display_sleeping && auto_sleep_selected(&app)) {
      bool key_wakeup = board_low_power_wait_for_key_or_sw6236_timeout(AUTO_SLEEP_POLL_MS);
      bool should_wake;

      now = millis();
      app.last_sample_ms = now;
      sample_once(&app, 0U);
      should_wake = key_wakeup || auto_power_activity_active(&app.metrics);
      if (should_wake) {
        wake_display(&app, now);
        app.wake_key_suppressed = key_wakeup;
      } else {
        app.auto_no_vbus_since_ms = now;
        continue;
      }
    }

    if ((now - app.last_sample_ms) >= 400U) {
      uint8_t old_percent = app.metrics.percent;
      uint32_t elapsed_ms = now - app.last_sample_ms;
      app.last_sample_ms = now;
      sample_once(&app, elapsed_ms);
      if (is_data_screen(app.screen) && old_percent != app.metrics.percent) {
        app.dirty = true;
      }
    }

    if (app.screen == SCREEN_PAGE1) {
      status_count = build_status_items(&app, &app.metrics, status_items, 16U);
      if (status_count > 1U && (now - app.last_status_rotate_ms) >= 1200U) {
        app.last_status_rotate_ms = now;
        app.status_rotate_index = (uint8_t)((app.status_rotate_index + 1U) % status_count);
        app.dirty = true;
      }
    }

    if (auto_sleep_selected(&app)) {
      if (auto_power_activity_active(&app.metrics)) {
        app.auto_no_vbus_since_ms = now;
        if (app.display_sleeping) {
          wake_display(&app, now);
        }
      } else {
        if (app.auto_no_vbus_since_ms == 0UL) {
          app.auto_no_vbus_since_ms = now;
        }
        if (!app.display_sleeping && (now - app.auto_no_vbus_since_ms) >= AUTO_SLEEP_NO_VBUS_DELAY_MS) {
          st7735_display_enable(false);
          app.display_sleeping = true;
        }
      }
    } else if (app.sleep_timeout >= SLEEP_TIMEOUT_MIN_STEP &&
               app.sleep_timeout <= SLEEP_TIMEOUT_MAX_STEP &&
               !app.display_sleeping) {
      uint32_t timeout_ms = (uint32_t)(app.sleep_timeout - 1U) * 5000UL;
      if ((now - app.last_activity_ms) >= timeout_ms) {
        st7735_display_enable(false);
        app.display_sleeping = true;
      }
    }

    if (app.display_sleeping && !auto_sleep_selected(&app)) {
      wait_for_display_wakeup(&app);
    }

    if (!app.display_sleeping && app.dirty && ((now - app.last_draw_ms) >= DISPLAY_RENDER_INTERVAL_MS)) {
      app.last_draw_ms = now;
      render(&app);
    }
  }
}






