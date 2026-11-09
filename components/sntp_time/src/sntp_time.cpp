#include "sntp_time.h"
#include <cstdio>
#include <cstdlib>

static const char *TAG = "time";

#define STORAGE_TIME "time"
#define KEY_TZ_OFFSET "tz_offset"

// POSIX TZ использует обратный знак: локальное UTC+7 = "UTC-7".
static std::string make_tz_string(int offset_min)
{
  int posix = -offset_min;
  if (posix == 0) return "UTC0";

  int abs_min = posix < 0 ? -posix : posix;
  int hours = abs_min / 60;
  int minutes = abs_min % 60;

  char buf[16];
  if (minutes == 0) {
    snprintf(buf, sizeof(buf), "UTC%c%d", posix > 0 ? '+' : '-', hours);
  } else {
    snprintf(buf, sizeof(buf), "UTC%c%d:%02d", posix > 0 ? '+' : '-', hours, minutes);
  }
  return buf;
}

sntp_time::sntp_time() : settings(STORAGE_TIME, false)
{
  int16_t saved = settings.get<int16_t>(KEY_TZ_OFFSET, DEFAULT_OFFSET_MIN);
  if (saved < MIN_OFFSET_MIN || saved > MAX_OFFSET_MIN) {
    saved = DEFAULT_OFFSET_MIN;
  }
  tz_offset_min = saved;
  apply_timezone();
}

void sntp_time::apply_timezone()
{
  std::string tz = make_tz_string(tz_offset_min);
  setenv("TZ", tz.c_str(), 1);
  tzset();
  ESP_LOGI(TAG, "timezone: %s, offset=%d min", tz.c_str(), (int)tz_offset_min);
}

bool sntp_time::set_offset_min(int16_t offset, bool persist)
{
  if (offset < MIN_OFFSET_MIN || offset > MAX_OFFSET_MIN) return false;

  if (persist) {
    if (!settings.set<int16_t>(KEY_TZ_OFFSET, offset, true)) {
      ESP_LOGE(TAG, "failed to save timezone");
      return false;
    }
  }

  tz_offset_min = offset;
  apply_timezone();
  return true;
}

void sntp_time::sync() {
  if (is_init) return;

  apply_timezone();

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
  is_init = true;

  ESP_LOGI(TAG, "%s", get_str_current_time().c_str());
}

tm sntp_time::get_current_time()
{
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  return timeinfo;
}

std::string sntp_time::get_str_current_time()
{
  struct tm timeinfo = get_current_time();

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &timeinfo);

  return std::string(buf);
}
