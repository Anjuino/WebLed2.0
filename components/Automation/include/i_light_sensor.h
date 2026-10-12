#ifndef I_LIGHT_SENSOR_H
#define I_LIGHT_SENSOR_H

#include "automation.h"
#include "light_sensor.h"

class i_light_sensor : public Automation {
  private:
    light_sensor sensor;              
    uint8_t pin = 2;
    uint32_t period_ms = 10000;

    int dark = 300;
    int light = 500;
    int last_value = -1;

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
};

#endif // I_LIGHT_SENSOR_H
