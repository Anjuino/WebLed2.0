#ifndef I_BUTTON_H
#define I_BUTTON_H

#include "automation.h"
#include "button.h"
#include "freertos/timers.h"

class i_button : public Automation {
  private:
    button btn;                        // драйвер: GPIO, прерывание, дребезг
    uint8_t pin = 6;
    uint32_t hold = 10;          // сколько держим ленту после нажатия
    TimerHandle_t hold_timer = nullptr;

    led_state state;
    bool applied = false;              // своё состояние уже выставили на ленту
    // переходники: драйвер и таймер знают только void*, возвращаем себе тип
    static void on_press_cb(void *arg) { static_cast<i_button*>(arg)->on_press(); };
    static void on_timer_cb(TimerHandle_t t) { static_cast<i_button*>(pvTimerGetTimerID(t))->on_timer(); };

    void on_press();
    void on_timer();

  public:
    i_button(device *_dev);
    ~i_button();

    void from_json(cJSON *json) override;
    void get_state(cJSON *json) override;
    void start() override;
};

#endif // I_BUTTON_H
