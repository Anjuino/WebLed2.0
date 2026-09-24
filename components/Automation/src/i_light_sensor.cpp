#include "i_light_sensor.h"
#include "device.h"
#include "esp_log.h"

static const char *TAG = "i_light_sensor";

i_light_sensor::i_light_sensor(device *_dev) : Automation(_dev) {
  priority = 10;
  name = "light_sensor";

  state.mode = 254;
  state.speed = 10;
  state.brightness = 50;
  state.r = 255;
  state.g = 180;
  state.b = 80;
}

void i_light_sensor::from_json(cJSON *json) {
  cJSON *j_priority = cJSON_GetObjectItem(json, "priority");
  if (cJSON_IsNumber(j_priority) && j_priority->valueint >= 0 && j_priority->valueint <= 255) {
    priority = j_priority->valueint;
  }

  cJSON *action = cJSON_GetObjectItem(json, "action");
  if(action) {
    cJSON *j_dark = cJSON_GetObjectItem(action, "dark");
    if (cJSON_IsNumber(j_dark)) {
      dark = j_dark->valueint;
    }
    cJSON *j_light = cJSON_GetObjectItem(action, "light");
    if (cJSON_IsNumber(j_light)) {
      light = j_light->valueint;
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
      if (cJSON_IsNumber(j_speed) && j_speed->valueint > 0) {
        state.speed = j_speed->valueint;
      }
      cJSON *j_brightness = cJSON_GetObjectItem(j_led, "brightness");
      if (cJSON_IsNumber(j_brightness) && j_brightness->valueint > 0) {
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

void i_light_sensor::start() {
  if (has_active_time) {
    ESP_LOGI(TAG, "старт: время активности %02d:%02d - %02d:%02d", active_start / 60, active_start % 60, active_stop / 60, active_stop % 60);
  } else {
    ESP_LOGI(TAG, "старт: время активности не задано, работаю всегда");
  }

  if (!sensor.init(pin, on_light_cb, period_ms, this)) {
    ESP_LOGE(TAG, "light sensor init failed, pin %d", pin);
  }
}

void i_light_sensor::on_light(int value) {
  bool less_is_darker = dark < light;
  if (less_is_darker ? value <= dark : value >= dark) {
    is_dark = true;
  } else if (less_is_darker ? value >= light : value <= light) {
    is_dark = false;
  }

  bool active = in_active_time();
  bool want = is_dark && active;
  ESP_LOGI(TAG, "light = %d, %s, %s", value, is_dark ? "темно" : "светло", want ? "лента нужна" : "лента не нужна");

  if (is_dark && !active) {
    ESP_LOGI(TAG, "вне времени активности %02d:%02d - %02d:%02d",
             active_start / 60, active_start % 60, active_stop / 60, active_stop % 60);
  }

  if (want) {
    if (dev->engage(this)) {
      if (!applied) {
        ESP_LOGI(TAG, "захватил ленту, включаю свой режим");
        class led *wled = dev->get_led();
        if (wled) wled->set_state(state.r, state.g, state.b, state.mode, state.speed, state.brightness);
        applied = true;
      }
    } else {
      ESP_LOGI(TAG, "лента занята более приоритетным источником");
      applied = false;
    }
  } else if (wanting) {
    ESP_LOGI(TAG, "%s, отпускаю ленту", is_dark ? "вышло время активности" : "стало светло");
    dev->release(this);
    applied = false;
  }

  wanting = want;
}
