#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h" 

#include "esp_littlefs.h"
#include "MemoryManager.h"
#include "led.h"
#include "nvs_proxy.h"
#include "mdns.h"
#include "wifimanager.h"
#include "https_server.h"

class led *wLed = nullptr;
class wifimanager *wifi = nullptr;
const char *TAG = "main";

void initFS()
{
  esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "storage",
    .format_if_mount_failed = true,
    .dont_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    }
    else if (ret == ESP_ERR_NOT_FOUND)
    {
      ESP_LOGE(TAG, "Failed to find LittleFS partition");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    }
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
  }
  else
  {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }

  // разово создать директорию для скриптов и рабоать с ней. проверять сущестование при старте, если нет то создать.
  /*ESP_LOGI(TAG, "Opening file");
  FILE *f = fopen("/littlefs/hello.txt", "r");
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), f) != NULL);
  ESP_LOGI(TAG, "Read: %s", buffer);
  fclose(f);*/
}

void start_mdns(void) 
{
  esp_err_t ret = mdns_init();
  if (ret != ESP_OK) {
    ESP_LOGE("MAIN", "Ошибка инициализации mDNS: %d", ret);
    return;
  }

  ret = mdns_hostname_set(wifi->get_mdns_name().c_str());
  if (ret != ESP_OK) {
    ESP_LOGE("MAIN", "Ошибка установки hostname: %d", ret);
  }
}
#include <sys/time.h>
extern "C" void app_main ()
{
  NVSProxy::nvs_init();
  vTaskDelay(pdMS_TO_TICKS(100));
  wifi = new wifimanager();
  //wifi->set_ap("WledTest", "87654321", true);
  //wifi->set_sta("TP-Link_467D", "66484608", true);
  //wifi->set_mode(WIFI_MODE_STA, true);
  wifi->init();

  while(!wifi->ready()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "wifi init...");
  }
  start_mdns();
  vTaskDelay(pdMS_TO_TICKS(100));
  wLed = new led();

  esp_err_t ret = https_server_start(443);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTPS server");
    return;
  }
  // тут запуск сервера с передачей в него ленты
  vTaskDelay(pdMS_TO_TICKS(1000));

  xTaskCreate(led::task_entry, "Led", 4 * 1024, wLed, 12, NULL);
  vTaskDelay(pdMS_TO_TICKS(500));
  xTaskCreate(memory_task, "WatchMemory", 4 * 1024, NULL, 2, NULL);
  vTaskDelete(NULL);
  
}