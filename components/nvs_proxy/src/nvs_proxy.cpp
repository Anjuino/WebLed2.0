#include "nvs_proxy.h"

NVSProxy::NVSProxy(const char* namespace_name, bool readonly)
    : namespace_name(namespace_name), is_readonly(readonly) 
{
  nvs_open_mode_t mode = readonly ? NVS_READONLY : NVS_READWRITE;
  esp_err_t err = nvs_open(namespace_name, mode, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace_name, esp_err_to_name(err));
    handle = 0;
  }
}

NVSProxy::~NVSProxy() {
  if (handle != 0) {
    nvs_close(handle);
  }
}