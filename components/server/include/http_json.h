#ifndef HTTP_JSON_H
#define HTTP_JSON_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

esp_err_t send_resp_json(cJSON *resp, httpd_req_t *req);

esp_err_t send_json_status(httpd_req_t *req, const char *bool_key, bool val, const char *str_key, const char *str_val);

#endif // HTTP_JSON_H
