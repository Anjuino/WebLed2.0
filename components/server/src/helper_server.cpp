#include "helper_server.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "HelperServer";

esp_err_t send_resp_json(cJSON *resp, httpd_req_t *req) {
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

esp_err_t send_json_status(httpd_req_t *req, const char *bool_key, bool val,
                            const char *str_key, const char *str_val) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, bool_key, val);
  if (str_key && str_val) cJSON_AddStringToObject(resp, str_key, str_val);
  return send_resp_json(resp, req);
}

bool get_query_param(httpd_req_t *req, const char *key, char *out, size_t out_len) {
  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
  return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

esp_err_t getjson(httpd_req_t *req, char **output) {
  size_t content_len = req->content_len;
  if(content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  if(content_len >= 8192) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
    return ESP_FAIL;
  }

  char *buf = (char*) malloc(content_len + 1);
  if(!buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    return ESP_FAIL;
  }

  size_t received = 0;
  while(received < content_len) {
    int ret = httpd_req_recv(req, buf + received, content_len - received);
    if(ret <= 0) {
      if(ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      free(buf);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv failed");
      return ESP_FAIL;
    }
    received += ret;
  }
  buf[received] = '\0';

  *output = buf;
  return ESP_OK;
}
