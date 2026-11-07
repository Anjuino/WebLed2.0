#include "ota.h"
#include "helper_server.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "private.h"
#include "version.h"
#include "device.h"

// Адрес сервера обновлений хранится в NVS без схемы: "203.0.113.5:5000".
// Схема всегда https. Если адрес не задан — удалённое обновление недоступно.
#define OTA_PATH_FIRMWARE  "/getfirmware"
#define OTA_PATH_ACK       "/ack"
#define OTA_PATH_CHECK     "/api/firmware/check"
#define OTA_TOKEN_MAX      32

static const char *TAG = "OTA";

static std::string trim_copy(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Адрес — только host[:port], без схемы, пробелов и слешей.
static bool is_valid_server(const std::string &s) {
  if (s.empty() || s.size() > 64) return false;
  if (s.find("://") != std::string::npos) return false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/') return false;
  }
  return true;
}

static bool is_valid_token(const std::string &s) {
  if (s.size() > OTA_TOKEN_MAX) return false;
  for (char c : s) {
    if (c < 0x21 || c > 0x7E) return false;
  }
  return true;
}

OtaManager::OtaManager() : m_settings(STORAGE_DEVICE, false) {
  load_config();
}

void OtaManager::load_config() {
  std::string s = trim_copy(m_settings.get(KEY_OTA_SERVER, ""));
  m_server = is_valid_server(s) ? s : "";

  std::string t = m_settings.get(KEY_OTA_TOKEN, "");
  m_token = is_valid_token(t) ? t : "";
}

bool OtaManager::set_server(const std::string &value) {
  std::string s = trim_copy(value);
  if (!is_valid_server(s)) return false;
  if (!m_settings.set_string(KEY_OTA_SERVER, s.c_str(), true)) return false;
  m_server = s;
  return true;
}

bool OtaManager::set_token(const std::string &value) {
  std::string t = trim_copy(value);
  if (!is_valid_token(t)) return false;
  if (!m_settings.set_string(KEY_OTA_TOKEN, t.c_str(), true)) return false;
  m_token = t;
  return true;
}

void OtaManager::set_update_info(bool available, const std::string &version, size_t size) {
  std::lock_guard<std::mutex> lock(m_update_mutex);
  m_update_available = available;
  m_update_version = available ? version : "";
  m_update_size = available ? size : 0;
}

bool OtaManager::update_available() {
  std::lock_guard<std::mutex> lock(m_update_mutex);
  return m_update_available;
}

std::string OtaManager::update_version() {
  std::lock_guard<std::mutex> lock(m_update_mutex);
  return m_update_version;
}

size_t OtaManager::update_size() {
  std::lock_guard<std::mutex> lock(m_update_mutex);
  return m_update_size;
}

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
  cfg.cert_pem = cert_remote_ca_crt_start;
  cfg.skip_cert_common_name_check = true; // адрес сервера прошивок ещё не зафиксирован — проверяем только CA

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

static void ota_notify_server(const std::string &base,
                              const char *chipid,
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
  std::string url = base + OTA_PATH_ACK;
  ota_post_json_log(url.c_str(), body, "OTA ack");
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
                    "%s%scurrent=%s&chipid=%s",
                    url,
                    strchr(url, '?') ? "&" : "?",
                    firmware_version(), chip_id);
  if (nu <= 0 || nu >= (int)sizeof(fetch_url)) {
    ESP_LOGE(TAG, "fetch_from_url: URL too long");
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Remote OTA: current=%s, chipid=%s", firmware_version(), chip_id);
  ESP_LOGI(TAG, "Remote OTA: GET %s", fetch_url);

  esp_http_client_config_t cfg = {};
  cfg.url = fetch_url;
  cfg.timeout_ms = 30000;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 4096;
  cfg.cert_pem = cert_remote_ca_crt_start;
  cfg.skip_cert_common_name_check = true; // адрес сервера прошивок ещё не зафиксирован — проверяем только CA

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
    ota_notify_server(base_url(), chip_id, true, nullptr);
    ESP_LOGI(TAG, "Rebooting in 2s...");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
  } else {
    const char *err_name = esp_err_to_name(err);
    ESP_LOGE(TAG, "Remote OTA failed: %s", err_name);
    ota_notify_server(base_url(), chip_id, false, err_name);
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
  std::string url = ota->base_url() + OTA_PATH_FIRMWARE;
  ESP_LOGI(TAG, "Remote OTA: GET %s", url.c_str());
  ota->fetch_from_url(url.c_str());
  ota->set_in_progress(false);
  vTaskDelete(NULL);
}

static esp_err_t remote_ota_upload_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;

  if (ota->in_progress()) {
    return send_json_status(req, "started", false, "error", "OTA already in progress");
  }

  if (ota->server().empty()) {
    return send_json_status(req, "started", false, "error", "OTA server not configured");
  }

  ota->set_in_progress(true);
  BaseType_t ok = xTaskCreate(ota_remote_task, "ota_remote", 10 * 1024, ota, 8, NULL);
  if (ok != pdPASS) {
    ota->set_in_progress(false);
    return send_json_status(req, "started", false, "error", "Failed to start OTA task");
  }

  ESP_LOGI(TAG, "Remote OTA task started, server=%s", ota->server().c_str());
  return send_json_status(req, "started", true, "message", "OTA fetch started in background");
}

static esp_err_t ota_config_get_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;
  cJSON *root = cJSON_CreateObject();
  if (!root) return send_json_status(req, "status", false, "error", "OOM");
  cJSON_AddStringToObject(root, "server", ota->server().c_str());
  cJSON_AddStringToObject(root, "token", ota->token().c_str());
  return send_resp_json(root, req);
}

static esp_err_t ota_config_set_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;

  char *buf = nullptr;
  if (getjson(req, &buf) != ESP_OK) return ESP_FAIL;

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *j_server = cJSON_GetObjectItem(root, "server");
  cJSON *j_token  = cJSON_GetObjectItem(root, "token");

  bool ok = true;
  const char *err = nullptr;

  if (j_server) {
    if (!cJSON_IsString(j_server) || !ota->set_server(j_server->valuestring)) {
      ok = false;
      err = "invalid server address";
    }
  }
  if (ok && j_token) {
    if (!cJSON_IsString(j_token) || !ota->set_token(j_token->valuestring)) {
      ok = false;
      err = "invalid token (max 32 printable chars)";
    }
  }

  cJSON_Delete(root);

  if (!ok) return send_json_status(req, "status", false, "error", err);
  return send_json_status(req, "status", true, nullptr, nullptr);
}

static std::string ota_error_json(const char *error, int http_status = 0) {
  cJSON *o = cJSON_CreateObject();
  if (!o) return R"({"status":false,"error":"OOM"})";
  cJSON_AddBoolToObject(o, "status", false);
  cJSON_AddStringToObject(o, "error", error ? error : "unknown");
  if (http_status) cJSON_AddNumberToObject(o, "http", http_status);
  char *js = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  std::string result = js ? js : R"({"status":false,"error":"OOM"})";
  if (js) free(js);
  return result;
}

static std::string ota_check_remote(OtaManager *ota) {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  char chip[24];
  snprintf(chip, sizeof(chip), "%02X-%02X-%02X-%02X-%02X-%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  char url[320];
  int n = snprintf(url, sizeof(url), "%s%s?chipid=%s&version=%s",
                   ota->base_url().c_str(), OTA_PATH_CHECK, chip, firmware_version());
  if (n <= 0 || n >= (int)sizeof(url)) return ota_error_json("url too long");

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = 8000;
  cfg.buffer_size = 1024;
  cfg.cert_pem = cert_remote_ca_crt_start;
  cfg.skip_cert_common_name_check = true;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return ota_error_json("client init failed");

  if (!ota->token().empty()) {
    esp_http_client_set_header(client, "X-Device-Token", ota->token().c_str());
  }
  esp_http_client_set_header(client, "Accept", "application/json");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "OTA check: open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return ota_error_json(esp_err_to_name(err));
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(TAG, "OTA check: HTTP %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ota_error_json("server returned non-200", status);
  }

  char body[2048];
  int total = 0;
  while (total < (int)sizeof(body) - 1) {
    int r = esp_http_client_read(client, body + total, sizeof(body) - 1 - total);
    if (r <= 0) break;
    total += r;
  }
  body[total] = '\0';
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (total == 0) return ota_error_json("empty server response");

  cJSON *in = cJSON_Parse(body);
  if (!in) return ota_error_json("invalid server response");

  bool update = cJSON_IsTrue(cJSON_GetObjectItem(in, "update"));

  std::string version;
  cJSON *j_version = cJSON_GetObjectItem(in, "version");
  if (cJSON_IsString(j_version)) version = j_version->valuestring;

  size_t size = 0;
  cJSON *j_size = cJSON_GetObjectItem(in, "size");
  if (cJSON_IsNumber(j_size) && j_size->valuedouble > 0) size = (size_t)j_size->valuedouble;

  ota->set_update_info(update, version, size);
  cJSON_Delete(in);

  ESP_LOGI(TAG, "OTA check: update=%d version=%s", update ? 1 : 0, version.c_str());
  return body;
}

static esp_err_t ota_check_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;

  if (ota->server().empty()) {
    return send_json_status(req, "status", false, "error", "OTA server not configured");
  }

  ota->set_update_info(false, "", 0);

  std::string result = ota_check_remote(ota);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, result.c_str());
}

static esp_err_t ota_install_handler(httpd_req_t *req) {
  OtaManager *ota = (OtaManager *)req->user_ctx;
  if (ota->in_progress()) {
    return send_json_status(req, "started", false, "error", "OTA already in progress");
  }
  
  ESP_LOGI(TAG, "OTA install requested (mock)");
  return send_json_status(req, "started", true, "message", "mock install started");
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

  httpd_uri_t ota_config_get = {
    .uri = "/api/ota/config",
    .method = HTTP_GET,
    .handler = ota_config_get_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &ota_config_get);

  httpd_uri_t ota_config_set = {
    .uri = "/api/ota/config",
    .method = HTTP_POST,
    .handler = ota_config_set_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &ota_config_set);

  httpd_uri_t ota_check = {
    .uri = "/api/ota/check",
    .method = HTTP_GET,
    .handler = ota_check_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &ota_check);

  httpd_uri_t ota_install = {
    .uri = "/api/ota/install",
    .method = HTTP_POST,
    .handler = ota_install_handler,
    .user_ctx = this
  };
  httpd_register_uri_handler(httpd, &ota_install);
}
