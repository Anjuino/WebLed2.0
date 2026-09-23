#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// показания до 60 нет датчика

class light_sensor {
  private:
      adc_unit_t unit;
      adc_channel_t channel;
      adc_oneshot_unit_handle_t adc_handle;
      TaskHandle_t task;
      void (*user_cb)(int value, void *user_data);
      void* user_arg;
      uint32_t period_ms;

      static void task_entry(void* arg);

  public:
      light_sensor();

      bool init(int gpio_num, void (*cb)(int value, void *user_data), uint32_t _period_ms, void *_user_arg = nullptr);

      ~light_sensor();
};

#endif // LIGHT_SENSOR_H
