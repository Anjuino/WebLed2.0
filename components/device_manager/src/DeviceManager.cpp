#include "DeviceManager.h"
#include "esp_partition.h"
#include <string.h>

temperature_sensor_handle_t temp_handle = NULL;

static const char *META_TAG = "device_meta";
static constexpr uint32_t META_MAGIC = 0xDEADBEEF;
static constexpr size_t META_PROJECT_SIZE = 64;
static constexpr size_t META_SERIAL_SIZE = 32;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  char project[META_PROJECT_SIZE];
  char serial[META_SERIAL_SIZE];
  uint32_t crc32;
} device_meta_t;

static const esp_partition_t *find_meta_partition(void) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x83, "meta");
}

static bool copy_field(const char *src, size_t src_size, char *out, size_t out_size) {
  if (!out || out_size == 0) return false;
  char buf[src_size];
  memcpy(buf, src, src_size);
  buf[src_size - 1] = '\0';
  strncpy(out, buf, out_size - 1);
  out[out_size - 1] = '\0';
  return true;
}

static bool read_meta_struct(device_meta_t *m) {
  const esp_partition_t *p = find_meta_partition();
  if (!p) {
    ESP_LOGE(META_TAG, "meta partition not found in table");
    return false;
  }
  if (esp_partition_read(p, 0, m, sizeof(*m)) != ESP_OK) {
    ESP_LOGE(META_TAG, "meta read failed");
    return false;
  }
  if (m->magic != META_MAGIC) {
    return false;
  }
  return true;
}

bool device_meta_is_provisioned(void) {
  const esp_partition_t *p = find_meta_partition();
  if (!p) return false;
  device_meta_t m;
  if (esp_partition_read(p, 0, &m, sizeof(m)) != ESP_OK) return false;
  return (m.magic == META_MAGIC);
}

bool device_meta_read_serial(char *out, size_t out_size) {
  device_meta_t m;
  if (!read_meta_struct(&m)) return false;
  return copy_field(m.serial, META_SERIAL_SIZE, out, out_size);
}

bool device_meta_read_project(char *out, size_t out_size) {
  device_meta_t m;
  if (!read_meta_struct(&m)) return false;
  return copy_field(m.project, META_PROJECT_SIZE, out, out_size);
}

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
    
    vTaskDelay(pdMS_TO_TICKS(60000 * 2));
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