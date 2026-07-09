#include "led.h"

led::led() : settings(STORAGE_LED, false)
{
  mutex = xSemaphoreCreateMutex();
  if (mutex == nullptr) {
    ESP_LOGE(TAG, "Не удалось создать мьютекс!");
  }

  uint8_t init_flag = settings.get<uint8_t>(KEY_INIT, 0);

  if (init_flag == 0) {
    settings.set(KEY_LED_COUNT, led_count);
    settings.set(KEY_LED_SPEED, speed);
    settings.set(KEY_LED_BRIGHTNESS, brightness);
    settings.set(KEY_LED_MODE, mode);
    settings.set(KEY_LED_SAVE_MODE, (uint8_t)save_mode);

    settings.set(KEY_INIT, (uint8_t)1, true);  //init для создания записи

    ESP_LOGI(TAG, "Первый запуск, сохраняем дефолтные состояния");
  } else {
    led_count = settings.get<uint16_t>(KEY_LED_COUNT, 1);
    speed = settings.get<uint8_t>(KEY_LED_SPEED, 10);
    brightness = settings.get<uint8_t>(KEY_LED_BRIGHTNESS, 50);
    mode = settings.get<uint8_t>(KEY_LED_MODE, 1);
    save_mode = settings.get<uint8_t>(KEY_LED_SAVE_MODE, 0);
    ESP_LOGI(TAG, "Загружено из памяти: LEDs=%u, Скорость=%u, Яркость=%u, Режим=%u, Режим сохранения %d", led_count, speed, brightness, mode, save_mode);
  }
}

led::~led()
{
  if (mutex != nullptr) {
    vSemaphoreDelete(mutex);
    mutex = nullptr;
  }

  if(led_strip != nullptr) {
    led_strip_del(led_strip);
    led_strip = nullptr;
  }

  if(color_buffer != nullptr) {
    free(color_buffer);
    color_buffer = nullptr;
  }
}

bool led::init()
{
  // Инициализируем буфер
  // Выделяем память под буфер: 3 байта (RGB) на каждый светодиод
  color_buffer = (uint8_t*)malloc(led_count * 3);
  if (!color_buffer) {
    ESP_LOGE(TAG, "Ошибка выделения буфера для %d LEDs", led_count);
    return false;
  }
  // Обнуляем буфер
  memset(color_buffer, 0, led_count * 3);

  led_strip_config_t strip_config = {};
  strip_config.strip_gpio_num = 21;                   //TODO поправить номер пина, добавить в настройку 2 для esp32wroom
  strip_config.max_leds = led_count;
  strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;
  strip_config.led_model = LED_MODEL_WS2812;
  strip_config.flags.invert_out = false;

  led_strip_rmt_config_t rmt_config = {};
  rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_config.resolution_hz = 10 * 1000 * 1000;
  rmt_config.flags.with_dma = true;               // false для esp32 wroom
  rmt_config.mem_block_symbols = 1024;            // 128 для esp32 wroom

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

  ESP_LOGI(TAG, "Запуск ленты: %d LEDs", led_count);

  if(save_mode) {
    ESP_LOGI(TAG, "Включен режим сохранения состояния");
  }
  return true;
}

bool led::update_led_count(uint16_t new_led_count)
{
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG, "Не удалось захватить мьютекс");
    return false;
  }

  ESP_LOGI(TAG, "Обновление количества светодоидов, текущее %u", led_count);
  uint16_t old_mode = mode;
  off();
  vTaskDelay(pdMS_TO_TICKS(100));

  led_strip_del(led_strip);
  led_strip = nullptr;
  free(color_buffer);
  color_buffer = nullptr;
  vTaskDelay(pdMS_TO_TICKS(500));

  if (!settings.set(KEY_LED_COUNT, new_led_count, true)) {
    ESP_LOGE(TAG, "Ошибка сохранения количества светодиодов");
    return false;
  }

  led_count = new_led_count;

  if (init()) {
    mode = old_mode;
    xSemaphoreGive(mutex);
    ESP_LOGI(TAG, "Обновление успешно %u", new_led_count);
    return true;
  }
  ESP_LOGI(TAG, "Перезапуск ленты закончился с ошибкой");
  xSemaphoreGive(mutex);
  return false;
}

void led::set_pixel(uint16_t pixel_count, uint8_t r, uint8_t g, uint8_t b)
{
  if(pixel_count >= led_count) {
    ESP_LOGI(TAG, "Номер светодиода %lu больше чем общее количество %lu", pixel_count, led_count);
    return;
  }

  if(color_buffer == nullptr) {
    ESP_LOGI(TAG, "Нет буфера под цвета");
    return;
  }

  uint16_t idx = pixel_count * 3;
  color_buffer[idx] = r;
  color_buffer[idx + 1] = g;
  color_buffer[idx + 2] = b;
}

void led::task()
{
  this->init();
  while (true) {
    static bool delay = false;

    switch (mode)
    {
      case 1:
        rainbow();
        delay = false;
        break;

      case 255:
        delay = true;
        break;
    }

    if (delay) vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void led::show(void) 
{
  for (uint16_t i = 0; i < led_count; i++) {
    uint16_t idx = i * 3;

    uint8_t r = (color_buffer[idx]     * brightness) / 255;
    uint8_t g = (color_buffer[idx + 1] * brightness) / 255;
    uint8_t b = (color_buffer[idx + 2] * brightness) / 255;

    led_strip_set_pixel(led_strip, i, r, g, b);
  }
  led_strip_refresh(led_strip);
}

void led::fill_color(uint8_t r, uint8_t g, uint8_t b)
{
  for (int i = 0; i < led_count; i++) {
    set_pixel(i, r, g, b); 
  }

  show();
}

void led::off(void)
{
  mode = 255;
  led_strip_clear(led_strip);
  memset(color_buffer, 0, led_count * 3);
}

void led::set_state(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _mode, uint8_t _speed, uint8_t _brightness)
{
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    ESP_LOGE(TAG, "Не удалось захватить мьютекс");
    return;
  }

  bool need_commit = false;

  if(this->r != _r) {
    ESP_LOGI(TAG, "Красный канал:%d", _r);
    this->r = _r;
    if(save_mode) {
      if(settings.set(KEY_LED_COLOR_R, _r)) {
        need_commit = true;
      }
      else {
        ESP_LOGE(TAG, "Ошибка сохранения канала r");
      }
    }
  } else {
    ESP_LOGI(TAG, "Красный канал тот же:%d", this->r);
  }

  if(this->g != _g) {
    ESP_LOGI(TAG, "Красный канал:%d", _g);
    this->g = _g;
    if(save_mode) {
      if(settings.set(KEY_LED_COLOR_G, _g)) {
        need_commit = true;
      }
      else {
        ESP_LOGE(TAG, "Ошибка сохранения канала g");
      }
    }
  } else {
    ESP_LOGI(TAG, "Зеленый канал тот же:%d", this->g);
  }

  if(this->b != _b) {
    ESP_LOGI(TAG, "Синий канал:%d", _b);
    this->b= _b;
    if(save_mode) {
      if(settings.set(KEY_LED_COLOR_B, _b)) {
        need_commit = true;
      }
      else {
        ESP_LOGE(TAG, "Ошибка сохранения канала b");
      }
    }
  } else {
    ESP_LOGI(TAG, "Синий канал тот же:%d", this->b);
  }

  if(this->mode != _mode) {
    ESP_LOGI(TAG, "Установка режима %d", _mode);
    this->mode = _mode;
    if(save_mode) {
      if(settings.set(KEY_LED_MODE, _mode)) {
        need_commit = true;
      }
      else {
        ESP_LOGE(TAG, "Ошибка сохранения режима");
      }
    }
  } else {
    ESP_LOGI(TAG, "Режим тот же %d", this->mode);
  }

  if(_speed <= 100) {
    if(this->speed != _speed) {
      ESP_LOGI(TAG, "Установка скорости %d", _speed);
      this->speed = _speed;
      if(save_mode) {
        if(settings.set(KEY_LED_SPEED, _speed)) {
          need_commit = true;
        }
        else {
          ESP_LOGE(TAG, "Ошибка сохранения скорости");
        }
      }
    } else {
      ESP_LOGE(TAG, "Скорость та же %d", this->speed);
    }
  } else {
    ESP_LOGE(TAG, "Скорость может быть от 0 до 100, передано как %d", _speed);
  }

  if(_brightness <= 100) {
    if(this->brightness != _brightness) {
      ESP_LOGI(TAG, "Установка яркости %d", _brightness);
      this->brightness = _brightness;
      if(save_mode) {
        if(settings.set(KEY_LED_BRIGHTNESS, _brightness)) {
          need_commit = true;
        }
        else {
          ESP_LOGE(TAG, "Ошибка сохранения яркости");
        }
      }
    } else {
      ESP_LOGE(TAG, "Яркость та же %d",this->brightness);
    }
  } else {
    ESP_LOGE(TAG, "Яркость может быть от 0 до 100, передано как %d", _brightness);
  }

  if(need_commit) {
    settings.commit();
  }

  xSemaphoreGive(mutex);
  if(mode == 1) show();  // нужно подумать дергать ли его если эффекты уже крутятся, по идее только на статичном надо
}

void led::rainbow()
{
  static uint16_t rainbow_offset = 0;
  for (uint16_t i = 0; i < led_count; i++) {
    uint16_t hue = (i * 255 / led_count + rainbow_offset) % 255;
    uint8_t r, g, b;

    if (hue < 85) {
      r = 255 - hue * 3;
      g = hue * 3;
      b = 0;
    } 
    else if (hue < 170) {
      hue -= 85;
      r = 0;
      g = 255 - hue * 3;
      b = hue * 3;
    } 
    else {
      hue -= 170;
      r = hue * 3;
      g = 0;
      b = 255 - hue * 3;
    }

    set_pixel(i, r, g, b);
  }

  rainbow_offset = (rainbow_offset + 1) % 255;
  show();
  vTaskDelay(pdMS_TO_TICKS(110 - speed));
}