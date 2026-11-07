#include "automation.h"
#include "device.h"
#include "esp_log.h"

static const char *TAG = "automation";

bool Automation::in_active_time() {
  if (!has_active_time) return true;

  if (!dev->is_time_synced()) {
    ESP_LOGW(TAG, "%s: время не синхронизировано, окно активности не учитывается", name.c_str());
    return true;
  }

  tm t = dev->t_time.get_current_time();
  uint16_t now = t.tm_hour * 60 + t.tm_min;

  if (active_start < active_stop) {
    return now >= active_start && now < active_stop;   // 10:00–18:00
  }
  return now >= active_start || now < active_stop;     // 19:00–08:00, через полночь
}
