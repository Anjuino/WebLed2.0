#include "device.h"
#include <algorithm>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "device";

static void on_press(void *user_data) {
  ESP_LOGE(TAG, "btn press");
}

static void on_light(int value, void *user_data) {
  ESP_LOGE(TAG, "light = %d", value);
}

device::device() : settings(STORAGE_DEVICE, false) 
{
  uint8_t init_flag = settings.get<uint8_t>(KEY_INIT, 0);

  if (init_flag == 0) {
    //первый запуск

  } else {
    //послед запуски
  }

  t_time.sync();
  wled = new led();
  xTaskCreatePinnedToCore(led::task_entry, "Led", 6 * 1024, wled, 10, NULL, 1);


  fs = new FileSystem();
  fs->init();
  script_engine_start(wled, fs);
}

device::~device()
{

}

led *device::get_led()
{
  if(wled) return wled;
  return NULL;
}

FileSystem *device::get_fs()
{
  if(fs) return fs;
  return NULL;
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

    cJSON *json_button = cJSON_GetObjectItem(root, "button");
    if(json_button) {
      class button *btn = new button();
      btn->init(GPIO_NUM_6, on_press);
      contorller->btn = btn;
    }

    cJSON *json_light_sensor = cJSON_GetObjectItem(root, "button");
    if(json_light_sensor) {
      class light_sensor *sensor = new light_sensor();
      contorller->l_sensor = sensor;
      sensor->init(GPIO_NUM_2, on_light, 20000);
    }

    return send_json_status(req, "status", true, nullptr, nullptr);
  }
  return send_json_status(req, "status", false, "error", "fail parse json");
}

static esp_err_t api_device_handler_get(httpd_req_t *req) {
  device *contorller = (device*)req->user_ctx;
  cJSON *root = cJSON_CreateObject();
  if(!root) return send_resp_json(NULL, req);

  char value[256];
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

    cJSON *settings = cJSON_GetObjectItem(root, "settings");
    if(settings) {
      cJSON *led_count = cJSON_GetObjectItem(settings, "led_count");
      if(cJSON_IsNumber(led_count)) wLed->update_led_count(led_count->valueint);

      cJSON *save_mode = cJSON_GetObjectItem(settings, "save_mode");
      if(cJSON_IsBool(save_mode)) wLed->set_save_mode(save_mode->valueint);
    }

    cJSON *state = cJSON_GetObjectItem(root, "state");
    if(state) {
      cJSON *mode = cJSON_GetObjectItem(state, "mode");
      if(cJSON_IsNumber(mode) && wLed->is_valid_mode((uint8_t)mode->valueint)) {
        wLed->set_mode(mode->valueint);
      }

      cJSON *speed = cJSON_GetObjectItem(state, "speed");
      if(cJSON_IsNumber(speed)) {
        wLed->set_speed(speed->valueint);
      }
      cJSON *brightness = cJSON_GetObjectItem(state, "brightness");
      if(cJSON_IsNumber(brightness)) {
        wLed->set_brightness(brightness->valueint);
      }
      cJSON *color = cJSON_GetObjectItem(state, "color");
      if(cJSON_IsObject(color)) {
        cJSON *r = cJSON_GetObjectItem(color, "r");
        cJSON *g = cJSON_GetObjectItem(color, "g");
        cJSON *b = cJSON_GetObjectItem(color, "b");
        if(cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
          wLed->set_color(r->valueint, g->valueint, b->valueint);
        }
      }
    }

    cJSON *leds_arr = cJSON_GetObjectItem(root, "leds_arr");
    if(cJSON_IsArray(leds_arr)) {
      if(wLed->get_mode() != 255) wLed->set_mode(255);
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
      wLed->show();
    }

    cJSON_Delete(root);
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

    cJSON *file_array = cJSON_CreateArray();
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
    cJSON_AddItemToObject(root, "files", file_array);
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

    if(cJSON_IsString(name) && cJSON_IsString(text) && cJSON_IsNumber(id) && cJSON_IsBool(continuous)) {
      fs->createfile(name->valuestring, buf);
      script_engine_compile_and_register(name->valuestring, id->valueint, continuous->valueint, text->valuestring);
    }
    free(buf);
    cJSON_Delete(root);
    return send_json_status(req, "status", true, nullptr, nullptr);
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