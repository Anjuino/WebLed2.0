#include "button.h"

void IRAM_ATTR button::isr_trampoline(void* arg) {
  button* self = (button*)arg;
  BaseType_t hp = pdFALSE;
  vTaskNotifyGiveFromISR(self->task, &hp);
  portYIELD_FROM_ISR(hp);
}

void button::task_entry(void* arg) {
  button* self = (button*)arg;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    gpio_intr_disable((gpio_num_t)self->pin);

    if (gpio_get_level((gpio_num_t)self->pin) == 0) {
      vTaskDelay(pdMS_TO_TICKS(self->debounce_ms));
      if (gpio_get_level((gpio_num_t)self->pin) == 0) {
        if (self->user_cb) {
          self->user_cb(self->user_data);
        }
      }
    }

    gpio_intr_enable((gpio_num_t)self->pin);
  }
}

bool button::init(uint8_t _pin, void (*cb)(void *user_data), void *_user_data) {
  pin = _pin;
  user_cb = cb;
  user_data = _user_data;
  debounce_ms = 50;

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << pin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_NEGEDGE;

  if (gpio_config(&cfg) != ESP_OK) {
    return false;
  }

  gpio_install_isr_service(0);

  if (xTaskCreate(task_entry, "btn_tsk", 3072, this, 3, &task) != pdPASS) {
    return false;
  }

  if (gpio_isr_handler_add((gpio_num_t)pin, isr_trampoline, this) != ESP_OK) {
    return false;
  }

  return true;
}

button::~button() {
    gpio_isr_handler_remove((gpio_num_t)pin);
    if (task) {
      vTaskDelete(task);
    }
}
