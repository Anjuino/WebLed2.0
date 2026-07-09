#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация HTTPS сервера
 * 
 * @param port Порт для HTTPS (обычно 443)
 * @param cert_pem Путь к сертификату или содержимое
 * @param key_pem Путь к приватному ключу или содержимое
 * @return esp_err_t ESP_OK при успехе
 */
esp_err_t https_server_start(uint16_t port);

/**
 * @brief Остановка HTTPS сервера
 */
void https_server_stop(void);

/**
 * @brief Регистрация обработчика для URL
 * 
 * @param uri URI путь (например, "/api/data")
 * @param method HTTP метод (0=GET, 1=POST, 2=PUT, 3=DELETE)
 * @param handler Функция-обработчик
 * @return esp_err_t 
 */
esp_err_t https_server_register_handler(
    const char *uri,
    int method,
    esp_err_t (*handler)(httpd_req_t *req)
);

/**
 * @brief Отправка JSON ответа
 */
esp_err_t https_server_send_json(httpd_req_t *req, const char *json_data);

/**
 * @brief Отправка текстового ответа
 */
esp_err_t https_server_send_text(httpd_req_t *req, const char *text);

/**
 * @brief Отправка ошибки
 */
esp_err_t https_server_send_error(httpd_req_t *req, int code, const char *message);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_SERVER_H