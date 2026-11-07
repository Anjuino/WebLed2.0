#include "i_motion.h"
#include "device.h"
#include "esp_log.h"

static const char *TAG = "i_motion";

i_motion::i_motion(device *_dev) : Automation(_dev) {
  priority = 15;
  name = "motion";

  state.mode = 254;
  state.speed = 10;
  state.brightness = 50;
  state.r = 255;
  state.g = 180;
  state.b = 80;

  lock = xSemaphoreCreateMutex();
}

i_motion::~i_motion() {
  if (hold_timer) xTimerDelete(hold_timer, portMAX_DELAY);
  if (lock) vSemaphoreDelete(lock);
}

void i_motion::get_state(cJSON *json) {
  cJSON *motion = cJSON_CreateObject();
  cJSON_AddBoolToObject(motion, "applied", applied);
  cJSON_AddBoolToObject(motion, "holding", holding);
  if (holding && hold_timer && xTimerIsTimerActive(hold_timer)) {
    TickType_t left = xTimerGetExpiryTime(hold_timer) - xTaskGetTickCount();
    cJSON_AddNumberToObject(motion, "remaining_sec", (left + configTICK_RATE_HZ - 1) / configTICK_RATE_HZ);
  }
  if (has_light) {
    cJSON_AddNumberToObject(motion, "light", linked ? linked->get_value() : own_value.load());
    cJSON_AddBoolToObject(motion, "is_dark", is_dark);
    cJSON_AddNumberToObject(motion, "last_light", last_light);   // по какому значению принято решение на последнем движении
  }
  cJSON_AddItemToObject(json, "motion", motion);
}

void i_motion::from_json(cJSON *json) {
  cJSON *j_priority = cJSON_GetObjectItem(json, "priority");
  if (cJSON_IsNumber(j_priority) && j_priority->valueint >= 0 && j_priority->valueint <= 255) {
    priority = j_priority->valueint;
  }

  cJSON *j_active = cJSON_GetObjectItem(json, "is_active");
  if (cJSON_IsBool(j_active)) is_active = cJSON_IsTrue(j_active);

  cJSON *action = cJSON_GetObjectItem(json, "action");
  if(action) {
    cJSON *j_hold = cJSON_GetObjectItem(action, "hold");
    if (cJSON_IsNumber(j_hold) && j_hold->valueint > 0) {
      hold = j_hold->valueint;
    }

    cJSON *j_light = cJSON_GetObjectItem(action, "light_sensor");
    if (cJSON_IsObject(j_light)) {
      has_light = true;
      cJSON *j_dark = cJSON_GetObjectItem(j_light, "dark");
      if (cJSON_IsNumber(j_dark)) dark = j_dark->valueint;
      cJSON *j_lt = cJSON_GetObjectItem(j_light, "light");
      if (cJSON_IsNumber(j_lt)) light = j_lt->valueint;
    }

    cJSON *j_time = cJSON_GetObjectItem(action, "time");
    if(j_time) {
      cJSON *j_start_time = cJSON_GetObjectItem(j_time, "start");
      cJSON *j_end_time = cJSON_GetObjectItem(j_time, "end");
      if(j_start_time && j_end_time) {
        cJSON *j_start_time_h = cJSON_GetObjectItem(j_start_time, "hour");
        cJSON *j_start_minute_h = cJSON_GetObjectItem(j_start_time, "minute");
        if (cJSON_IsNumber(j_start_time_h) && cJSON_IsNumber(j_start_minute_h)) {
          active_start = j_start_time_h->valueint * 60 + j_start_minute_h->valueint;
        }
        cJSON *j_end_time_h = cJSON_GetObjectItem(j_end_time, "hour");
        cJSON *j_end_minute_h = cJSON_GetObjectItem(j_end_time, "minute");
        if (cJSON_IsNumber(j_end_time_h) && cJSON_IsNumber(j_end_minute_h)) {
          active_stop = j_end_time_h->valueint * 60 + j_end_minute_h->valueint;
          has_active_time = true;
        }
      }
    }

    cJSON *j_led = cJSON_GetObjectItem(action, "led");
    if(j_led) {
      cJSON *j_mode = cJSON_GetObjectItem(j_led, "mode");
      if (cJSON_IsNumber(j_mode) && j_mode->valueint > 0) {
        state.mode = j_mode->valueint;
      }
      cJSON *j_speed = cJSON_GetObjectItem(j_led, "speed");
      if (cJSON_IsNumber(j_speed) && j_speed->valueint >= 0 && j_speed->valueint <= 255) {
        state.speed = j_speed->valueint;
      }
      cJSON *j_brightness = cJSON_GetObjectItem(j_led, "brightness");
      if (cJSON_IsNumber(j_brightness) && j_brightness->valueint >= 0 && j_brightness->valueint <= 255) {
        state.brightness = j_brightness->valueint;
      }
      cJSON *color = cJSON_GetObjectItem(j_led, "color");
      if(color) {
        cJSON *j_color_r = cJSON_GetObjectItem(color, "r");
        cJSON *j_color_g = cJSON_GetObjectItem(color, "g");
        cJSON *j_color_b = cJSON_GetObjectItem(color, "b");
        if (cJSON_IsNumber(j_color_r) && cJSON_IsNumber(j_color_g) && cJSON_IsNumber(j_color_b)) {
          state.r = j_color_r->valueint;
          state.g = j_color_g->valueint;
          state.b = j_color_b->valueint;
        }
      }
    }
  }
}

void i_motion::start() {
  hold_timer = xTimerCreate("motion_hold", pdMS_TO_TICKS(hold * 1000), pdFALSE, this, on_timer_cb);
  if (!hold_timer) ESP_LOGE(TAG, "timer create failed");

  if (has_light) {
    // датчик света уже есть в списке автоматики — читаем с него (в device движение создаётся последним).
    // Нет — читаем сами: второй раз тот же ADC инициализировать нельзя, поэтому только в этой ветке
    for (auto &a : dev->automations) {
      if (a->get_name() == "light_sensor") {
        linked = static_cast<i_light_sensor*>(a.get());
        break;
      }
    }
    if (linked) {
      ESP_LOGI(TAG, "свет: беру показания у light_sensor, dark=%d light=%d", dark, light);
    } else {
      ESP_LOGI(TAG, "свет: light_sensor не настроен, читаю сам, dark=%d light=%d", dark, light);
      if (!own_sensor.init(i_light_sensor::PIN, on_own_light_cb, i_light_sensor::PERIOD_MS, this)) {
        ESP_LOGE(TAG, "light sensor init failed, pin %d", i_light_sensor::PIN);
      }
    }
  }

  if (!pir.init(PIN, ACTIVE_HIGH, on_motion_cb, this)) {
    ESP_LOGE(TAG, "motion sensor init failed, pin %d", PIN);
  }

  ESP_LOGI(TAG, "старт: держать %lu с%s", hold, has_light ? ", только в темноте" : ", свет не учитывается");
}

// замер именно в момент движения: значение из опроса может быть старше 10 секунд
int i_motion::read_light() {
  int value = linked ? linked->read_now() : own_sensor.read_now();
  if (value < 0) value = linked ? linked->get_value() : own_value.load();
  return value;
}

// гистерезис как у light_sensor; вызывается под lock
bool i_motion::dark_enough() {
  if (!has_light) return true;

  int value = read_light();
  last_light = value;
  if (value < 0) {
    ESP_LOGI(TAG, "замера света ещё не было");
    return is_dark;
  }

  bool less_is_darker = dark < light;
  if (less_is_darker ? value <= dark : value >= dark) {
    is_dark = true;
  } else if (less_is_darker ? value >= light : value <= light) {
    is_dark = false;
  }
  ESP_LOGI(TAG, "light = %d, %s", value, is_dark ? "темно" : "светло");
  return is_dark;
}

// отпустить ленту: вызывается под lock
void i_motion::stop_holding(const char *why) {
  if (hold_timer) xTimerStop(hold_timer, 0);
  ESP_LOGI(TAG, "%s: отпускаю ленту", why);
  dev->release(this);
  holding = false;
  applied = false;
}

void i_motion::on_motion() {
  xSemaphoreTake(lock, portMAX_DELAY);

  if (!is_active) {
    ESP_LOGI(TAG, "движение: источник отключён, игнор");
    xSemaphoreGive(lock);
    return;
  }

  if (!in_active_time()) {
    ESP_LOGI(TAG, "движение: вне времени активности");
    xSemaphoreGive(lock);
    return;
  }

  if (!dark_enough()) {
    ESP_LOGI(TAG, "движение: светло, игнор");
    xSemaphoreGive(lock);
    return;
  }

  bool was_owner = dev->is_owner(this);
  if (!dev->engage(this)) {
    ESP_LOGI(TAG, "движение: лента занята более приоритетным источником");
    applied = false;
    xSemaphoreGive(lock);
    return;
  }

  ESP_LOGI(TAG, "движение: %s ленту на %lu сек", was_owner ? "продлеваю" : "захватил", hold);

  // продление: лента уже в нашем состоянии, достаточно перезапустить таймер
  if (!was_owner) {
    class led *wled = dev->get_led();
    if (wled) wled->set_state(state.r, state.g, state.b, state.mode, state.speed, state.brightness);
  }
  applied = true;
  holding = true;

  if (hold_timer) xTimerReset(hold_timer, 0);   // отсчёт заново

  xSemaphoreGive(lock);
}

void i_motion::on_timer() {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (holding) {
    // PIR держит пин активным, пока человек в зоне, а фронт приходит один раз — пока движение есть, не гасим
    if (pir.is_active()) {
      if (hold_timer) xTimerReset(hold_timer, 0);
    } else {
      stop_holding("timer");
    }
  }
  xSemaphoreGive(lock);
}
