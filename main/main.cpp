#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h" 
#include "esp_timer.h"

#include "filesystem.h"
#include "DeviceManager.h"
#include "led.h"
#include "nvs_proxy.h"
#include "wifimanager.h"
#include "server.h"
#include "script_engine.h"


class led *wLed = nullptr;
class wifimanager *wifi = nullptr;
class server *serv = nullptr;
class FileSystem *fs = nullptr;

const char *TAG = "main";

extern "C" void app_main ()
{
  NVSProxy::nvs_init();
  vTaskDelay(pdMS_TO_TICKS(100));
  wifi = new wifimanager();
  //wifi->set_ap("WledTest", "87654321", true);
  //wifi->set_sta("TP-Link_467D", "66484608", false);
  wifi->init();

  uint64_t timer = (esp_timer_get_time() / 1000) + 5000;
  while(!wifi->ready()) {
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGI(TAG, "wifi init...");
    if(timer < (esp_timer_get_time() / 1000)) {
      wifi->set_ap("Equalizer", "12345678", false);
      wifi->init();
      break;
    }
  }

  fs = new FileSystem();
  xTaskCreate(FileSystem::task_entry, "FileSystem", 4 * 1024, fs, 2, NULL);
  vTaskDelay(pdMS_TO_TICKS(100));

  wLed = new led();
  serv = new server(wLed, fs, wifi);

  esp_err_t ret = serv->start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server");
    return;
  }

  xTaskCreatePinnedToCore(led::task_entry, "Led", 8 * 1024, wLed, 14, NULL, 1);
  xTaskCreate(memory_task, "WatchDevice", 4 * 1024, NULL, 2, NULL);

  script_engine_start(wLed, fs);

  vTaskDelete(NULL);
}