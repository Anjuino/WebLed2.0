#ifndef HELPER_SERVER_H
#define HELPER_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

esp_err_t send_resp_json(cJSON *resp, httpd_req_t *req);

esp_err_t send_json_status(httpd_req_t *req, const char *bool_key, bool val, const char *str_key, const char *str_val);

// Читает query-строку запроса и сразу достаёт из неё один параметр key в
// out. Ничего клиенту не отправляет и не логирует при отсутствии — это
// нормальный случай (например, перебор нескольких возможных параметров
// подряд), а не ошибка. false — параметра нет (или query-строки нет вовсе).
bool get_query_param(httpd_req_t *req, const char *key, char *out, size_t out_len);

// Читает тело запроса целиком в новый буфер (malloc, владение переходит
// вызывающему — освобождать через free()). Ограничение на размер — 4096
// байт. Сама логирует, отправляет клиенту ошибку и возвращает ESP_FAIL
// при пустом/слишком большом теле или обрыве соединения.
esp_err_t getjson(httpd_req_t *req, char **output);

#endif // HELPER_SERVER_H
