#include "stdint.h"
#include "stdbool.h"
#include "freertos/FreeRTOS.h" 

#include "MemoryManager.h"
#include "led.h"

#include "nvs_proxy.h"

class led *wLed = nullptr;

extern "C" void app_main ()
{
  NVSProxy::nvs_init();
  vTaskDelay(pdMS_TO_TICKS(1000));

  // тут запуск wifimanagera
  //vTaskDelay(pdMS_TO_TICKS(1000));

  wLed = new led();
  // тут запуск сервера с передачей в него ленты
  //vTaskDelay(pdMS_TO_TICKS(1000));

  xTaskCreate(led::task_entry, "Led", 4 * 1024, wLed, 10, NULL);
  vTaskDelay(pdMS_TO_TICKS(500));
  xTaskCreate(memory_task, "WatchMemory", 4 * 1024, NULL, 3, NULL);
  vTaskDelete(NULL);
}