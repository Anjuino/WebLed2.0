#ifndef AUTH_H
#define AUTH_H

#include "esp_http_server.h"
#include <string>

// Инициализация: читает логин/пароль из NVS и кэширует их в RAM.
// Вызывается один раз при старте HTTP-сервера.
void auth_init();

// Проверяет заголовок Authorization: Basic ...
// При ошибке сам отправляет 401 и возвращает false.
bool auth_check(httpd_req_t *req);

// Текущий логин (пароль наружу не отдаём).
bool auth_get_user(std::string &user);

// Смена логина/пароля. Если pass пустой — пароль не меняется.
// Возвращает false и текст ошибки при неверных данных/ошибке NVS.
bool auth_set_credentials(const char *user, const char *pass, const char **error);

#endif // AUTH_H
