#include "ota.h"
#include "http_json.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

#define OTA_REMOTE_URL "http://192.168.0.113:5000/getfirmware"
#define OTA_ACK_URL    "http://192.168.0.113:5000/ack"

#define FIRMWARE_VERSION 1

static const char *TAG = "OTA";

static int local_ota_chunk_reader(void *ctx, char *buf, int max_len) {
  httpd_req_t *req = (httpd_req_t *)ctx;
  while (true) {
    int r = httpd_req_recv(req, buf, max_len);
    if (r > 0) return r;
    if (r == HTTPD_SOCK_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    return r;
  }
}

static int remote_ota_chunk_reader(void *ctx, char *buf, int max_len) {
  return esp_http_client_read((esp_http_client_handle_t)ctx, buf, max_len);
}

esp_err_t OtaManager::write_stream(size_t total_size, int (*reader)(void *ctx, char *buf, int max_len), void *ctx) {
  if (!reader || total_size == 0) {
    ESP_LOGE(TAG, "write_stream: bad args (reader=%p, size=%u)", (void*)reader, (unsigned)total_size);
    return ESP_ERR_INVALID_ARG;
  }

  const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
  if (!ota_partition) {
    ESP_LOGE(TAG, "write_stream: no OTA partition");
    return ESP_FAIL;
  }
  if (total_size > ota_partition->size) {
    ESP_LOGE(TAG, "write_stream: firmware too large %u > %u", (unsigned)total_size, (unsigned)ota_partition->size);
    return ESP_ERR_INVALID_SIZE;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    return err;
  }

  char buf[4096];
  size_t remaining = total_size;
  size_t progress = 0;
  size_t last_pct = (size_t)-1;

  while (remaining > 0) {
    int to_read = (remaining < sizeof(buf)) ? (int)remaining : (int)sizeof(buf);
    int recv_len = reader(ctx, buf, to_read);

    if (recv_len <= 0) {
      ESP_LOGE(TAG, "write_stream: read error/EOF (%d) at %u/%u bytes", recv_len, (unsigned)progress, (unsigned)total_size);
      esp_ota_abort(ota_handle);
      return ESP_FAIL;
    }

    err = esp_ota_write(ota_handle, buf, recv_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      return err;
    }

    remaining -= recv_len;
    progress  += recv_len;

    size_t pct = (progress * 100) / total_size;
    if (pct != last_pct && (pct % 10) == 0) {
      ESP_LOGI(TAG, "OTA progress: %u%%", (unsigned)pct);
      last_pct = pct;
    }
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_ota_set_boot_partition(ota_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "OTA write successful (%u bytes)", (unsigned)total_size);
  return ESP_OK;
}

static void ota_post_json_log(const char *url, const char *body, const char *what) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 2000;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "%s: client init failed", what);
    return;
  }
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, body, strlen(body));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGD(TAG, "%s: HTTP %d", what, esp_http_client_get_status_code(client));
  } else {
    ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
}

static void ota_notify_server(const char *chipid,
                              bool success, const char *error_msg) {
  char body[256];
  if (success) {
    snprintf(body, sizeof(body), "{\"status\":\"installed\",\"chipid\":\"%s\"}",
             chipid ? chipid : "?");
  } else {
    snprintf(body, sizeof(body),
             "{\"status\":\"failed\",\"error\":\"%s\",\"chipid\":\"%s\"}",
             error_msg ? error_msg : "unknown",
             chipid ? chipid : "?");
  }
  ota_post_json_log(OTA_ACK_URL, body, "OTA ack");
}

esp_err_t OtaManager::fetch_from_url(const char *url) {
  if (!url || !*url) {
    ESP_LOGE(TAG, "fetch_from_url: empty URL");
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  char chip_id[24];
  snprintf(chip_id, sizeof(chip_id), "%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  char fetch_url[256];
  int nu = snprintf(fetch_url, sizeof(fetch_url),
                    "%s%scurrent=%d&chipid=%s",
                    url,
                    strchr(url, '?') ? "&" : "?",
                    FIRMWARE_VERSION, chip_id);
  if (nu <= 0 || nu >= (int)sizeof(fetch_url)) {
    ESP_LOGE(TAG, "fetch_from_url: URL too long");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Remote OTA: current=v%d, chipid=%s", FIRMWARE_VERSION, chip_id);
  ESP_LOGI(TAG, "Remote OTA: GET %s", fetch_url);

  esp_http_client_config_t cfg = {};
  cfg.url = fetch_url;
  cfg.timeout_ms = 30000;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "esp_http_client_init failed");
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code  = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "HTTP status=%d, content-length=%d", status_code, content_length);

  if (status_code != 200 || content_length <= 0) {
    ESP_LOGW(TAG, "Remote OTA: нет прошивки (status=%d, len=%d)", status_code, content_length);
    esp_http_client_cleanup(client);
    return ESP_OK;
  }

  if ((size_t)content_length > esp_ota_get_next_update_partition(NULL)->size) {
    ESP_LOGE(TAG, "Remote OTA: firmware %d > partition size", content_length);
    esp_http_client_cleanup(client);
    return ESP_ERR_INVALID_SIZE;
  }

  err = write_stream((size_t)content_length, remote_ota_chunk_reader, client);
  esp_http_client_cleanup(client);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Remote OTA success, notifying server");
    ota_notify_server(chip_id, true, nullptr);
    ESP_LOGI(TAG, "Rebooting in 2s...");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
  } else {
    const char *err_name = esp_err_to_name(err);
    ESP_LOGE(TAG, "Remote OTA failed: %s", err_name);
    ota_notify_server(chip_id, false, err_name);
  }
  return err;
}

static esp_err_t local_ota_upload_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager*)req->user_ctx;
  if (!ota) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA context null");
    return ESP_FAIL;
  }

  char content_len_str[16];
  size_t content_len = 0;
  if (httpd_req_get_hdr_value_str(req, "Content-Length", content_len_str, sizeof(content_len_str)) == ESP_OK) {
    content_len = (size_t)atoi(content_len_str);
  }
  if (content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length header required");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Local OTA upload started, %u bytes", (unsigned)content_len);

  esp_err_t err = ota->write_stream(content_len, local_ota_chunk_reader, req);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Local OTA failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed");
    return ESP_FAIL;
  }

  esp_err_t ret = send_json_status(req, "success", true, "message", "OTA update successful");

  ESP_LOGI(TAG, "Local OTA success, rebooting in 2s...");
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  esp_restart();
  return ret;
}

static void ota_remote_task(void *arg) {
  OtaManager *ota = (OtaManager *)arg;
  ota->fetch_from_url(OTA_REMOTE_URL);
  ota->set_in_progress(false);
  vTaskDelete(NULL);
}

static esp_err_t remote_ota_upload_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;

  if (ota->in_progress()) {
    return send_json_status(req, "started", false, "error", "OTA already in progress");
  }

  ota->set_in_progress(true);
  BaseType_t ok = xTaskCreate(ota_remote_task, "ota_remote", 10 * 1024, ota, 5, NULL);
  if (ok != pdPASS) {
    ota->set_in_progress(false);
    return send_json_status(req, "started", false, "error", "Failed to start OTA task");
  }

  ESP_LOGI(TAG, "Remote OTA task started, url=%s", OTA_REMOTE_URL);
  return send_json_status(req, "started", true, "message", "OTA fetch started in background");
}

void OtaManager::register_handlers(httpd_handle_t httpd) {
  httpd_uri_t local_ota_upload = {
    .uri = "/api/ota/local-upload",
    .method = HTTP_POST,
    .handler = local_ota_upload_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &local_ota_upload);

  httpd_uri_t remote_ota_upload = {
    .uri = "/api/ota/remote-upload",
    .method = HTTP_POST,
    .handler = remote_ota_upload_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &remote_ota_upload);
}
