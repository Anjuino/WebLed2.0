#include "DeviceManager.h"

temperature_sensor_handle_t temp_handle = NULL;

void memory_task(void *pvParameters)
{
    static const char* TAG = "Mem";
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = 20,
        .range_max = 80,
        .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT,
    };

    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
    while (true)
    {
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "=== Memory ===");
        ESP_LOGI(TAG, "Free: %zu bytes",     internal_free);
        ESP_LOGI(TAG, "Min free: %zu bytes", internal_min_free);
        ESP_LOGI(TAG, "Total: %zu bytes",    internal_total);
        ESP_LOGI(TAG, "=== PSRAM ===");
        ESP_LOGI(TAG, "Free: %zu bytes",     psram_free);
        ESP_LOGI(TAG, "Min free: %zu bytes", psram_min_free);
        ESP_LOGI(TAG, "Total: %zu bytes",    psram_total);

        float temp_cpu;
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &temp_cpu));
        ESP_LOGI(TAG, "Temperature: %.1f C", temp_cpu);
        
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

memory_info getstatus(void) {
    memory_info mem;
    mem.free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    mem.total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    mem.min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    mem.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mem.psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    mem.psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    float temp_cpu;
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &temp_cpu));
    mem.temperature = temp_cpu;
    return mem;
}