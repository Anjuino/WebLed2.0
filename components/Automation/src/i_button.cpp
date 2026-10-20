#include "i_button.h"
#include "device.h"
#include "esp_log.h"
#include <cinttypes>
#include <cstring>
#include <string>

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

  lock = xSemaphoreCreateMutex();
}

i_button::~i_button() {
  if (hold_timer) xTimerDelete(hold_timer, portMAX_DELAY);
  if (lock) vSemaphoreDelete(lock);
}

void i_button::get_state(cJSON *json) {
  cJSON *btn = cJSON_CreateObject();
  cJSON_AddBoolToObject(btn, "applied", applied);
  cJSON_AddBoolToObject(btn, "holding", holding);
  // сколько ещё держать: -1 — до повторного нажатия (hold = 0), поля нет — кнопка не держит ленту
  if (holding) {
    if (hold == 0) {
      cJSON_AddNumberToObject(btn, "remaining_sec", -1);
    } else if (hold_timer && xTimerIsTimerActive(hold_timer)) {
      TickType_t left = xTimerGetExpiryTime(hold_timer) - xTaskGetTickCount();
      cJSON_AddNumberToObject(btn, "remaining_sec", (left + configTICK_RATE_HZ - 1) / configTICK_RATE_HZ);
    }
  }
  cJSON_AddItemToObject(json, "button", btn);
}

void i_button::from_json(cJSON *json) {
  cJSON *j_priority = cJSON_GetObjectItem(json, "priority");
  if (cJSON_IsNumber(j_priority) && j_priority->valueint >= 0 && j_priority->valueint <= 255) {
    priority = j_priority->valueint;
  }

  cJSON *j_active = cJSON_GetObjectItem(json, "is_active");
  if (cJSON_IsBool(j_active)) is_active = cJSON_IsTrue(j_active);

  cJSON *action = cJSON_GetObjectItem(json, "action");
  if(action) {
    cJSON *j_hold = cJSON_GetObjectItem(action, "hold");
    if (cJSON_IsNumber(j_hold) && j_hold->valueint >= 0) {
      hold = j_hold->valueint;   // 0 — держать до повторного нажатия
    }

    cJSON *j_repeat = cJSON_GetObjectItem(action, "repeat");
    if (cJSON_IsString(j_repeat)) {
      if (strcmp(j_repeat->valuestring, "release") == 0) repeat = repeat_t::release;
      else if (strcmp(j_repeat->valuestring, "extend") == 0) repeat = repeat_t::extend;
      else ESP_LOGW(TAG, "неизвестный repeat \"%s\", оставляю extend", j_repeat->valuestring);
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
  // без таймера и с «продлить» ленту было бы не отпустить никогда — повтор обязан выключать
  if (hold == 0 && repeat == repeat_t::extend) {
    ESP_LOGW(TAG, "hold=0 (до повторного нажатия) с repeat=extend: использую release");
    repeat = repeat_t::release;
  }

  if (hold > 0) {
    hold_timer = xTimerCreate("btn_hold", pdMS_TO_TICKS(hold * 1000), pdFALSE, this, on_timer_cb);
    if (!hold_timer) ESP_LOGE(TAG, "timer create failed");
  }

  if (!btn.init(pin, on_press_cb, this)) {
    ESP_LOGE(TAG, "button init failed, pin %d", pin);
  }

  ESP_LOGI(TAG, "старт: держать %s, повторное нажатие — %s",
           hold ? (std::to_string(hold) + " с").c_str() : "до повторного нажатия",
           repeat == repeat_t::release ? "отпустить" : "продлить");
}

// отпустить ленту: вызывается под lock
void i_button::stop_holding(const char *why) {
  if (hold_timer) xTimerStop(hold_timer, 0);
  ESP_LOGI(TAG, "%s: отпускаю ленту", why);
  dev->release(this);
  holding = false;
  applied = false;
}

void i_button::on_press() {
  xSemaphoreTake(lock, portMAX_DELAY);

  // повтор, пока держим ленту: проверяем ДО окна времени и is_active — выключить нужно уметь
  // всегда (включил в 07:59, окно до 08:00 — в 08:01 кнопка всё равно должна погасить)
  if (holding && repeat == repeat_t::release) {
    if (xTaskGetTickCount() - press_tick < pdMS_TO_TICKS(REPEAT_GUARD_MS)) {
      ESP_LOGI(TAG, "press: повтор слишком быстро, игнор (двойной клик)");
    } else {
      stop_holding("press");
    }
    xSemaphoreGive(lock);
    return;
  }

  if (!is_active) {
    ESP_LOGI(TAG, "press: источник отключён, игнор");
    xSemaphoreGive(lock);
    return;
  }

  if (!in_active_time()) {
    ESP_LOGI(TAG, "press: вне времени активности");
    xSemaphoreGive(lock);
    return;
  }
  if (has_active_time) {
    ESP_LOGI(TAG, "press: время активности %02d:%02d - %02d:%02d",
             active_start / 60, active_start % 60, active_stop / 60, active_stop % 60);
  } else {
    ESP_LOGI(TAG, "press: время активности не задано, работаю всегда");
  }

  if (!dev->engage(this)) {
    ESP_LOGI(TAG, "press: лента занята более приоритетным источником");
    applied = false;
    xSemaphoreGive(lock);
    return;
  }

  if (hold) ESP_LOGI(TAG, "press: %s ленту на %lu сек", holding ? "продлеваю" : "захватил", hold);
  else ESP_LOGI(TAG, "press: захватил ленту до повторного нажатия");

  class led *wled = dev->get_led();
  if (wled) wled->set_state(state.r, state.g, state.b, state.mode, state.speed, state.brightness);
  applied = true;
  if (!holding) press_tick = xTaskGetTickCount();
  holding = true;

  if (hold_timer) xTimerReset(hold_timer, 0);   // extend: отсчёт заново

  xSemaphoreGive(lock);
}

void i_button::on_timer() {
  xSemaphoreTake(lock, portMAX_DELAY);
  // повтор мог отпустить ленту, пока таймер стоял в очереди задачи таймеров
  if (holding) stop_holding("timer");
  xSemaphoreGive(lock);
}
