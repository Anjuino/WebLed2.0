#include "server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>
#include <math.h>
#include <tuple>
#include "DeviceManager.h"
#include "cJSON.h"
#include "helper_server.h"
#include "script_engine.h"

#ifdef CONFIG_SERVER_HTTPS
#include "esp_https_server.h"
#include "private.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#endif

static const char *TAG = "Server";

static esp_err_t api_led_info_handler(httpd_req_t *req) {
  led *wLed = (led*)req->user_ctx;
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
  ESP_LOGI(TAG, "Запрос пришел");
  led *wLed = (led*)req->user_ctx;

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

static esp_err_t api_device_info_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  if(!root) return send_resp_json(NULL, req);

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

  return send_resp_json(root, req);
}

static esp_err_t f_file_get(httpd_req_t *req) {
  ESP_LOGI(TAG, "Запрос пришел get");
  FileSystem *fs = (FileSystem*)req->user_ctx;

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
  FileSystem *fs = (FileSystem*)req->user_ctx;

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
    return send_json_status(req, "status", true, nullptr, nullptr);
  }
  return send_json_status(req, "status", false, "error", "fail parse json");
}

static esp_err_t f_file_delete(httpd_req_t *req) {
  ESP_LOGI(TAG, "Запрос пришел delete file");
  FileSystem *fs = (FileSystem*)req->user_ctx;

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

#ifdef CONFIG_SERVER_HTTPS
void server::checkCertificate() {
  int ret;
  mbedtls_x509_crt cert_ctx;
  mbedtls_x509_crt_init(&cert_ctx);

  size_t cert_len = cert_cert_crt_end - cert_cert_crt_start;
  ESP_LOGI(TAG, "Trying to parse certificate...");
  ESP_LOGI(TAG, "Cert length: %d", cert_len);

  ret = mbedtls_x509_crt_parse(&cert_ctx, (const unsigned char *)cert_cert_crt_start, cert_len); 

  if (ret != 0) {
    char error_buf[100];
    mbedtls_strerror(ret, error_buf, sizeof(error_buf));
    ESP_LOGE(TAG, "Certificate parsing FAILED!");
    ESP_LOGE(TAG, "Error: %s", error_buf);
  } else {
    ESP_LOGI(TAG, "Certificate parsed successfully!");
    char buf[1024];
    mbedtls_x509_crt_info(buf, sizeof(buf), "  ", &cert_ctx);
    ESP_LOGI(TAG, "Certificate info:\n%s", buf);
  }

  mbedtls_x509_crt_free(&cert_ctx);
}
#endif

server::server(class led *_led, class FileSystem *_fs, class wifimanager *_wifi) {
  if(_led) wLed = _led;
  if(_fs) fs = _fs;
  if(_wifi) wifi = _wifi;
}

server::~server() {
  stop();
}

esp_err_t server::start() {
  if (m_server != nullptr) {
    ESP_LOGW(TAG, "Server already running");
    return ESP_OK;
  }

#ifdef CONFIG_SERVER_HTTPS
  uint16_t port = 443;
  ESP_LOGI(TAG, "Starting HTTPS server on port %d", port);

  checkCertificate();

  httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
  ssl_config.port_secure = port;

  size_t cert_len = cert_cert_crt_end - cert_cert_crt_start;
  size_t key_len = cert_private_key_end - cert_private_key_start;
  size_t ca_len = cert_ca_crt_end - cert_ca_crt_start;

  ssl_config.servercert = (const uint8_t *)cert_cert_crt_start;
  ssl_config.servercert_len = cert_len;
  ssl_config.prvtkey_pem = (const uint8_t *)cert_private_key_start;
  ssl_config.prvtkey_len = key_len;
  ssl_config.cacert_pem = (const uint8_t *)cert_ca_crt_start;
  ssl_config.cacert_len = ca_len;

  ssl_config.httpd.recv_wait_timeout = 10;
  ssl_config.httpd.send_wait_timeout = 10;
  ssl_config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  ssl_config.tls_handshake_timeout_ms = 5000;
  ssl_config.session_tickets = true;
  ssl_config.httpd.task_priority = 10;
  ssl_config.httpd.max_open_sockets = 6;
  ssl_config.httpd.stack_size = 15 * 1024;
  ssl_config.httpd.max_uri_handlers = 16; 
  esp_err_t err = httpd_ssl_start(&m_server, &ssl_config);
#else
  uint16_t port = 80;
  ESP_LOGI(TAG, "Starting HTTP server on port %d", port);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  config.task_priority = 10;
  config.max_open_sockets = 7;
  config.stack_size = 13 * 1024;
  config.max_uri_handlers = 16; 
  config.keep_alive_enable = true;
  config.keep_alive_idle = 3;
  config.keep_alive_interval = 3;   
  config.keep_alive_count = 2;

  esp_err_t err = httpd_start(&m_server, &config);
#endif

  if (err != ESP_OK) {
    #ifdef CONFIG_SERVER_HTTPS
      ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
    #else
      ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    #endif
      return err;
  }

  httpd_uri_t led_info_handler = {
    .uri = "/api/led",
    .method = HTTP_GET,
    .handler = api_led_info_handler,
    .user_ctx = wLed
  };
  httpd_register_uri_handler(m_server, &led_info_handler);

  httpd_uri_t led_control_handler = {
    .uri = "/api/led",
    .method = HTTP_POST,
    .handler = api_led_control_handler,
    .user_ctx = wLed
  };
  httpd_register_uri_handler(m_server, &led_control_handler);

  httpd_uri_t device_info_handler = {
    .uri = "/api/deviceinfo",
    .method = HTTP_GET,
    .handler = api_device_info_handler,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(m_server, &device_info_handler);

  httpd_uri_t file_get = {
    .uri = "/api/file",
    .method = HTTP_GET,
    .handler = f_file_get,
    .user_ctx = fs
  };
  httpd_register_uri_handler(m_server, &file_get);

  httpd_uri_t file_post = {
    .uri = "/api/file",
    .method = HTTP_POST,
    .handler = f_file_create,
    .user_ctx = fs
  };
  httpd_register_uri_handler(m_server, &file_post);

  httpd_uri_t file_delete = {
    .uri = "/api/file",
    .method = HTTP_DELETE,
    .handler = f_file_delete,
    .user_ctx = fs
  };
  httpd_register_uri_handler(m_server, &file_delete);

  m_ota.register_handlers(m_server);
  wifi->register_handlers(m_server);

  ESP_LOGI(TAG, "Server started successfully");
  return ESP_OK;
}

void server::stop() {
  if (m_server != nullptr) {
    #ifdef CONFIG_SERVER_HTTPS
      httpd_ssl_stop(m_server);
    #else
      httpd_stop(m_server);
    #endif

    m_server = nullptr;
    ESP_LOGI(TAG, "Server stopped");
  }
}
