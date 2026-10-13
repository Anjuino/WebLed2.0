#ifndef AUTOMATION_H
#define AUTOMATION_H


#include <cstdint>
#include <string>
#include "cJSON.h"

class device;

struct led_state {
  uint8_t mode;
  uint8_t speed;
  uint8_t brightness;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

class Automation {
  friend class device;

  private:
    led_state saved = {};      // состояние ленты до нашего захвата
    bool has_saved = false;

  protected:
    uint8_t priority;
    std::string name;
    device *dev = nullptr;

    bool is_active = true;                          // сохранённый флаг «включён» — читается из JSON

    // окно активности, минуты от полуночи
    bool has_active_time = false;
    uint16_t active_start = 0;
    uint16_t active_stop = 0;

    bool in_active_time();   // нет окна или нет времени — true

  public:

    Automation(device *_dev) { dev = _dev; };
    virtual ~Automation() = default;

    virtual void from_json(cJSON *json) = 0;
    virtual void get_state(cJSON *json) = 0;
    virtual void start() = 0;

    uint8_t get_priority() { return priority; };
    std::string get_name() { return name; };
    bool get_active() { return is_active; };
    void set_active(bool on) { is_active = on; };
};

#endif // AUTOMATION_H