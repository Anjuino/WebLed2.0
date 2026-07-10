#include "wifimanager.h"


static std::string getHostname() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  
  char macStr[7];
  sprintf(macStr, "%02X%02X%02X", mac[3], mac[4], mac[5]);
  
  return "espled" + std::string(macStr);
}

static std::string getmode(wifi_mode_t _mode) {
  char mode[10];
  if(_mode == WIFI_MODE_STA) sprintf(mode, "%s", "STA");
  else if(_mode == WIFI_MODE_AP) sprintf(mode, "%s", "AP");
  else if(_mode == WIFI_MODE_NULL) sprintf(mode, "%s", "NULL");
  
  return std::string(mode);
}

wifimanager::wifimanager() : settings(STORAGE_WIFI, false) {
  wifi_event_group = xEventGroupCreate();
  if (wifi_event_group == NULL) {
    ESP_LOGE(TAG, "Failed to create event group");
  }

  mutex = xSemaphoreCreateMutex();
  if (mutex == nullptr) {
    ESP_LOGE(TAG, "Не удалось создать мьютекс!");
  }

  uint8_t init_flag = settings.get<uint8_t>(KEY_INIT, 0);

  if (init_flag == 0) {
    std::string hostname = getHostname();

    settings.set(KEY_WIFI_MODE, (uint8_t)WIFI_MODE_AP);
    settings.set(KEY_NAME_MDNS, hostname);
    settings.set(KEY_SSID, "12345678");
    settings.set(KEY_SSID_PASSWORD, "12345678");
    settings.set(KEY_AP, hostname);
    settings.set(KEY_AP_PASSWORD, "12345678");
    settings.set(KEY_INIT, (uint8_t)1, true);

  } else {
    mode = (wifi_mode_t)settings.get<uint8_t>(KEY_WIFI_MODE, (uint8_t)WIFI_MODE_AP);

    mdns = settings.get(KEY_NAME_MDNS, getHostname());

    ssid_name = settings.get(KEY_SSID, "12345678");
    ssid_pswd = settings.get(KEY_SSID_PASSWORD, "12345678");

    ap_name = settings.get(KEY_AP, getHostname());
    ap_pswd = settings.get(KEY_AP_PASSWORD, "12345678");

    ESP_LOGI(TAG, "Загружено из памяти: сеть:%s, имя по mDNS:%s, ssid:%s, имя точки доступа:%s", 
      getmode(mode).c_str(), mdns.c_str(), ssid_name.c_str(), ap_name.c_str());
  }
}

wifimanager::~wifimanager() {
  if (mutex != nullptr) {
    vSemaphoreDelete(mutex);
    mutex = nullptr;
  }

  if (sta_netif != nullptr) {
    esp_netif_destroy(sta_netif);
    sta_netif = nullptr;
  }

  if (ap_netif != nullptr) {
    esp_netif_destroy(ap_netif);
    ap_netif = nullptr;
  }

  if (wifi_event_group) {
    vEventGroupDelete(wifi_event_group);
  }
}

void wifimanager::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  wifimanager* instance = static_cast<wifimanager*>(arg);

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        ESP_LOGI(instance->TAG, "WiFi STA started");
        if(!instance->is_scan) esp_wifi_connect();
        break;

      case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(instance->TAG, "WiFi STA connected to AP");
        xEventGroupSetBits(instance->wifi_event_group, WIFI_STA_CONNECTED_BIT);
        instance->is_ready = true; 
        break;

      case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(instance->TAG, "WiFi STA disconnected");
        xEventGroupClearBits(instance->wifi_event_group, WIFI_CONNECTED_BIT);
        if(!instance->is_scan) esp_wifi_connect();
        instance->is_ready = false; 
        break;

      case WIFI_EVENT_AP_START:
        ESP_LOGI(instance->TAG, "WiFi AP started");
        xEventGroupSetBits(instance->wifi_event_group, WIFI_AP_STARTED_BIT);
        instance->is_ready = true; 
        break;

      case WIFI_EVENT_AP_STOP:
        ESP_LOGI(instance->TAG, "WiFi AP stopped");
        xEventGroupClearBits(instance->wifi_event_group, WIFI_AP_STARTED_BIT);
        instance->is_ready = false; 
        break;

      default:
        break;
    }
  }
}

void wifimanager::ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  wifimanager* instance = static_cast<wifimanager*>(arg);

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(instance->TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(instance->wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

std::vector<wifi_ap_record_t> wifimanager::scan_wifi_networks() {
  std::vector<wifi_ap_record_t> ap_records;

  // Проверяем, что WiFi инициализирован
  wifi_mode_t current_mode;
  if (esp_wifi_get_mode(&current_mode) != ESP_OK) {
    ESP_LOGE(TAG, "WiFi not initialized");
    return ap_records;
  }

  bool need_switch_back = false;
  is_scan = true;
  if (current_mode == WIFI_MODE_AP) {
    ESP_LOGI(TAG, "Switching to APSTA mode for scanning...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    need_switch_back = true;
    vTaskDelay(pdMS_TO_TICKS(1000));
  } else if (current_mode == WIFI_MODE_STA) {
    // В STA режиме все ок, сканируем как есть
    ESP_LOGI(TAG, "Scanning in STA mode...");
  } else {
    ESP_LOGE(TAG, "Invalid WiFi mode for scanning");
    return ap_records;
  }

  wifi_scan_config_t scan_config = {};

  scan_config.ssid = 0;
  scan_config.show_hidden = false;
  scan_config.scan_type =  WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 50;
  scan_config.scan_time.active.max = 100;

  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  if (ap_count > 0) {
    ap_records.resize(ap_count);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records.data()));
    ESP_LOGI(TAG, "Found %d APs", ap_count);

    for (int i = 0; i < ap_count; i++) {
      ESP_LOGI(TAG, "SSID: %s, RSSI: %d", ap_records[i].ssid, ap_records[i].rssi);
    }
  } else {
    ESP_LOGI(TAG, "No APs found");
  }

  if (need_switch_back) {
    ESP_LOGI(TAG, "Switching back to AP mode...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }

  is_scan = false;
  return ap_records;
}

bool wifimanager::wifi_init_ap(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
  assert(ap_netif);
  
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifimanager::wifi_event_handler, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifimanager::ip_event_handler, this));

  wifi_config_t wifi_config = {};

  if (!ap_name.empty()) {
    strlcpy((char*)wifi_config.ap.ssid, ap_name.c_str(), sizeof(wifi_config.ap.ssid));
  } else {
    strlcpy((char*)wifi_config.ap.ssid, "Led", sizeof(wifi_config.ap.ssid));
  }

  if (!ap_pswd.empty() && ap_pswd.length() >= 8) {
    strlcpy((char*)wifi_config.ap.password, ap_pswd.c_str(), sizeof(wifi_config.ap.password));
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  } else {
    wifi_config.ap.password[0] = '\0';  // Пустой пароль
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }
  
  // Остальные настройки
  wifi_config.ap.ssid_len = 0;
  wifi_config.ap.channel = 1;
  wifi_config.ap.ssid_hidden = 0;
  wifi_config.ap.max_connection = 4;
  wifi_config.ap.beacon_interval = 100;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  return true;
}

bool wifimanager::wifi_init_sta(void) {
  ESP_LOGI(TAG, "Initializing WiFi STA mode");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifimanager::wifi_event_handler, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifimanager::ip_event_handler, this));

  wifi_config_t wifi_config = {};

  if (!ssid_name.empty()) {
    strlcpy((char*)wifi_config.sta.ssid, ssid_name.c_str(), sizeof(wifi_config.sta.ssid));
  } else {
    ESP_LOGE(TAG, "SSID is empty!");
    return false;
  }
  
  // Копируем пароль
  if (!ssid_pswd.empty()) {
    strlcpy((char*)wifi_config.sta.password, ssid_pswd.c_str(), sizeof(wifi_config.sta.password));
  } else {
    wifi_config.sta.password[0] = '\0';
    ESP_LOGW(TAG, "Password is empty - connecting to open network");
  }
  
  // Настройки сканирования и подключения
  wifi_config.sta.scan_method = WIFI_FAST_SCAN;
  wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SECURITY;
  wifi_config.sta.threshold.rssi = -127;
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi STA started, connecting to SSID: %s", wifi_config.sta.ssid);
  
  return true;
}

bool wifimanager::init(void)
{
  if (mode == WIFI_MODE_AP) {
    return wifi_init_ap();
  } else if (mode == WIFI_MODE_STA) {
    return wifi_init_sta();
  }

  return false;
}

bool wifimanager::get_info_sta(std::string& ssid, std::string& password) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ssid = ssid_name;
    password = ssid_pswd;
    xSemaphoreGive(mutex);
    return !ssid.empty();
  }
  return false;
}

bool wifimanager::set_sta(const std::string& ssid, const std::string& password, bool need_save) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ssid_name = ssid;
    ssid_pswd = password;
    mode = WIFI_MODE_STA;
    
    settings.set(KEY_SSID, ssid);
    settings.set(KEY_SSID_PASSWORD, password);
    settings.set(KEY_WIFI_MODE, (uint8_t)WIFI_MODE_STA, need_save);

    xSemaphoreGive(mutex);
    ESP_LOGI(TAG, "Установка параметров подключения %s", ssid.c_str());
    return true;
  }
  return false;
}

bool wifimanager::get_info_ap(std::string& ap, std::string& password) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ap = ap_name;
    password = ap_pswd;
    xSemaphoreGive(mutex);
    return !ap.empty();
  }
  return false;
}

bool wifimanager::set_ap(const std::string& ap, const std::string& password, bool need_save) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ap_name = ap;
    ap_pswd = password;
    mode = WIFI_MODE_AP;

    settings.set(KEY_AP, ap);
    settings.set(KEY_AP_PASSWORD, password);
    settings.set(KEY_WIFI_MODE, (uint8_t)WIFI_MODE_AP, need_save);

    xSemaphoreGive(mutex);
    ESP_LOGI(TAG, "Установка имени точки доступа %s", ap_name.c_str());
    return true;
  }
  return false;
}

bool wifimanager::set_mode(wifi_mode_t _mode, bool need_save) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    mode = _mode;

    settings.set(KEY_WIFI_MODE, (uint8_t)mode, need_save);

    xSemaphoreGive(mutex);
    ESP_LOGI(TAG, "Установка режима работы %s",  getmode(_mode).c_str());
    return true;
  }
  return false;
}

bool wifimanager::set_mdns(const std::string& _mdns, bool need_save) {
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if(_mdns.length() < 16) {
      mdns = _mdns;

      settings.set(KEY_NAME_MDNS, _mdns, need_save);

      xSemaphoreGive(mutex);
      return true;
    }
  }
  return false;
}