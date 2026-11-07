#ifndef MOTION_SENSOR_H
#define MOTION_SENSOR_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Датчик движения с цифровым выходом (PIR и т.п.): пин держит активный уровень, пока есть движение.
// Колбэк зовётся по фронту «нет движения -> есть движение».
class motion_sensor {
  private:
      static constexpr uint32_t CONFIRM_MS = 300;   // сколько пин должен оставаться активным, чтобы считать это движением

      uint8_t pin = 0;
      bool active_high = true;
      TaskHandle_t task = nullptr;
      void (*user_cb)(void *user_data) = nullptr;
      void *user_data = nullptr;

      static void IRAM_ATTR isr_trampoline(void* arg);
      static void task_entry(void* arg);

  public:
      motion_sensor() {};

      bool init(uint8_t _pin, bool _active_high, void (*cb)(void *user_data), void *_user_data = nullptr);

      bool is_active();   // движение есть прямо сейчас (пин на активном уровне)

      ~motion_sensor();
};

#endif // MOTION_SENSOR_H
