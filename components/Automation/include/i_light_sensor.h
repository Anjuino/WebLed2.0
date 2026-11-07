#ifndef I_LIGHT_SENSOR_H
#define I_LIGHT_SENSOR_H

#include <atomic>
#include "automation.h"
#include "light_sensor.h"

class i_light_sensor : public Automation {
  public:
    static constexpr uint8_t PIN = 2;                // пин фоторезистора (ADC1); тот же читает датчик движения
    static constexpr uint32_t PERIOD_MS = 10000;

  private:
    light_sensor sensor;
    uint8_t pin = PIN;
    uint32_t period_ms = PERIOD_MS;

    int dark = 300;
    int light = 500;
    std::atomic<int> last_value{-1};   // читает датчик движения из другой задачи; -1 — замера ещё не было

    bool is_dark = false;              // текущее решение по датчику
    bool wanting = false;              // на прошлом опросе хотели ленту
    bool applied = false;              // своё состояние уже выставили на ленту

    led_state state;

    static void on_light_cb(int value, void *arg) { static_cast<i_light_sensor*>(arg)->on_light(value); };

    void on_light(int value);

  public:
    i_light_sensor(device *_dev);

    void from_json(cJSON *json) override;
    void get_state(cJSON *json) override;
    void start() override;

    int get_value() { return last_value; };   // последний замер, даже при is_active = false
    int read_now() { return sensor.read_now(); };   // свежий замер прямо сейчас; -1 — не удалось
};

#endif // I_LIGHT_SENSOR_H
