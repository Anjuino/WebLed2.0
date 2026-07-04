#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" 

void memory_task(void *pvParameters);

#endif // MEMORYMANAGER_H