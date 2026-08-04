#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t https_server_start(uint16_t port);

void https_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_SERVER_H