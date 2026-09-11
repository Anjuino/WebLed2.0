#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "led.h"
#include "esp_http_server.h"

// Запускает движок скриптов: инициализирует Wrench VM, регистрирует биндинги
// (led::setPixel/show/... для скриптов) и поднимает отдельную FreeRTOS-задачу,
// в которой и происходит вся компиляция. Вызывать один раз, до первого
// script_engine_compile_and_register().
void script_engine_start(class led *wLed);

// Компилирует source и регистрирует его как эффект led с указанным id
// (см. led::add_effect). Сама компиляция выполняется не в вызывающем
// контексте, а в задаче движка — этот вызов ставит задание в очередь и
// блокируется до готовности результата (компиляция в Wrench быстрая,
// поэтому синхронное ожидание не проблема).
//
// error/error_len — опциональный буфер под текст ошибки компиляции, если
// вызов вернул false.
bool script_engine_compile_and_register(const char *name, uint8_t id, bool continuous,
                                         const char *source, int source_len = -1,
                                         char *error = nullptr, size_t error_len = 0);

// Регистрирует POST /api/led/scripts на переданном httpd — тело запроса
// это текст скрипта, id/name/continuous передаются query-параметрами.
void script_engine_register_handlers(httpd_handle_t httpd);

#endif
