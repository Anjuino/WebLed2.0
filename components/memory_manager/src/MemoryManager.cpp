#include "MemoryManager.h"


void memory_task(void *pvParameters)
{
    static const char* TAG = "Mem";
    while (true)
    {
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "=== Memory ===");
        ESP_LOGI(TAG, "Free: %zu bytes",     internal_free);
        ESP_LOGI(TAG, "Min free: %zu bytes", internal_min_free);
        ESP_LOGI(TAG, "Total: %zu bytes",    internal_total);
        
        vTaskDelay(pdMS_TO_TICKS(120000));
    }
}