#ifndef I_BUTTON_H
#define I_BUTTON_H

#include "automation.h"
#include "button.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

class i_button : public Automation {
  private:
    button btn;                        // драйвер: GPIO, прерывание, дребезг
    uint8_t pin = 6;
    uint32_t hold = 10;                // сколько держим ленту после нажатия, сек; 0 — до повторного нажатия
    TimerHandle_t hold_timer = nullptr;

    // что делает нажатие, пока кнопка держит ленту
    enum class repeat_t : uint8_t {
      extend,                          // перезапустить отсчёт hold с начала
      release,                         // отпустить ленту — вернуть то, что было до кнопки
    };
    repeat_t repeat = repeat_t::extend;

    static constexpr uint32_t REPEAT_GUARD_MS = 500;   // повтор быстрее — случайный двойной клик, игнор
    TickType_t press_tick = 0;         // когда кнопка захватила ленту

    led_state state;
    bool applied = false;              // своё состояние уже выставили на ленту
    bool holding = false;              // кнопка держит ленту: нажата и ещё не отпущена (таймером или повтором)
    SemaphoreHandle_t lock = nullptr;  // нажатие (btn_tsk) и таймер (задача таймеров) — разные задачи

    void stop_holding(const char *why);
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
