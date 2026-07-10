#include "https_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_event.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h> 
#include <sys/time.h>
#include "private.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "MemoryManager.h"
#include "cJSON.h"
#include "driver/temperature_sensor.h"
#include "wifimanager.h"
#include <vector>

static const char *TAG = "HTTPS_SERVER";

temperature_sensor_handle_t temp_handle = NULL;

static httpd_handle_t server = NULL;
extern wifimanager *wifi;

size_t cert_len = cert_cert_crt_end - cert_cert_crt_start;
size_t key_len = cert_private_key_end - cert_private_key_start;
size_t ca_len = cert_ca_crt_end - cert_ca_crt_start;

static void check_certificate(void) {
  int ret;
  mbedtls_x509_crt cert_ctx;
  mbedtls_x509_crt_init(&cert_ctx);

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

static esp_err_t api_data_handler(httpd_req_t *req) {
  memory_info memory = getstatus();
  float temp_cpu;
  ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &temp_cpu));

  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "cpu_temp", temp_cpu);

  cJSON *_memory = cJSON_CreateObject();
  cJSON_AddNumberToObject(_memory, "iram_total", memory.total);
  cJSON_AddNumberToObject(_memory, "iram_free", memory.free);
  cJSON_AddNumberToObject(_memory, "iram_min_free", memory.min_free);
  cJSON_AddItemToObject(root, "memory_info", _memory);

  cJSON *wifi_array = cJSON_CreateArray();
    
  std::vector<wifi_ap_record_t> wifi_networks = wifi->scan_wifi_networks();

  for (size_t i = 0; i < wifi_networks.size(); i++) {
    wifi_ap_record_t ap = wifi_networks[i];

    cJSON *ap_obj = cJSON_CreateObject();

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

  char *json = cJSON_Print(root);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);

  free(json);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "Ответ отправлен, найдено %d Wi-Fi сетей", wifi_networks.size());
  return ESP_OK;
}


esp_err_t https_server_start(uint16_t port) {

  temperature_sensor_config_t temp_sensor_config = {
    .range_min = 20,
    .range_max = 80,
    .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT,
  };

  ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
  ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
  if (server != NULL) {
    ESP_LOGW(TAG, "Server already running");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Starting HTTPS server on port %d", port);

  check_certificate();

  httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
  ssl_config.port_secure = port;

  ssl_config.servercert = (const uint8_t *)cert_cert_crt_start;
  ssl_config.servercert_len = cert_len;

  ssl_config.prvtkey_pem = (const uint8_t *)cert_private_key_start;
  ssl_config.prvtkey_len = key_len;

  ssl_config.cacert_pem = (const uint8_t *)cert_ca_crt_start;
  ssl_config.cacert_len = ca_len;

  ssl_config.httpd.recv_wait_timeout = 35;
  ssl_config.httpd.send_wait_timeout = 35;

  ssl_config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  ssl_config.tls_handshake_timeout_ms = 5000;
  ssl_config.session_tickets = true;
  ssl_config.httpd.task_priority = 10;
  ssl_config.httpd.max_open_sockets = 2;
  esp_err_t err = httpd_ssl_start(&server, &ssl_config);
  

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
    return err;
  }
  
  httpd_uri_t uri_handler = {
    .uri = "/api/data",
    .method = HTTP_GET,
    .handler = api_data_handler,
    .user_ctx = NULL
  };
  httpd_register_uri_handler(server, &uri_handler);
  
  ESP_LOGI(TAG, "HTTPS server started successfully on port %d", port);
  return ESP_OK;
}

// Остановка сервера
void https_server_stop(void) {
  if (server != NULL) {
    httpd_ssl_stop(server);
    server = NULL;
    ESP_LOGI(TAG, "HTTPS server stopped");
  }
}