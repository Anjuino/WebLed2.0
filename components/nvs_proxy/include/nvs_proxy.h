#ifndef NVSPROXY_H
#define NVSPROXY_H

#include "nvs_flash.h"
#include "esp_log.h"
#include <cstring>
#include <string>
#include <type_traits>

class NVSProxy {
  private:
    const char* TAG = "nvs_proxy";

    nvs_handle_t handle = 0;
    const char* namespace_name;
    bool is_readonly;

  public:
    NVSProxy(const char* namespace_name, bool readonly = true);
    ~NVSProxy();

    static void nvs_init()
    {
      esp_err_t err = nvs_flash_init();
      if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
      }
      ESP_ERROR_CHECK(err);
    }

    bool commit() {
      if (handle == 0 || is_readonly) {
        ESP_LOGW(TAG, "Cannot commit: handle not open or readonly");
        return false;
      }
      esp_err_t err = nvs_commit(handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
        return false;
      }
      return true;
    }

    template<typename T>
    T get(const char* key, T default_value = T{}) {
      if (handle == 0) {
        ESP_LOGW(TAG, "NVS not open, returning default for key '%s'", key);
        return default_value;
      }

      T value;
      esp_err_t err;

      if constexpr (std::is_same_v<T, uint8_t>) {
        err = nvs_get_u8(handle, key, &value);
      } else if constexpr (std::is_same_v<T, int8_t>) {
        err = nvs_get_i8(handle, key, &value);
      } else if constexpr (std::is_same_v<T, uint16_t>) {
        err = nvs_get_u16(handle, key, &value);
      } else if constexpr (std::is_same_v<T, int16_t>) {
        err = nvs_get_i16(handle, key, &value);
      } else if constexpr (std::is_same_v<T, uint32_t>) {
        err = nvs_get_u32(handle, key, &value);
      } else if constexpr (std::is_same_v<T, int32_t>) {
        err = nvs_get_i32(handle, key, &value);
      } else if constexpr (std::is_same_v<T, uint64_t>) {
        err = nvs_get_u64(handle, key, &value);
      } else if constexpr (std::is_same_v<T, int64_t>) {
        err = nvs_get_i64(handle, key, &value);
      } else {
        static_assert(sizeof(T) == 0, "Unsupported type for NVS get");
      }

      if (err == ESP_OK) {
        return value;
      }

      if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to get key '%s': %s", key, esp_err_to_name(err));
      }
      return default_value;
    }

    std::string get(const char* key, std::string default_value = "") {
      if (handle == 0) {
        ESP_LOGW(TAG, "NVS not open, returning default for key '%s'", key);
        return default_value;
      }

      size_t length = 0;
      esp_err_t err = nvs_get_str(handle, key, NULL, &length);
      if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
          ESP_LOGW(TAG, "Failed to get key '%s': %s", key, esp_err_to_name(err));
        }
        return default_value;
      }

      std::string result;
      result.resize(length);
      err = nvs_get_str(handle, key, result.data(), &length);
      if (err != ESP_OK) {
        return default_value;
      }
      result.resize(length - 1);
      return result;
    }

    template<typename T>
    bool set(const char* key, T value, bool commit = false) {
      nvs_handle_t write_handle = handle;
      bool need_close = false;
      
      if (is_readonly || handle == 0) {
        esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &write_handle);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
          return false;
        }
        need_close = true;
      }

      esp_err_t err;
      
      if constexpr (std::is_same_v<T, uint8_t>) {
        err = nvs_set_u8(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, int8_t>) {
        err = nvs_set_i8(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, uint16_t>) {
        err = nvs_set_u16(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, int16_t>) {
        err = nvs_set_i16(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, uint32_t>) {
        err = nvs_set_u32(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, int32_t>) {
        err = nvs_set_i32(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, uint64_t>) {
        err = nvs_set_u64(write_handle, key, value);
      } else if constexpr (std::is_same_v<T, int64_t>) {
        err = nvs_set_i64(write_handle, key, value);
      } else {
        static_assert(sizeof(T) == 0, "Unsupported type for NVS set");
      }

      if (commit && err == ESP_OK) {
        err = nvs_commit(write_handle);
      }

      if (need_close && write_handle != 0) {
        nvs_close(write_handle);
      }

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set key '%s': %s", key, esp_err_to_name(err));
        return false;
      }

      return true;
    }

    bool set(const char* key, const std::string& value, bool commit = false) {
      return set_string(key, value.c_str(), commit);
    }

    bool set(const char* key, const char* value, bool commit = false) {
      return set_string(key, value, commit);
    }

    bool set_string(const char* key, const char* value, bool commit = false) {
      nvs_handle_t write_handle = handle;
      bool need_close = false;
      
      if (is_readonly || handle == 0) {
        esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &write_handle);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
          return false;
        }
        need_close = true;
      }

      esp_err_t err = nvs_set_str(write_handle, key, value);
      if (commit && err == ESP_OK) {
        err = nvs_commit(write_handle);
      }

      if (need_close && write_handle != 0) {
        nvs_close(write_handle);
      }

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set string key '%s': %s", key, esp_err_to_name(err));
        return false;
      }

      return true;
    }

    template<typename T>
    bool set_struct(const char* key, const T& value) {
      static_assert(std::is_trivially_copyable_v<T>, "Struct must be trivially copyable");
      return set_blob(key, &value, sizeof(T));
    }

    template<typename T>
    bool get_struct(const char* key, T& out_value) {
      static_assert(std::is_trivially_copyable_v<T>, "Struct must be trivially copyable");
      size_t size = sizeof(T);
      return get_blob(key, &out_value, size);
    }

    template<typename T>
    T get_struct(const char* key, T default_value = T{}) {
      static_assert(std::is_trivially_copyable_v<T>, "Struct must be trivially copyable");
      T value;
      if (get_struct(key, value)) {
        return value;
      }
      return default_value;
    }

    bool set_blob(const char* key, const void* data, size_t size, bool commit = false) {
      nvs_handle_t write_handle = handle;
      bool need_close = false;
      
      if (is_readonly || handle == 0) {
        esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &write_handle);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
          return false;
        }
        need_close = true;
      }

      esp_err_t err = nvs_set_blob(write_handle, key, data, size);
      if (commit && err == ESP_OK) {
        err = nvs_commit(write_handle);
      }

      if (need_close && write_handle != 0) {
        nvs_close(write_handle);
      }

      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob key '%s': %s", key, esp_err_to_name(err));
        return false;
      }

      return true;
    }

    bool get_blob(const char* key, void* out_data, size_t& size) {
      if (handle == 0) {
        ESP_LOGW(TAG, "NVS not open for blob key '%s'", key);
        return false;
      }

      esp_err_t err = nvs_get_blob(handle, key, out_data, &size);
      if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
          ESP_LOGW(TAG, "Failed to get blob key '%s': %s", key, esp_err_to_name(err));
        }
        return false;
      }
      return true;
    }

    bool is_open() const { return handle != 0; }

    bool exists(const char* key) {
      if (!is_open()) return false;

      uint8_t temp;
      esp_err_t err = nvs_get_u8(handle, key, &temp);
      return (err == ESP_OK);
    }
};

#endif // NVSPROXY_H