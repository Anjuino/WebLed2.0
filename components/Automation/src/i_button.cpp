#include "i_button.h"
#include "device.h"
#include "esp_log.h"
#include <cinttypes>

static const char *TAG = "i_button";

i_button::i_button(device *_dev) : Automation(_dev) {
  priority = 20;
  name = "button";

  state.mode = 254;
  state.speed = 10;
  state.brightness = 50;
  state.r = 255;
  state.g = 0;
  state.b = 0;
}

i_button::~i_button() {
  if (hold_timer) xTimerDelete(hold_timer, portMAX_DELAY);
}

void i_button::from_json(cJSON *json) {
  cJSON *j_priority = cJSON_GetObjectItem(json, "priority");
  if (cJSON_IsNumber(j_priority) && j_priority->valueint >= 0 && j_priority->valueint <= 255) {
    priority = j_priority->valueint;
  }

  cJSON *action = cJSON_GetObjectItem(json, "action");
  if(action) {
    cJSON *j_hold = cJSON_GetObjectItem(action, "hold");
    if (cJSON_IsNumber(j_hold) && j_hold->valueint > 0) {
      hold = j_hold->valueint;
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

void i_button::start() {
  hold_timer = xTimerCreate("btn_hold", pdMS_TO_TICKS(hold * 1000), pdFALSE, this, on_timer_cb);
  if (!hold_timer) ESP_LOGE(TAG, "timer create failed");

  if (!btn.init(pin, on_press_cb, this)) {
    ESP_LOGE(TAG, "button init failed, pin %d", pin);
  }
}

void i_button::on_press() {
  if (!in_active_time()) {
    ESP_LOGI(TAG, "press: вне времени активности");
    return;
  } else {
    if (has_active_time) {
      ESP_LOGI(TAG, "press: время активности %02d:%02d - %02d:%02d",
              active_start / 60, active_start % 60, active_stop / 60, active_stop % 60);
    } else {
      ESP_LOGI(TAG, "press: время активности не задано, работаю всегда");
    }
  }

  if (!dev->engage(this)) {
    ESP_LOGI(TAG, "press: лента занята более приоритетным источником");
    return;
  }

  ESP_LOGI(TAG, "press: захватил ленту на %lu сек", hold);
  class led *wled = dev->get_led();

  if(wled) wled->set_state(state.r, state.g, state.b, state.mode, state.speed, state.brightness);

  if (hold_timer) xTimerReset(hold_timer, 0);   // повторное нажатие продлевает
}

void i_button::on_timer() {
  ESP_LOGI(TAG, "timer: отпускаю ленту");
  dev->release(this);
}
