#ifndef SNTP_TIME_H
#define SNTP_TIME_H

#include "esp_sntp.h"
#include "esp_log.h"
#include "nvs_proxy.h"
#include <cstdint>
#include <time.h>
#include <string>

class sntp_time {

  private:
    static constexpr int16_t DEFAULT_OFFSET_MIN = 7 * 60;
    static constexpr int16_t MIN_OFFSET_MIN = -12 * 60;
    static constexpr int16_t MAX_OFFSET_MIN = 14 * 60;

    NVSProxy settings;
    bool is_init = false;
    int16_t tz_offset_min = DEFAULT_OFFSET_MIN;

    void apply_timezone();

  public:
    sntp_time();
    ~sntp_time() {}
    tm get_current_time();
    std::string get_str_current_time();
    void sync();

    int16_t get_offset_min() const { return tz_offset_min; }
    bool set_offset_min(int16_t offset, bool persist = true);
};

#endif // SNTP_TIME_H
