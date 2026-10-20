#include "device.h"
#include "version.h"
#include "i_button.h"
#include "i_light_sensor.h"
#include "i_schedule.h"
#include <algorithm>
#include <cstring>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "device";

// конфиги автоматики храним blob'ом: строка в NVS ограничена 4000 байт,
// а полное расписание (7 дней × 5 интервалов) выходит за 5 КБ
std::string device::load_config(const char *key) {
  std::string raw = settings.get_blob_as_string(key, "");
  if (raw.empty()) raw = settings.get(key, "");   // старые прошивки писали строкой
  return raw;
}

bool device::save_config(const char *key, cJSON *json) {
  char *str = cJSON_PrintUnformatted(json);
  if (!str) return false;
  // ключ мог остаться строкой от старой прошивки — сносим его, чтобы не было двух типов под одним именем
  bool ok = settings.erase(key) && settings.set_blob(key, str, strlen(str));
  if (!ok) ESP_LOGE(TAG, "не удалось сохранить %s (%u байт)", key, (unsigned)strlen(str));
  free(str);
  return ok;
}

template <typename T>
void device::add_automation(const char *key) {
  std::string raw = load_config(key);
  if (raw.empty()) return;
  cJSON *cfg = cJSON_Parse(raw.c_str());
  if (!cfg) {
    ESP_LOGE(TAG, "%s: невалидный JSON в NVS", key);
    return;
  }
  auto item = std::make_unique<T>(this);
  item->from_json(cfg);
  cJSON_Delete(cfg);
  item->start();
  automations.push_back(std::move(item));
}

device::device() : settings(STORAGE_DEVICE, false) 
{
  owner_mutex = xSemaphoreCreateMutex();
  t_time.sync();
  wled = new led();
  xTaskCreatePinnedToCore(led::task_entry, "Led", 6 * 1024, wled, 10, NULL, 1);
  xTaskCreatePinnedToCore(memory_task, "WatchDevice", 4 * 1024, NULL, 2, NULL, 1);

  fs = new FileSystem();
  script_engine_start(wled, fs);

  add_automation<i_button>(KEY_BUTTON_CONFIG);
  add_automation<i_light_sensor>(KEY_LIGHT_SENSOR_CONFIG);
  add_automation<i_schedule>(KEY_SCHEDULE_CONFIG);
}

device::~device()
{

}

static esp_err_t api_device_handler_post(httpd_req_t *req) {
  device *contorller = (device*)req->user_ctx;
  char *buf = nullptr;
  if(getjson(req, &buf) == ESP_OK) {

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!root) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
      return ESP_FAIL;
    }

    static const struct { const char *json_key; const char *nvs_key; } sections[] = {
      { "button",       KEY_BUTTON_CONFIG },
      { "light_sensor", KEY_LIGHT_SENSOR_CONFIG },
      { "schedule",     KEY_SCHEDULE_CONFIG },
    };

    const char *failed = nullptr;
    bool saved = false;
    for (const auto &s : sections) {
      cJSON *item = cJSON_GetObjectItem(root, s.json_key);
      if (!cJSON_IsObject(item)) continue;
      if (!contorller->save_config(s.nvs_key, item)) {
        failed = s.json_key;
        break;
      }
      saved = true;
    }
    cJSON_Delete(root);

    if (saved) contorller->settings.commit();

    // не перезагружаемся при ошибке: иначе клиент думает, что всё сохранилось
    if (failed) {
      char msg[64];
      snprintf(msg, sizeof(msg), "nvs write failed: %s", failed);
      return send_json_status(req, "status", false, "error", msg);
    }

    ESP_LOGI(TAG, "Сохранил изменения");
    send_json_status(req, "status", true, nullptr, nullptr);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }
  return send_json_status(req, "status", false, "error", "fail parse json");
}

static esp_err_t api_device_handler_get(httpd_req_t *req) {
  device *contorller = (device*)req->user_ctx;
  cJSON *root = cJSON_CreateObject();
  if(!root) return send_resp_json(NULL, req);

  char value[256];
  if (get_query_param(req, "config", value, sizeof(value))) {
    cJSON *button = cJSON_Parse(contorller->load_config(KEY_BUTTON_CONFIG).c_str());
    if (button) cJSON_AddItemToObject(root, "button", button);

    cJSON *light_sensor = cJSON_Parse(contorller->load_config(KEY_LIGHT_SENSOR_CONFIG).c_str());
    if (light_sensor) cJSON_AddItemToObject(root, "light_sensor", light_sensor);

    cJSON *schedule = cJSON_Parse(contorller->load_config(KEY_SCHEDULE_CONFIG).c_str());
    if (schedule) cJSON_AddItemToObject(root, "schedule", schedule);

    return send_resp_json(root, req);
  }

  if (get_query_param(req, "info", value, sizeof(value))) {
    memory_info memory = getstatus();
    double temperature = round(memory.temperature * 100.0) / 100.0;
    cJSON_AddNumberToObject(root, "cpu_temp", temperature);

    cJSON *_memory = cJSON_CreateObject();
    if(!_memory) {
      cJSON_Delete(root);
      return send_resp_json(NULL, req);
    }
    
    cJSON_AddNumberToObject(_memory, "iram_total", memory.total);
    cJSON_AddNumberToObject(_memory, "iram_free", memory.free);
    cJSON_AddNumberToObject(_memory, "iram_min_free", memory.min_free);
    cJSON_AddNumberToObject(_memory, "psram_total", memory.psram_total);
    cJSON_AddNumberToObject(_memory, "psram_free", memory.psram_free);
    cJSON_AddNumberToObject(_memory, "psram_min_free", memory.psram_min_free);
    cJSON_AddItemToObject(root, "memory_info", _memory);

    std::string time_str = contorller->t_time.get_str_current_time();
    cJSON_AddStringToObject(root, "time", time_str.c_str());

    cJSON_AddStringToObject(root, "version", firmware_version());

    return send_resp_json(root, req);
  }
  
  if (get_query_param(req, "state", value, sizeof(value))) {
    cJSON_AddStringToObject(root, "current_owner", contorller->get_owner_name().c_str());
    for (const auto &item : contorller->automations) {
      Automation *obj = item.get();
      obj->get_state(root);
    }

    return send_resp_json(root, req);
  }

  if (get_query_param(req, "restart", value, sizeof(value))) {
    send_json_status(req, "status", true, nullptr, nullptr);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }

  cJSON_Delete(root);
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Query param is missing");
  return ESP_FAIL;
}

static esp_err_t api_led_info_handler(httpd_req_t *req) {
  device *controller = (device*)req->user_ctx;
  led *wLed = controller->get_led();
  cJSON *root = cJSON_CreateObject();
  if(root) {
    cJSON *settings = cJSON_CreateObject();
    if(settings) {
      cJSON_AddNumberToObject(settings, "led_count", wLed->get_count_led());
      cJSON_AddBoolToObject(settings, "save_mode", wLed->get_save_mode());
      cJSON_AddBoolToObject(settings, "manual", controller->is_manual());
      cJSON_AddStringToObject(settings, "led_model", wLed->get_model() == LED_MODEL_WS2815 ? "ws2815" : "ws2812");
      cJSON_AddStringToObject(settings, "pixel_format", wLed->get_pixel_format() == LED_PIXEL_FORMAT_RGB ? "rgb" : "grb");
      cJSON_AddItemToObject(root, "settings", settings);
    } else {
      cJSON_Delete(root);
      return send_resp_json(NULL, req);
    }

    cJSON *state = cJSON_CreateObject();
    if(state) {
      cJSON_AddNumberToObject(state, "speed", wLed->get_speed());
      cJSON_AddNumberToObject(state, "mode", wLed->get_mode());
      cJSON_AddNumberToObject(state, "brightness", wLed->get_brightness());

      auto [r, g, b] = wLed->get_color();
      cJSON *color = cJSON_CreateObject();
      if(color) {
        cJSON_AddNumberToObject(color, "r", r);
        cJSON_AddNumberToObject(color, "g", g);
        cJSON_AddNumberToObject(color, "b", b);
        cJSON_AddItemToObject(state, "color", color);
      } else {
        cJSON_Delete(root);
        return send_resp_json(NULL, req);
      }

      cJSON_AddItemToObject(root, "state", state);
    }
    cJSON *modes = cJSON_CreateArray();
    if(modes) {
      size_t effects_count = 0;
      const LedEffect *effects = wLed->effects(&effects_count);

      std::vector<LedEffect> sorted_effects(effects, effects + effects_count);
      std::sort(sorted_effects.begin(), sorted_effects.end(), [](const LedEffect &a, const LedEffect &b) { return a.id < b.id; });

      for (const auto &fx : sorted_effects) {
        cJSON *m = cJSON_CreateObject();
        if (m) {
          cJSON_AddNumberToObject(m, "id_mode", fx.id);
          cJSON_AddStringToObject(m, "name", fx.name.c_str());
          cJSON_AddItemToArray(modes, m);
        }
      }
      cJSON_AddItemToObject(root, "modes", modes);
    } else {
      cJSON_Delete(root);
      return send_resp_json(NULL, req);
    }
  }
  return send_resp_json(root, req);
}

static esp_err_t api_led_control_handler(httpd_req_t *req) {
  device *controller = (device*)req->user_ctx;
  led *wLed = controller->get_led();

  char *buf = nullptr;
  if(getjson(req, &buf) == ESP_OK) {

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!root) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
      return ESP_FAIL;
    }

    // settings.manual читаем до остальных изменений, чтобы в одном запросе
    // можно было включить ручное управление и сразу поменять ленту
    cJSON *manual = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "settings"), "manual");
    if(cJSON_IsBool(manual)) controller->set_manual(cJSON_IsTrue(manual));

    // запрос меняет ленту, если пришёл state или leds_arr
    bool changes_led = cJSON_IsObject(cJSON_GetObjectItem(root, "state")) ||
                       cJSON_IsArray(cJSON_GetObjectItem(root, "leds_arr"));

    // если есть автоматика, менять ленту с панели можно только в ручном режиме
    bool blocked = changes_led && !controller->is_manual() && controller->has_automations();

    cJSON *settings = cJSON_GetObjectItem(root, "settings");
    if(settings) {
      cJSON *led_count = cJSON_GetObjectItem(settings, "led_count");
      if(cJSON_IsNumber(led_count)) wLed->update_led_count(led_count->valueint);

      cJSON *save_mode = cJSON_GetObjectItem(settings, "save_mode");
      if(cJSON_IsBool(save_mode)) wLed->set_save_mode(save_mode->valueint);

      // тип ленты и порядок цветов: можно передать одно из двух, второе останется как есть
      cJSON *j_model = cJSON_GetObjectItem(settings, "led_model");
      cJSON *j_format = cJSON_GetObjectItem(settings, "pixel_format");
      if (cJSON_IsString(j_model) || cJSON_IsString(j_format)) {
        led_model_t new_model = wLed->get_model();
        led_pixel_format_t new_format = wLed->get_pixel_format();
        bool ok = true;
        if (cJSON_IsString(j_model)) {
          if (!strcmp(j_model->valuestring, "ws2812")) new_model = LED_MODEL_WS2812;
          else if (!strcmp(j_model->valuestring, "ws2815")) new_model = LED_MODEL_WS2815;
          else ok = false;
        }
        if (cJSON_IsString(j_format)) {
          if (!strcmp(j_format->valuestring, "grb")) new_format = LED_PIXEL_FORMAT_GRB;
          else if (!strcmp(j_format->valuestring, "rgb")) new_format = LED_PIXEL_FORMAT_RGB;
          else ok = false;
        }
        if (!ok || !wLed->set_strip_type(new_model, new_format)) {
          ESP_LOGW(TAG, "led: неизвестный led_model/pixel_format — игнор");
        }
      }
    }

    cJSON *state = cJSON_GetObjectItem(root, "state");
    if(state && !blocked) {
      // начинаем с текущих значений — то, чего нет в запросе, не меняется
      uint8_t new_mode = wLed->get_mode();
      uint8_t new_speed = wLed->get_speed();
      uint8_t new_brightness = wLed->get_brightness();
      auto [new_r, new_g, new_b] = wLed->get_color();

      cJSON *mode = cJSON_GetObjectItem(state, "mode");
      if(cJSON_IsNumber(mode) && wLed->is_valid_mode((uint8_t)mode->valueint)) {
        new_mode = mode->valueint;
      }

      cJSON *speed = cJSON_GetObjectItem(state, "speed");
      if(cJSON_IsNumber(speed)) {
        new_speed = speed->valueint;
      }
      cJSON *brightness = cJSON_GetObjectItem(state, "brightness");
      if(cJSON_IsNumber(brightness)) {
        new_brightness = brightness->valueint;
      }
      cJSON *color = cJSON_GetObjectItem(state, "color");
      if(cJSON_IsObject(color)) {
        cJSON *r = cJSON_GetObjectItem(color, "r");
        cJSON *g = cJSON_GetObjectItem(color, "g");
        cJSON *b = cJSON_GetObjectItem(color, "b");
        if(cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
          new_r = r->valueint;
          new_g = g->valueint;
          new_b = b->valueint;
        }
      }

      // один вызов: без промежуточных кадров, один лог, одна запись в NVS
      wLed->set_state(new_r, new_g, new_b, new_mode, new_speed, new_brightness);
    }

    cJSON *leds_arr = cJSON_GetObjectItem(root, "leds_arr");
    if(cJSON_IsArray(leds_arr) && !blocked) {
      if(wLed->get_mode() != 253) wLed->set_mode(253);
      cJSON *item = NULL;
      cJSON_ArrayForEach(item, leds_arr) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        if(!cJSON_IsNumber(id)) continue;

        cJSON *item_color = cJSON_GetObjectItem(item, "color");
        if(!cJSON_IsObject(item_color)) continue;

        cJSON *r = cJSON_GetObjectItem(item_color, "r");
        cJSON *g = cJSON_GetObjectItem(item_color, "g");
        cJSON *b = cJSON_GetObjectItem(item_color, "b");
        if(!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b)) continue;

        ESP_LOGI(TAG, "led id=%d color=(%d,%d,%d)", id->valueint, r->valueint, g->valueint, b->valueint);
        wLed->set_pixel(id->valueint, r->valueint, g->valueint, b->valueint);
      }
      wLed->refresh();
    }

    cJSON_Delete(root);

    if(blocked) {
      ESP_LOGI(TAG, "led: запрос отклонён, работает автоматика, ручное управление выключено");
      httpd_resp_set_status(req, "409 Conflict");
      return send_json_status(req, "status", false, "error", "работает автоматика. Включите ручное управление");
    }
    return send_json_status(req, "status", true, nullptr, nullptr);
  }

  return send_json_status(req, "status", false, "error", "fail parse json");
}

static esp_err_t f_file_get(httpd_req_t *req) {
  ESP_LOGI(TAG, "Запрос пришел get");
  device *controller = (device*)req->user_ctx;
  FileSystem *fs = controller->get_fs();

  char value[256];

  // ?name=test
  if (get_query_param(req, "name", value, sizeof(value))) {
    std::string text = fs->getfile(value);
    if (text.empty()) {
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
      return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(text.c_str());
    if (!root) {
      ESP_LOGE(TAG, "Файл %s содержит невалидный JSON", value);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid JSON in file");
      return ESP_FAIL;
    }

    return send_resp_json(root, req);
  }

  // ?info=
  if (get_query_param(req, "info", value, sizeof(value))) {
    std::vector<fileinfo_t> list = fs->getfilelist();

    cJSON *root = cJSON_CreateObject();
    if (!root) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
      return ESP_FAIL;
    }

    // крепим к root сразу: любой cJSON_Delete(root) ниже освободит и массив
    cJSON *file_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "files", file_array);
    uint8_t count = 0;
    for (auto item : list) {
      cJSON *file_obj = cJSON_CreateObject();
      cJSON_AddStringToObject(file_obj, "name", item.name.c_str());
      cJSON_AddNumberToObject(file_obj, "size", item.size);
      cJSON_AddItemToArray(file_array, file_obj);
      count++;
    }

    cJSON *info_fs = cJSON_CreateObject();
    if(!info_fs) {
      cJSON_Delete(root);
      return send_resp_json(NULL, req);
    }
  
    cJSON_AddNumberToObject(info_fs, "file_total", count);
    auto [total, used] = fs->get_info();
    cJSON_AddNumberToObject(info_fs, "size_total", total);
    cJSON_AddNumberToObject(info_fs, "size_used", used);
    cJSON_AddItemToObject(root, "info", info_fs);
    return send_resp_json(root, req);
  }

  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Query param is missing");
  return ESP_FAIL;
}

static esp_err_t f_file_create(httpd_req_t *req) {
  ESP_LOGI(TAG, "Запрос пришел create file");
  device *controller = (device*)req->user_ctx;
  FileSystem *fs = controller->get_fs();

  char *buf = nullptr;
  if(getjson(req, &buf) == ESP_OK) {
    cJSON *root = cJSON_Parse(buf);
    if(!root) {
      free(buf);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
      return ESP_FAIL;
    }

    cJSON *name = cJSON_GetObjectItem(root, "name");
    cJSON *text = cJSON_GetObjectItem(root, "text");
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *continuous = cJSON_GetObjectItem(root, "continuous");

    const char *bad = nullptr;
    if (!cJSON_IsString(name) || !name->valuestring[0]) bad = "name: non-empty string required";
    else if (strchr(name->valuestring, '/')) bad = "name: '/' is not allowed";
    else if (!cJSON_IsString(text) || !text->valuestring[0]) bad = "text: non-empty string required";
    else if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valueint > 255) bad = "id: number 0..255 required";
    else if (!cJSON_IsBool(continuous)) bad = "continuous: bool required";
    if (bad) {
      ESP_LOGW(TAG, "create file: %s", bad);
      free(buf);
      cJSON_Delete(root);
      return send_json_status(req, "status", false, "error", bad);
    }

    // сначала компилируем — на флеш попадает только рабочий скрипт. Иначе битый файл
    // грузился бы при каждом старте (а до лимита инструкций мог и повесить загрузку)
    char error[160] = {0};
    bool registered = false;
    bool ok = script_engine_compile_and_register(name->valuestring, id->valueint, cJSON_IsTrue(continuous),
                                                 text->valuestring, -1, error, sizeof(error), &registered);
    if (ok && !fs->createfile(name->valuestring, buf)) {
      ok = false;
      snprintf(error, sizeof(error), "file write failed");
      ESP_LOGE(TAG, "create file '%s': %s", name->valuestring, error);
    }
    free(buf);
    cJSON_Delete(root);

    if (!ok) return send_json_status(req, "status", false, "error", error);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "status", true);
    cJSON_AddBoolToObject(resp, "registered", registered);
    // пересохранили скрипт, чей id уже занят его старой версией: проверен, но заменится после ребута
    if (!registered) cJSON_AddStringToObject(resp, "warning", "saved, the effect will update after reboot");
    return send_resp_json(resp, req);
  }
  return send_json_status(req, "status", false, "error", "fail parse json");
}

static esp_err_t f_file_delete(httpd_req_t *req) {
  ESP_LOGI(TAG, "Запрос пришел delete file");
  device *controller = (device*)req->user_ctx;
  FileSystem *fs = controller->get_fs();

  char value[256];

  // ?name=test
  if (get_query_param(req, "name", value, sizeof(value))) {
    if(fs->exsistfile(value)) {
      fs->deletefile(value);
      return send_json_status(req, "status", true, nullptr, nullptr);
    } else {
      return send_json_status(req, "status", false, "error", "file not found");
    }
  }
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Query param is missing");
  return ESP_FAIL;
}

void device::register_handlers(httpd_handle_t httpd)
{
  httpd_uri_t device_handler_post = {
    .uri = "/api/device",
    .method = HTTP_POST,
    .handler = api_device_handler_post,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &device_handler_post);

  httpd_uri_t device_handler_get = {
    .uri = "/api/device",
    .method = HTTP_GET,
    .handler = api_device_handler_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &device_handler_get);

  httpd_uri_t led_info_handler = {
    .uri = "/api/led",
    .method = HTTP_GET,
    .handler = api_led_info_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &led_info_handler);

  httpd_uri_t led_control_handler = {
    .uri = "/api/led",
    .method = HTTP_POST,
    .handler = api_led_control_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &led_control_handler);

  httpd_uri_t file_get = {
    .uri = "/api/file",
    .method = HTTP_GET,
    .handler = f_file_get,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &file_get);

  httpd_uri_t file_post = {
    .uri = "/api/file",
    .method = HTTP_POST,
    .handler = f_file_create,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &file_post);

  httpd_uri_t file_delete = {
    .uri = "/api/file",
    .method = HTTP_DELETE,
    .handler = f_file_delete,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &file_delete);
}

bool device::is_time_synced() {
  // пока SNTP не отработал, часы показывают 1970 год
  if (!time_synced) {
    tm now = t_time.get_current_time();
    time_synced = now.tm_year >= (2024 - 1900);
  }
  return time_synced;
}

bool device::engage(Automation *who) {
  xSemaphoreTake(owner_mutex, portMAX_DELAY);
  Automation *prev = current_owner;
  bool ok = !manual_active                                  // ручное управление — отказ всем
         && (prev == nullptr                                // никого нет
         || prev == who                                     // это я же, повторно
         || who->get_priority() > prev->get_priority());    // я главнее

  if (ok && prev != who) {
    // снимок только если у источника его ещё нет. Если источник перебили, а потом
    // вернули ему ленту, на ленте уже ЕГО состояние — старый снимок сохраняем
    if (!who->has_saved) {
      auto [r, g, b] = wled->get_color();
      who->saved = { wled->get_mode(), wled->get_speed(), wled->get_brightness(), r, g, b };
      who->has_saved = true;
    }
    current_owner = who;
  }
  xSemaphoreGive(owner_mutex);

  if (!ok) {
    ESP_LOGI(TAG, "engage: %s — отказ, лентой владеет %s", who->get_name().c_str(), prev ? prev->get_name().c_str() : "ручное управление");
  } else if (prev != who) {
    ESP_LOGI(TAG, "engage: %s захватил ленту (был %s)", who->get_name().c_str(), prev ? prev->get_name().c_str() : "никто");
  }
  return ok;
}

void device::release(Automation *who) {
  xSemaphoreTake(owner_mutex, portMAX_DELAY);
  bool was_owner = current_owner == who;
  bool restored = false;
  bool handed = false;
  if (was_owner) {
    current_owner = nullptr;
    if (who->has_saved) {
      const led_state &s = who->saved;
      wled->set_state(s.r, s.g, s.b, s.mode, s.speed, s.brightness);
      who->has_saved = false;
      restored = true;
    }
  } else if (who->has_saved) {
    if (current_owner) {
      // источник сдаётся, пока лентой владеет другой. Владелец хранит снимок
      // состояния этого источника — отдаём владельцу то, что было до него
      if (current_owner->has_saved) current_owner->saved = who->saved;
      handed = true;
    } else {
      // владельца нет: верхний источник уже вернул ленту в состояние этого
      // источника — восстанавливаем то, что было до него
      const led_state &s = who->saved;
      wled->set_state(s.r, s.g, s.b, s.mode, s.speed, s.brightness);
      restored = true;
    }
    who->has_saved = false;
  }
  xSemaphoreGive(owner_mutex);

  if (restored) {
    ESP_LOGI(TAG, "release: %s отпустил ленту, состояние восстановлено", who->get_name().c_str());
  } else if (handed) {
    ESP_LOGI(TAG, "release: %s — не владелец, снимок передан владельцу", who->get_name().c_str());
  } else {
    ESP_LOGI(TAG, "release: %s — не владелец, игнор", who->get_name().c_str());
  }
}

std::string device::get_owner_name() {
  xSemaphoreTake(owner_mutex, portMAX_DELAY);
  std::string owner_name = current_owner ? current_owner->get_name() : "";
  xSemaphoreGive(owner_mutex);
  return owner_name;
}

void device::set_manual(bool on) {
  xSemaphoreTake(owner_mutex, portMAX_DELAY);
  bool was_active = manual_active;
  manual_active = on;

  // при включении снимаем владельца и выбрасываем снимки у ВСЕХ источников
  // (и у перебитых тоже): при своём release никто не должен восстанавливать
  // старое поверх ручного
  Automation *prev = nullptr;
  if (on) {
    prev = current_owner;
    current_owner = nullptr;
    for (auto &a : automations) a->has_saved = false;
  }
  xSemaphoreGive(owner_mutex);

  if (on && !was_active) {
    ESP_LOGI(TAG, "manual: ручное управление ВКЛ, автоматика заблокирована (был %s)", prev ? prev->get_name().c_str() : "никто");
  } else if (!on && was_active) {
    // ничего не восстанавливаем — ручное состояние остаётся, пока автоматике не понадобится лента
    ESP_LOGI(TAG, "manual: ручное управление ВЫКЛ, автоматика снова работает");
  }
}