#include "light_sensor.h"

void light_sensor::task_entry(void* arg) {
  light_sensor* self = (light_sensor*)arg;
  int raw = 0;
  int value = 0;
  int prev_value = 0;

  while (true) {
    xSemaphoreTake(self->lock, portMAX_DELAY);
    esp_err_t err = adc_oneshot_read(self->adc_handle, self->channel, &raw);
    xSemaphoreGive(self->lock);
    if (err == ESP_OK) {
      value = 0.3 * prev_value + 0.7 * raw;
      prev_value = value; 
      self->user_cb(value, self->user_arg);
    }
    vTaskDelay(pdMS_TO_TICKS(self->period_ms));
  }
}

light_sensor::light_sensor()
    : unit(ADC_UNIT_1), channel(ADC_CHANNEL_0), adc_handle(nullptr), task(nullptr), user_cb(nullptr), user_arg(nullptr) {}

bool light_sensor::init(int gpio_num, void (*cb)(int value, void *user_data), uint32_t _period_ms, void *_user_arg) {
  user_cb = cb;
  user_arg = _user_arg;
  period_ms = _period_ms;

  if (adc_oneshot_io_to_channel(gpio_num, &unit, &channel) != ESP_OK) {
    return false;
  }

  adc_oneshot_unit_init_cfg_t init_cfg = {};
  init_cfg.unit_id = unit;
  if (adc_oneshot_new_unit(&init_cfg, &adc_handle) != ESP_OK) {
    return false;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten    = ADC_ATTEN_DB_12;
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_oneshot_config_channel(adc_handle, channel, &chan_cfg) != ESP_OK) {
    return false;
  }

  lock = xSemaphoreCreateMutex();
  if (!lock) {
    return false;
  }

  if (xTaskCreate(task_entry, "light_tsk", 3*1024, this, 5, &task) != pdPASS) {
    return false;
  }

  return true;
}

int light_sensor::read_now() {
  if (!adc_handle || !lock) return -1;
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(200)) != pdTRUE) return -1;

  const int samples = 4;
  int sum = 0, ok = 0, raw = 0;
  for (int i = 0; i < samples; i++) {
    if (adc_oneshot_read(adc_handle, channel, &raw) == ESP_OK) {
      sum += raw;
      ok++;
    }
  }
  xSemaphoreGive(lock);

  return ok ? sum / ok : -1;
}

light_sensor::~light_sensor() {
    if (task) {
      vTaskDelete(task);
    }
    if (lock) {
      vSemaphoreDelete(lock);
    }
    if (adc_handle) {
      adc_oneshot_del_unit(adc_handle);
    }
}
