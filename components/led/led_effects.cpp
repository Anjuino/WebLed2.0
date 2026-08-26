#include "led_effects.h"

static void rainbow(led *self, void *)
{
  static uint16_t rainbow_offset = 0;
  uint16_t count = self->get_count_led();

  for (uint16_t i = 0; i < count; i++) {
    uint16_t hue = (i * 255 / count + rainbow_offset) % 255;
    uint8_t r, g, b;

    if (hue < 85) {
      r = 255 - hue * 3;
      g = hue * 3;
      b = 0;
    } else if (hue < 170) {
      hue -= 85;
      r = 0;
      g = 255 - hue * 3;
      b = hue * 3;
    } else {
      hue -= 170;
      r = hue * 3;
      g = 0;
      b = 255 - hue * 3;
    }

    self->set_pixel(i, r, g, b);
  }

  rainbow_offset = (rainbow_offset + 1) % 255;
  self->show();
  vTaskDelay(pdMS_TO_TICKS(110 - self->get_speed()));
}

static void static_color(led *self, void *)
{
  auto [r, g, b] = self->get_color();
  uint16_t count = self->get_count_led();
  for (uint16_t i = 0; i < count; i++) {
    self->set_pixel(i, r, g, b);
  }
  self->show();
}

static void off(led *self, void *)
{
  self->off();
}

void register_builtin_effects(led *self)
{
  self->add_effect(1,   "Радуга",         true,  &rainbow);
  self->add_effect(254, "Статичный цвет", false, &static_color);
  self->add_effect(255, "Выкл",           false, &off);
}
