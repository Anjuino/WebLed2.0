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

static const char *TAG = "HTTPS_SERVER";

static httpd_handle_t server = NULL;

size_t cert_len = cert_cert_crt_end - cert_cert_crt_start;
size_t key_len = cert_private_key_end - cert_private_key_start;
size_t ca_len = cert_ca_crt_end - cert_ca_crt_start;

void check_my_certificate(void) {
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


typedef struct {
  const char *uri;
  int method;
  esp_err_t (*handler)(httpd_req_t *req);
} uri_handler_t;

#define MAX_HANDLERS 20
static uri_handler_t handlers[MAX_HANDLERS] = {};
static int handler_count = 0;


static esp_err_t api_data_handler(httpd_req_t *req) {
  const char* response = "{\"status\":\"ok\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, response, strlen(response));
  ESP_LOGI(TAG, "Ответ");
  return ESP_OK;
}


esp_err_t https_server_start(uint16_t port) {
  if (server != NULL) {
    ESP_LOGW(TAG, "Server already running");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Starting HTTPS server on port %d", port);

  check_my_certificate();

  httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
  ssl_config.port_secure = port;

  ssl_config.servercert = (const uint8_t *)cert_cert_crt_start;
  ssl_config.servercert_len = cert_len;

  ssl_config.prvtkey_pem = (const uint8_t *)cert_private_key_start;
  ssl_config.prvtkey_len = key_len;

  ssl_config.cacert_pem = (const uint8_t *)cert_ca_crt_start;
  ssl_config.cacert_len = ca_len;

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
  
  ESP_LOGI(TAG, "✅ HTTPS server started successfully on port %d", port);
  return ESP_OK;
}

// Остановка сервера
void https_server_stop(void) {
  if (server != NULL) {
    httpd_ssl_stop(server);
    server = NULL;
    ESP_LOGI(TAG, "HTTPS server stopped");
  }
  
  for (int i = 0; i < handler_count; i++) {
    free((void*)handlers[i].uri);
  }
  handler_count = 0;
}