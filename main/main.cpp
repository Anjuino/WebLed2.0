#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h" 
#include "esp_timer.h"

#include "DeviceManager.h"
#include "led.h"
#include "nvs_proxy.h"
#include "wifimanager.h"
#include "server.h"
#include "device.h"

class wifimanager *wifi = nullptr;
class server *serv = nullptr;
class device *contorller = nullptr;

static const char *TAG = "main";

extern "C" void app_main ()
{
  NVSProxy::nvs_init();
  wifi = new wifimanager();
  wifi->init();

  uint64_t timer = (esp_timer_get_time() / 1000) + 10000;
  while(!wifi->ready()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "wifi init...");
    if(timer < (esp_timer_get_time() / 1000)) {
      wifi->set_ap("Equalizer", "12345678", false);
      wifi->init();
      break;
    }
  }

  serv = new server(wifi, new device());
  esp_err_t ret = serv->start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server");
    return;
  }

  vTaskDelete(NULL);
}