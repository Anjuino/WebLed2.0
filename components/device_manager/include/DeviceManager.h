#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/temperature_sensor.h"
#include "version.h"

struct memory_info {
  size_t total;
  size_t free;
  size_t min_free;
  size_t psram_total;
  size_t psram_free;
  size_t psram_min_free;
  float temperature;
};


void memory_task(void *pvParameters);
memory_info getstatus();


#define DEVICE_META_SERIAL_MAX_LEN 31
#define DEVICE_META_PROJECT_MAX_LEN 63
bool device_meta_read_serial(char *out, size_t out_size);
bool device_meta_read_project(char *out, size_t out_size);
bool device_meta_is_provisioned(void);

#endif // DEVICEMANAGER_H
