#ifndef SNTP_TIME_H
#define SNTP_TIME_H

#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <string>

class sntp_time {

  private:
    bool is_init = false;

  public:
    sntp_time() {};
    ~sntp_time() {};
    tm get_current_time();
    std::string get_str_current_time();
    void sync();
};

#endif // SNTP_TIME_H