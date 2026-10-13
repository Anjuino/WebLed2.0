#include "i_schedule.h"
#include "device.h"
#include "esp_log.h"
#include <cstring>
#include <ctime>

static const char *TAG = "i_schedule";

i_schedule::i_schedule(device *_dev) : Automation(_dev) {
  priority = 15;
  name = "schedule";
}

i_schedule::~i_schedule() {
  if (tick_timer) {
    xTimerStop(tick_timer, portMAX_DELAY);
    xTimerDelete(tick_timer, portMAX_DELAY);
  }
}

void i_schedule::get_state(cJSON *json) {
  cJSON *sch = cJSON_CreateObject();
  cJSON_AddBoolToObject(sch, "applied", applied);
  cJSON_AddItemToObject(json, "schedule", sch);
}

int8_t i_schedule::parse_wday(cJSON *obj) {
  if (!cJSON_IsNumber(obj)) return -1;
  int v = obj->valueint;
  if (v < 0 || v > 6) return -1;
  return (int8_t)v;
}

bool i_schedule::parse_hm(cJSON *obj, uint16_t &out_min) {
  if (!cJSON_IsObject(obj)) return false;
  cJSON *h = cJSON_GetObjectItem(obj, "hour");
  cJSON *m = cJSON_GetObjectItem(obj, "minute");
  if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) return false;
  if (h->valueint < 0 || h->valueint > 23) return false;
  if (m->valueint < 0 || m->valueint > 59) return false;
  out_min = (uint16_t)(h->valueint * 60 + m->valueint);
  return true;
}

bool i_schedule::parse_led(cJSON *obj, led_state &out) {
  if (!cJSON_IsObject(obj)) return false;
  bool ok = false;
  cJSON *mode = cJSON_GetObjectItem(obj, "mode");
  if (cJSON_IsNumber(mode) && mode->valueint >= 0 && mode->valueint <= 255) {
    out.mode = (uint8_t)mode->valueint;
    ok = true;
  }
  cJSON *speed = cJSON_GetObjectItem(obj, "speed");
  if (cJSON_IsNumber(speed) && speed->valueint >= 0 && speed->valueint <= 255) {
    out.speed = (uint8_t)speed->valueint;
  }
  cJSON *brightness = cJSON_GetObjectItem(obj, "brightness");
  if (cJSON_IsNumber(brightness) && brightness->valueint >= 0 && brightness->valueint <= 255) {
    out.brightness = (uint8_t)brightness->valueint;
  }
  cJSON *color = cJSON_GetObjectItem(obj, "color");
  if (cJSON_IsObject(color)) {
    cJSON *r = cJSON_GetObjectItem(color, "r");
    cJSON *g = cJSON_GetObjectItem(color, "g");
    cJSON *b = cJSON_GetObjectItem(color, "b");
    if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
      out.r = (uint8_t)r->valueint;
      out.g = (uint8_t)g->valueint;
      out.b = (uint8_t)b->valueint;
    }
  }
  return ok;
}

void i_schedule::from_json(cJSON *json) {
  if (!cJSON_IsObject(json)) return;

  cJSON *j_priority = cJSON_GetObjectItem(json, "priority");
  if (cJSON_IsNumber(j_priority) && j_priority->valueint >= 0 && j_priority->valueint <= 255) {
    priority = (uint8_t)j_priority->valueint;
  }

  cJSON *j_active = cJSON_GetObjectItem(json, "is_active");
  if (cJSON_IsBool(j_active)) is_active = cJSON_IsTrue(j_active);

  days_count = 0;
  cJSON *j_days = cJSON_GetObjectItem(json, "days");
  if (!cJSON_IsArray(j_days)) return;

  cJSON *j_day = NULL;
  cJSON_ArrayForEach(j_day, j_days) {
    if (days_count >= MAX_DAYS) break;
    if (!cJSON_IsObject(j_day)) continue;

    cJSON *j_day_name = cJSON_GetObjectItem(j_day, "day");
    int8_t w = parse_wday(j_day_name);
    if (w < 0) continue;

    day_t &d = days[days_count];
    d.wday = w;
    d.intervals_count = 0;

    cJSON *j_intervals = cJSON_GetObjectItem(j_day, "intervals");
    if (cJSON_IsArray(j_intervals)) {
      cJSON *j_iv = NULL;
      cJSON_ArrayForEach(j_iv, j_intervals) {
        if (d.intervals_count >= MAX_INTERVALS) break;
        if (!cJSON_IsObject(j_iv)) continue;

        uint16_t start_min = 0, stop_min = 0;
        if (!parse_hm(cJSON_GetObjectItem(j_iv, "start"), start_min)) continue;
        if (!parse_hm(cJSON_GetObjectItem(j_iv, "stop"), stop_min)) continue;
        if (start_min == stop_min) continue;

        interval_t &iv = d.intervals[d.intervals_count];
        iv.start = start_min;
        iv.stop = stop_min;
        parse_led(cJSON_GetObjectItem(j_iv, "led"), iv.state);
        iv.valid = true;
        d.intervals_count++;
      }
    }

    days_count++;
  }
}

led_state *i_schedule::find_current_interval(int today_wday, uint16_t now_min) {
  int yesterday_wday = (today_wday + 6) % 7;

  // вчерашние через-полночь — хвост идёт сегодня до stop
  for (uint8_t d = 0; d < days_count; d++) {
    if (days[d].wday != yesterday_wday) continue;
    for (uint8_t i = 0; i < days[d].intervals_count; i++) {
      interval_t &iv = days[d].intervals[i];
      if (!iv.valid) continue;
      if (iv.start > iv.stop && now_min < iv.stop) {
        return &iv.state;
      }
    }
  }

  // сегодняшние
  for (uint8_t d = 0; d < days_count; d++) {
    if (days[d].wday != today_wday) continue;
    for (uint8_t i = 0; i < days[d].intervals_count; i++) {
      interval_t &iv = days[d].intervals[i];
      if (!iv.valid) continue;
      bool inside = false;
      if (iv.start < iv.stop) {
        inside = (now_min >= iv.start && now_min < iv.stop);
      } else if (iv.start > iv.stop) {
        inside = (now_min >= iv.start);
      }
      if (inside) return &iv.state;
    }
  }

  return nullptr;
}

void i_schedule::evaluate() {
  if (!is_active) {
    if (applied) {
      ESP_LOGI(TAG, "evaluate: источник отключён, отпускаю ленту");
      dev->release(this);
      applied = false;
      last_applied = nullptr;
    }
    return;
  }

  if (days_count == 0) return;

  if (!dev->is_time_synced()) {
    ESP_LOGW(TAG, "evaluate: время не синхронизировано, пропуск");
    return;
  }

  tm now = dev->t_time.get_current_time();
  uint16_t now_min = (uint16_t)(now.tm_hour * 60 + now.tm_min);
  int today_wday = now.tm_wday;

  led_state *cur = find_current_interval(today_wday, now_min);

  if (cur) {
    if (!applied) {
      if (dev->engage(this)) {
        class led *wled = dev->get_led();
        if (wled) wled->set_state(cur->r, cur->g, cur->b, cur->mode, cur->speed, cur->brightness);
        applied = true;
        last_applied = cur;
        ESP_LOGI(TAG, "evaluate: захватил ленту, интервал активен");
      } else {
        ESP_LOGI(TAG, "evaluate: лента занята более приоритетным источником");
      }
    } else if (cur != last_applied) {
      class led *wled = dev->get_led();
      if (wled) wled->set_state(cur->r, cur->g, cur->b, cur->mode, cur->speed, cur->brightness);
      last_applied = cur;
      ESP_LOGI(TAG, "evaluate: переключились на другой интервал");
    }
  } else {
    if (applied) {
      ESP_LOGI(TAG, "evaluate: интервал закончился, отпускаю ленту");
      dev->release(this);
      applied = false;
      last_applied = nullptr;
    }
  }
}

void i_schedule::start() {
  tick_timer = xTimerCreate("sched_tick", pdMS_TO_TICKS(TICK_MS), pdTRUE, this, on_tick_cb);
  if (!tick_timer) {
    ESP_LOGE(TAG, "timer create failed");
    return;
  }
  if (xTimerStart(tick_timer, 0) != pdPASS) {
    ESP_LOGE(TAG, "timer start failed");
  }

  ESP_LOGI(TAG, "start: дней=%d, опрос каждые %u мс", days_count, TICK_MS);

  evaluate();
}
