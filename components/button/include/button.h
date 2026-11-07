#ifndef BUTTON_H
#define BUTTON_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class button {
  private:
      uint8_t pin;
      TaskHandle_t task;
      void (*user_cb)(void *user_data);
      void *user_data;
      uint32_t debounce_ms;

      static void IRAM_ATTR isr_trampoline(void* arg);
      static void task_entry(void* arg);

  public:
      button() {};

      bool init(uint8_t _pin, void (*cb)(void *user_data), void *_user_data = nullptr);

      ~button();
};

#endif // BUTTON_H
