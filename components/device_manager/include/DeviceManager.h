#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" 
#include "driver/temperature_sensor.h"

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
#endif // MEMORYMANAGER_H