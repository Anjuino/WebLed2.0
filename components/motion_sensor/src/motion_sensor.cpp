#include "motion_sensor.h"

void IRAM_ATTR motion_sensor::isr_trampoline(void* arg) {
  motion_sensor* self = (motion_sensor*)arg;
  BaseType_t hp = pdFALSE;
  vTaskNotifyGiveFromISR(self->task, &hp);
  portYIELD_FROM_ISR(hp);
}

void motion_sensor::task_entry(void* arg) {
  motion_sensor* self = (motion_sensor*)arg;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // защита от помехи: настоящее движение держит пин секундами, а короткий выброс —
    // миллисекунды. Ждём и смотрим, что пин всё ещё активен
    vTaskDelay(pdMS_TO_TICKS(CONFIRM_MS));
    ulTaskNotifyTake(pdTRUE, 0);   // фронты, пришедшие за время ожидания, — тот же всплеск, обрабатывать их ещё раз не нужно

    if (self->is_active() && self->user_cb) {
      self->user_cb(self->user_data);
    }
  }
}

bool motion_sensor::init(uint8_t _pin, bool _active_high, void (*cb)(void *user_data), void *_user_data) {
  pin = _pin;
  active_high = _active_high;
  user_cb = cb;
  user_data = _user_data;

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << pin);
  cfg.mode = GPIO_MODE_INPUT;
  // подтяжка к неактивному уровню: оборванный провод не даёт ложных срабатываний
  cfg.pull_up_en = active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = active_high ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;

  if (gpio_config(&cfg) != ESP_OK) {
    return false;
  }

  gpio_install_isr_service(0);   // сервис может быть уже поднят кнопкой — это не ошибка

  if (xTaskCreate(task_entry, "motion_tsk", 3072, this, 3, &task) != pdPASS) {
    return false;
  }

  if (gpio_isr_handler_add((gpio_num_t)pin, isr_trampoline, this) != ESP_OK) {
    return false;
  }

  return true;
}

bool motion_sensor::is_active() {
  return (gpio_get_level((gpio_num_t)pin) != 0) == active_high;
}

motion_sensor::~motion_sensor() {
  if (task) {
    gpio_isr_handler_remove((gpio_num_t)pin);
    vTaskDelete(task);
  }
}
