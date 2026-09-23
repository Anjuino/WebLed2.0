#include "sntp_time.h"


void sntp_time::sync() {
  setenv("TZ", "UTC-7", 1);
  tzset();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  ESP_LOGI("time", "%s", get_str_current_time().c_str());
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
