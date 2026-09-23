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
#include "cJSON.h"
#include "helper_server.h"
#include "script_engine.h"
#include "sntp_time.h"


#ifdef CONFIG_SERVER_HTTPS
#include "esp_https_server.h"
#include "private.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#endif

static const char *TAG = "Server";
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

server::server(class wifimanager *_wifi, class device *_device) {
  if(_wifi) wifi = _wifi;
  if(_device) controller = _device;
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
  ssl_config.httpd.lru_purge_enable = true;
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
  config.stack_size = 10 * 1024;
  config.max_uri_handlers = 24;
  config.lru_purge_enable = true;
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

  m_ota.register_handlers(m_server);
  wifi->register_handlers(m_server);
  controller->register_handlers(m_server);

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
