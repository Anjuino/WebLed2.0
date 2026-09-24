#ifndef DEVICE_H
#define DEVICE_H

#define STORAGE_DEVICE "device"
#define KEY_VERSION "ver_cfg"
#define KEY_BUTTON_CONFIG "btn_cfg"
#define KEY_LIGHT_SENSOR_CONFIG "light_cfg"

#include <memory>
#include "nvs_proxy.h"
#include "sntp_time.h"
#include "server.h"
#include "cJSON.h"
#include "DeviceManager.h"
#include "led.h"
#include "script_engine.h"
#include "filesystem.h"
#include "automation.h"

class device {
  private:
    led *wled = nullptr;
    FileSystem *fs = nullptr;

    std::vector<std::unique_ptr<Automation>> automations;   // пул
    Automation *current_owner = nullptr;                    // кто держит ленту
    SemaphoreHandle_t owner_mutex;                          // engage зовут из разных задач

    bool time_synced = false;                               // время получено (SNTP)

    bool manual_active = false;                             // ручное управление — всем engage отказ

  public:
    NVSProxy settings;
    sntp_time t_time;
    device();
    ~device();

    led *get_led() { return (wled !=0) ?  wled : nullptr; };
    FileSystem *get_fs() { return (fs !=0) ?  fs : nullptr; };
    void register_handlers(httpd_handle_t httpd);

    bool engage(Automation *who);
    void release(Automation *who);
    void set_manual(bool on);   // вкл — автоматика заблокирована, пока пользователь не выключит
    bool is_manual() { return manual_active; };
    std::string get_owner_name();   // "" — лентой никто из автоматики не владеет
    bool has_automations() { return !automations.empty(); };

    bool is_time_synced();
};



#endif // DEVICE_H
