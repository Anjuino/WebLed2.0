#ifndef I_MOTION_H
#define I_MOTION_H

#include <atomic>
#include "automation.h"
#include "motion_sensor.h"
#include "light_sensor.h"
#include "i_light_sensor.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

class i_motion : public Automation {
  private:
    static constexpr uint8_t PIN = 5;                 // пин выхода датчика движения
    static constexpr bool ACTIVE_HIGH = true;          // PIR: низкий уровень -> высокий и держит его

    motion_sensor pir;                 // драйвер: GPIO, прерывание
    uint32_t hold = 30;                // сколько держим ленту после последнего движения, сек
    TimerHandle_t hold_timer = nullptr;

    // привязка к свету: есть блок action.light_sensor — включаемся только в темноте
    bool has_light = false;
    int dark = 300;
    int light = 500;
    bool is_dark = false;              // решение с гистерезисом, обновляется по движению
    int last_light = -1;               // значение света, по которому принято последнее решение
    i_light_sensor *linked = nullptr;  // датчик света из списка автоматики, если он там есть
    light_sensor own_sensor;           // иначе читаем сами; в список автоматики не попадает
    std::atomic<int> own_value{-1};

    led_state state;
    bool applied = false;              // своё состояние уже выставили на ленту
    bool holding = false;              // держим ленту: сработали и таймер ещё не вышел
    SemaphoreHandle_t lock = nullptr;  // движение (motion_tsk) и таймер (задача таймеров) — разные задачи

    static void on_motion_cb(void *arg) { static_cast<i_motion*>(arg)->on_motion(); };
    static void on_timer_cb(TimerHandle_t t) { static_cast<i_motion*>(pvTimerGetTimerID(t))->on_timer(); };
    static void on_own_light_cb(int value, void *arg) { static_cast<i_motion*>(arg)->own_value = value; };

    void on_motion();
    void on_timer();
    int read_light();                  // свежий замер; не вышло — последний из опроса
    bool dark_enough();                // обновляет гистерезис по свежему замеру; true — можно включать
    void stop_holding(const char *why);

  public:
    i_motion(device *_dev);
    ~i_motion();

    void from_json(cJSON *json) override;
    void get_state(cJSON *json) override;
    void start() override;
};

#endif // I_MOTION_H
