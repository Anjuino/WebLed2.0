#ifndef I_SCHEDULE_H
#define I_SCHEDULE_H

#include "automation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class i_schedule : public Automation {
  private:
    static constexpr uint8_t MAX_INTERVALS = 5;
    static constexpr uint8_t MAX_DAYS = 7;
    static constexpr uint32_t TICK_MS = 15 * 1000;

    struct interval_t {
      uint16_t start = 0;
      uint16_t stop = 0;
      led_state state = {};
      bool valid = false;
    };

    struct day_t {
      int8_t wday = -1;
      interval_t intervals[MAX_INTERVALS];
      uint8_t intervals_count = 0;
    };

    day_t days[MAX_DAYS];
    uint8_t days_count = 0;

    TimerHandle_t tick_timer = nullptr;
    bool applied = false;                  // своё состояние сейчас на ленте (мы владелец)
    bool wanting = false;                  // на прошлом тике был активный интервал
    led_state *last_applied = nullptr;

    static void on_tick_cb(TimerHandle_t t) { static_cast<i_schedule*>(pvTimerGetTimerID(t))->evaluate(); }
    void evaluate();

    static int8_t parse_wday(cJSON *obj);
    static bool parse_hm(cJSON *obj, uint16_t &out_min);
    static bool parse_led(cJSON *obj, led_state &out);

    led_state *find_current_interval(int today_wday, uint16_t now_min);

  public:
    i_schedule(device *_dev);
    ~i_schedule();

    void from_json(cJSON *json) override;
    void get_state(cJSON *json) override;
    void start() override;
};

#endif // I_SCHEDULE_H
