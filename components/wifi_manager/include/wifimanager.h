#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_proxy.h"
#include "esp_mac.h"
#include "vector"

#define STORAGE_WIFI "wifi"

#define KEY_INIT "init"

#define KEY_SSID "ssid_name"
#define KEY_SSID_PASSWORD "ssid_pswd"
#define KEY_AP "ap_name"
#define KEY_AP_PASSWORD "ap_pswd"

#define KEY_WIFI_MODE "wifi_mode"
#define KEY_NAME_MDNS "mdns_name"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_AP_STARTED_BIT BIT2
#define WIFI_STA_CONNECTED_BIT BIT3

class wifimanager {

  private:
    const char *TAG = "wifimanager";

    NVSProxy settings;
    SemaphoreHandle_t mutex = nullptr;
    EventGroupHandle_t wifi_event_group = nullptr;
    esp_netif_t* sta_netif = nullptr;
    esp_netif_t* ap_netif = nullptr;

    wifi_mode_t mode = WIFI_MODE_NULL;
    std::string mdns;

    std::string ssid_name;
    std::string ssid_pswd;
    std::string ap_name;
    std::string ap_pswd;

    bool is_ready = false;
    bool is_scan = false;

    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

  public:
    wifimanager();
    ~wifimanager();

    bool init(void);
    bool wifi_init_sta(void);
    bool wifi_init_ap(void);
    std::vector<wifi_ap_record_t> scan_wifi_networks();

    bool get_info_sta(std::string& ssid, std::string& password);
    bool set_sta(const std::string& ssid, const std::string& password, bool need_save);

    bool get_info_ap(std::string& ap, std::string& password);
    bool set_ap(const std::string& ap, const std::string& password, bool need_save);

    wifi_mode_t get_mode() { return mode; };
    bool set_mode(wifi_mode_t _mode, bool need_save);

    std::string get_mdns_name() { return mdns; };
    bool set_mdns(const std::string& _mdns, bool need_save);

    bool ready() { return is_ready; };
};

#endif // WIFIMANAGER_H