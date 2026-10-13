#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
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

void check_app()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  esp_ota_get_state_partition(running, &state);

  const char *state_str = (state == ESP_OTA_IMG_VALID) ? "VALID" :
                          (state == ESP_OTA_IMG_PENDING_VERIFY) ? "PENDING_VERIFY" :
                          (state == ESP_OTA_IMG_INVALID) ? "INVALID" : "UNDEFINED";

  esp_reset_reason_t rst = esp_reset_reason();
  bool was_rollback = (state == ESP_OTA_IMG_VALID) &&
                      (rst == ESP_RST_PANIC || rst == ESP_RST_TASK_WDT ||
                       rst == ESP_RST_INT_WDT || rst == ESP_RST_BROWNOUT);

  if (was_rollback) {
    ESP_LOGE("BOOT", "rollback to %s, version %s", running->label, firmware_version());
  } else {
    ESP_LOGI("BOOT", "running=%s, state=%s, version %s", running->label, state_str, firmware_version());
  }
}

extern "C" void app_main ()
{
  check_app();

  char serial[32];
  char project[64];
  if (device_meta_read_serial(serial, sizeof(serial)) &&
      device_meta_read_project(project, sizeof(project))) {
    ESP_LOGI(TAG, "device: %s, %s", project, serial);
  } else {
    ESP_LOGW(TAG, "device: not provisioned");
  }

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

  vTaskDelay(pdMS_TO_TICKS(60 * 1000));
  esp_ota_mark_app_valid_cancel_rollback();
  vTaskDelete(NULL);
}