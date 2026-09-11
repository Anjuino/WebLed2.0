#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h" 
#include "esp_timer.h"

#include "esp_littlefs.h"
#include "DeviceManager.h"
#include "led.h"
#include "nvs_proxy.h"
#include "mdns.h"
#include "wifimanager.h"
#include "server.h"
#include "script_engine.h"


class led *wLed = nullptr;
class wifimanager *wifi = nullptr;
class server *serv = nullptr;

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
    ESP_LOGE(TAG, "Ошибка инициализации mDNS: %d", ret);
    return;
  }

  ret = mdns_hostname_set(wifi->get_mdns_name().c_str());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Ошибка установки hostname: %d", ret);
    return;
  }

  ESP_LOGI(TAG, "hostname: %s", wifi->get_mdns_name().c_str());
}

extern "C" void app_main ()
{
  NVSProxy::nvs_init();
  vTaskDelay(pdMS_TO_TICKS(100));
  wifi = new wifimanager();
  //wifi->set_ap("WledTest", "87654321", true);
  wifi->set_sta("TP-Link_467D", "66484608", false);
  wifi->init();

  uint64_t timer = (esp_timer_get_time() / 1000) + 5000;
  while(!wifi->ready()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "wifi init...");
    if(timer < (esp_timer_get_time() / 1000)) {
      wifi->set_ap("Equalizer", "12345678", false);
      wifi->init();
      break;
    }
  }

  start_mdns();
  vTaskDelay(pdMS_TO_TICKS(100));

  wLed = new led();
  script_engine_start(wLed);

  static const char *demo_src =
    "hue;\n"
    "function step() {\n"
    "    r; g; b; h;\n"
    "    if (hue < 85) { r = 255 - hue * 3; g = hue * 3; b = 0; }\n"
    "    else if (hue < 170) { h = hue - 85; r = 0; g = 255 - h * 3; b = h * 3; }\n"
    "    else { h = hue - 170; r = h * 3; g = 0; b = 255 - h * 3; }\n"
    "    led::setPixel(0, r, g, b);\n"
    "    led::show();\n"
    "    hue = hue + 1;\n"
    "    if (hue >= 255) hue = 0;\n"
    "    led::delay(20);\n"
    "}\n";
  script_engine_compile_and_register("WrenchDemo", 3, true, demo_src);

  static const char *sin_src =
    "t;\n"
    "function step() {\n"
    "    r; g; b;\n"
    "    r = (int)(127.5 + 127.5 * sin(t * 0.05));\n"
    "    g = (int)(127.5 + 127.5 * cos(t * 0.04));\n"
    "    b = (int)(127.5 + 127.5 * sin(t * 0.03 + 1.5));\n"
    "    led::setPixel(0, r, g, b);\n"
    "    led::show();\n"
    "    t = t + 1;\n"
    "    led::delay(20);\n"
    "}\n";
  script_engine_compile_and_register("WrenchSin", 4, true, sin_src);

  serv = new server(wLed);

  esp_err_t ret = serv->start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  xTaskCreatePinnedToCore(led::task_entry, "Led", 8 * 1024, wLed, 14, NULL, 1);
  vTaskDelay(pdMS_TO_TICKS(100));
  xTaskCreate(memory_task, "WatchDevice", 4 * 1024, NULL, 2, NULL);
  vTaskDelete(NULL);
}