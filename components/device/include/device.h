#ifndef DEVICE_H
#define DEVICE_H

#define STORAGE_DEVICE "device"
#define KEY_INIT "init"
#define KEY_CONFIG "config"

#include "nvs_proxy.h"
#include "sntp_time.h"
#include "server.h"
#include "cJSON.h"
#include "DeviceManager.h"
#include "led.h"
#include "script_engine.h"
#include "filesystem.h"
#include "button.h"
#include "light_sensor.h"

class device {
  private:
    NVSProxy settings;
    class led *wled = nullptr;
    class FileSystem *fs = nullptr;

  public:
    class button *btn = nullptr;
    class light_sensor *l_sensor = nullptr;
    sntp_time t_time;
    device();
    ~device();

    led *get_led();
    FileSystem *get_fs();
    void register_handlers(httpd_handle_t httpd);
};



#endif // DEVICE_H
