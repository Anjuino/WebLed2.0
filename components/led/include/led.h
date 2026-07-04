#ifndef LED_H
#define LED_H

#include "led_strip.h"
#include "freertos/FreeRTOS.h" 
#include "esp_log.h"
#include "nvs_proxy.h"
#include <cstring>

#define STORAGE_LED "controller"
#define KEY_INIT "init"

#define KEY_LED_COUNT "count"
#define KEY_LED_SPEED "speed"
#define KEY_LED_BRIGHTNESS "brightness"
#define KEY_LED_MODE "mode"
#define KEY_LED_SAVE_MODE "safe_mode"

#define KEY_LED_COLOR_R "color_r"
#define KEY_LED_COLOR_G "color_g"
#define KEY_LED_COLOR_B "color_b"

class led {
  private:
    const char *TAG = "Led";
    NVSProxy settings;

    led_strip_handle_t led_strip = nullptr;
    uint8_t* color_buffer = nullptr;

    uint16_t led_count = 1;
    uint8_t speed = 20;
    uint8_t brightness = 20;
    uint8_t mode = 1;
    bool save_mode = false;

    uint8_t r,g,b = 0;
    void task();

  public:

    led();
    ~led();

    static void task_entry(void* pvParameters) { static_cast<led*>(pvParameters)->task(); };

    bool init();
    void set_pixel(uint16_t pixel_count, uint8_t r, uint8_t g, uint8_t b);

    void set_state(uint8_t r, uint8_t g, uint8_t b, uint8_t mode, uint8_t speed, uint8_t brightness);

    uint8_t get_speed(void) { return speed; };
    uint8_t get_brightness(void) { return brightness; };
    uint8_t get_mode(void) {return mode; };

    bool update_led_count(uint16_t new_count);
    void show(void);

    void off(void);
    void fill_color(uint8_t r, uint8_t g, uint8_t b);
    void rainbow();

};

#endif // LED_H