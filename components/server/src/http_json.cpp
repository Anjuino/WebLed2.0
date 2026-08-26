#include "http_json.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "HttpJson";

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
