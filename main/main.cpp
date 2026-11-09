#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

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

static constexpr gpio_num_t FACTORY_RESET_GPIO = GPIO_NUM_7;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
static constexpr uint32_t FACTORY_RESET_POLL_MS = 250;

static void factory_reset_task(void *)
{
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << FACTORY_RESET_GPIO;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;

  if (gpio_config(&cfg) != ESP_OK) {
    ESP_LOGE(TAG, "factory reset: gpio_config failed");
    vTaskDelete(nullptr);
    return;
  }

  bool pressed = false;
  TickType_t pressed_at = 0;

  while (true) {
    bool low = gpio_get_level(FACTORY_RESET_GPIO) == 0;

    if (low && !pressed) {
      pressed = true;
      pressed_at = xTaskGetTickCount();
      ESP_LOGI(TAG, "factory reset: button pressed, hold 10s");
    } else if (!low && pressed) {
      pressed = false;
      ESP_LOGI(TAG, "factory reset: cancelled");
    } else if (pressed &&
               (xTaskGetTickCount() - pressed_at) >= pdMS_TO_TICKS(FACTORY_RESET_HOLD_MS)) {
      ESP_LOGW(TAG, "factory reset: erasing NVS");

      esp_err_t err = ESP_FAIL;
      for (int i = 0; i < 3; i++) {
        err = nvs_flash_erase();
        if (err == ESP_OK) break;
        ESP_LOGE(TAG, "factory reset: nvs_flash_erase failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if (err == ESP_OK) {
        ESP_LOGW(TAG, "factory reset: NVS erased, restarting");
      } else {
        ESP_LOGE(TAG, "factory reset: failed, restarting anyway");
      }
      esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_POLL_MS));
  }
}

void check_app()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  esp_ota_get_state_partition(running, &state);

  const char *state_str = (state == ESP_OTA_IMG_VALID) ? "VALID" :
                          (state == ESP_OTA_IMG_PENDING_VERIFY) ? "PENDING_VERIFY" :
                          (state == ESP_OTA_IMG_INVALID) ? "INVALID" : "UNDEFINED";

  esp_reset_reason_t rst = esp_reset_reason();
  bool was_crash = (rst == ESP_RST_PANIC || rst == ESP_RST_TASK_WDT ||
                    rst == ESP_RST_INT_WDT || rst == ESP_RST_BROWNOUT);

  NVSProxy boot_nvs("boot", false);
  std::string last = boot_nvs.get("last_partition", "");

  if (last.empty()) {
    ESP_LOGI("BOOT", "running=%s, state=%s, rst=%d, version %s (first boot)",
             running->label, state_str, rst, firmware_version());
    boot_nvs.set("last_partition", running->label, true);
  } else if (last != running->label) {
    if (was_crash) {
      ESP_LOGE("BOOT", ">>> ROLLBACK: %s -> %s, rst=%d, version %s <<<",
               last.c_str(), running->label, rst, firmware_version());
    } else {
      ESP_LOGW("BOOT", "switched: %s -> %s (OTA update), version %s",
               last.c_str(), running->label, firmware_version());
    }
    boot_nvs.set("last_partition", running->label, true);
  } else {
    ESP_LOGI("BOOT", "running=%s, state=%s, rst=%d, version %s",
             running->label, state_str, rst, firmware_version());
  }
}

extern "C" void app_main ()
{
  NVSProxy::nvs_init();

  if (xTaskCreate(factory_reset_task, "factory_reset", 3 * 1024, nullptr, 2, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "failed to start factory reset task");
  }

  check_app();

  char serial[32];
  char project[64];
  if (device_meta_read_serial(serial, sizeof(serial)) &&
      device_meta_read_project(project, sizeof(project))) {
    ESP_LOGI(TAG, "device: %s, %s", project, serial);
  } else {
    ESP_LOGW(TAG, "device: not provisioned");
  }

  contorller = new device();

  wifi = new wifimanager();
  wifi->init();

  uint64_t timer = (esp_timer_get_time() / 1000) + 15000;
  while(!wifi->ready()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "wifi init...");
    if(timer < (esp_timer_get_time() / 1000)) {
      wifi->start_ap();
      break;
    }
  }

  serv = new server(wifi, contorller);
  esp_err_t ret = serv->start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start server");
    return;
  }

  esp_err_t mark = esp_ota_mark_app_valid_cancel_rollback();
  if (mark != ESP_OK) ESP_LOGW(TAG, "mark app valid: %s", esp_err_to_name(mark));
  vTaskDelete(NULL);
}