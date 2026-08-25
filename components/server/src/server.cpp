#include "server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <vector>
#include <math.h>
#include <tuple>
#include "DeviceManager.h"
#include "cJSON.h"
#include "wifimanager.h"
#include "esp_ota_ops.h"

#ifdef CONFIG_SERVER_HTTPS
#include "esp_https_server.h"
#include "private.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#endif

static const char *TAG = "Server";

static esp_err_t send_resp_json(cJSON *resp, httpd_req_t *req) {
  if(!resp) {
    ESP_LOGE(TAG, "Нет json объекта");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Нет json объекта");
    return ESP_FAIL;
  } 

  char *json = cJSON_Print(resp);
  if(!json) {
    ESP_LOGE(TAG, "Не удалось создать json строку");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Не удалось создать json строку");
    cJSON_Delete(resp);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  esp_err_t err = httpd_resp_sendstr(req, json);

  if(err != ESP_OK) {
    ESP_LOGE(TAG, "Ошибка отправки %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error");
  }

  free(json);
  cJSON_Delete(resp);
  return err;
}

static esp_err_t api_led_info_handler(httpd_req_t *req) {
  led *wLed = (led*)req->user_ctx;
  cJSON *root = cJSON_CreateObject();
  if(root) {
    cJSON_AddNumberToObject(root, "speed", wLed->get_speed());
    cJSON_AddNumberToObject(root, "mode", wLed->get_mode());
    cJSON_AddNumberToObject(root, "brightness", wLed->get_brightness());

    auto [r, g, b] = wLed->get_color();
    cJSON *color = cJSON_CreateObject();
    if(color) {
      cJSON_AddNumberToObject(color, "r", r);
      cJSON_AddNumberToObject(color, "g", g);
      cJSON_AddNumberToObject(color, "b", b);
      cJSON_AddItemToObject(root, "color", color);
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

  char buf[256] = {0};
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if(ret <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(buf);
  if(!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *speed = cJSON_GetObjectItem(root, "speed");
  if(cJSON_IsNumber(speed)) wLed->set_speed(speed->valueint);

  cJSON *mode = cJSON_GetObjectItem(root, "mode");
  if(cJSON_IsNumber(mode)) wLed->set_mode(mode->valueint);

  cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
  if(cJSON_IsNumber(brightness)) wLed->set_brightness(brightness->valueint);

  cJSON *color = cJSON_GetObjectItem(root, "color");
  if(cJSON_IsObject(color)) {
    cJSON *r = cJSON_GetObjectItem(color, "r");
    cJSON *g = cJSON_GetObjectItem(color, "g");
    cJSON *b = cJSON_GetObjectItem(color, "b");
    if(cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
      wLed->set_color(r->valueint, g->valueint, b->valueint);
    }
  }

  cJSON_Delete(root);
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", true);
  return send_resp_json(resp, req);
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
  cJSON_AddItemToObject(root, "memory_info", _memory);

  cJSON *wifi_array = cJSON_CreateArray();
  if(!wifi_array) {
    cJSON_Delete(root);
    return send_resp_json(NULL, req);
  }

  extern wifimanager *wifi;
  std::vector<wifi_ap_record_t> wifi_networks = wifi->scan_wifi_networks();

  for (size_t i = 0; i < wifi_networks.size(); i++) {
    wifi_ap_record_t ap = wifi_networks[i];
    cJSON *ap_obj = cJSON_CreateObject();
    if(!ap_obj) {
      cJSON_Delete(wifi_array);
      cJSON_Delete(root);
      return send_resp_json(NULL, req);
    }

    cJSON_AddStringToObject(ap_obj, "ssid", (const char*)ap.ssid);
    cJSON_AddNumberToObject(ap_obj, "rssi", ap.rssi);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
            ap.bssid[0], ap.bssid[1], ap.bssid[2],
            ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    cJSON_AddStringToObject(ap_obj, "bssid", mac_str);

    cJSON_AddItemToArray(wifi_array, ap_obj);
  }

  cJSON_AddItemToObject(root, "wifi_networks", wifi_array);
  return send_resp_json(root, req);
}

static esp_err_t ota_upload_handler(httpd_req_t *req) {
  server *srv = (server*)req->user_ctx;
  if (!srv) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server context null");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "OTA upload via HTTP started");
  
  char content_len_str[16];
  size_t content_len = 0;
  if (httpd_req_get_hdr_value_str(req, "Content-Length", content_len_str, sizeof(content_len_str)) == ESP_OK) {
    content_len = atoi(content_len_str);
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length header required");
    return ESP_FAIL;
  }

  const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
  if (!ota_partition) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    return ESP_FAIL;
  }

  if (content_len > ota_partition->size) {
    httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Firmware too large");
    return ESP_FAIL;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    return ESP_FAIL;
  }

  char buf[4096];
  size_t remaining = content_len;
  size_t progress = 0;

  while (remaining > 0) {
    int to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
    int recv_len = httpd_req_recv(req, buf, to_read);

    if (recv_len <= 0) {
      if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
      ESP_LOGE(TAG, "Receive error");
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
      return ESP_FAIL;
    }
    
    err = esp_ota_write(ota_handle, buf, recv_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Flash write failed: %s", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
      return ESP_FAIL;
    }

    remaining -= recv_len;
    progress += recv_len;
    
    if ((progress * 100 / content_len) % 10 == 0) {
      ESP_LOGI(TAG, "OTA progress: %d%%", (int)(progress * 100 / content_len));
    }
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
      ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
      return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(ota_partition);
  if (err != ESP_OK) {
      ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
      return ESP_FAIL;
  }
  
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", true);
  cJSON_AddStringToObject(resp, "message", "OTA update successful! Rebooting...");

  ESP_LOGI(TAG, "OTA update successful, restarting in 2 seconds...");
  esp_err_t ret = send_resp_json(resp, req);
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  esp_restart();

  return ret;
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

server::server(class led *_led) {
  if(_led) wLed = _led;
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

  ssl_config.httpd.recv_wait_timeout = 15;
  ssl_config.httpd.send_wait_timeout = 15;
  ssl_config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  ssl_config.tls_handshake_timeout_ms = 5000;
  ssl_config.session_tickets = true;
  ssl_config.httpd.task_priority = 10;
  ssl_config.httpd.max_open_sockets = 2;
  
  esp_err_t err = httpd_ssl_start(&m_server, &ssl_config);
#else
  uint16_t port = 80;
  ESP_LOGI(TAG, "Starting HTTP server on port %d", port);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  config.task_priority = 10;
  config.max_open_sockets = 6;
  config.stack_size = 15 * 1024;

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
    .uri = "/api/ledinfo",
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

  httpd_uri_t ota_upload = {
    .uri = "/api/ota/upload",
    .method = HTTP_POST,
    .handler = ota_upload_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(m_server, &ota_upload);

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
