#include "script_engine.h"
#include "wrench.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

static const char *TAG = "ScriptEngine";

static led   *g_led = nullptr;
static WRState *g_w = nullptr;

// Все обращения к состоянию Wrench VM (компиляция И вызов step() из
// led::task()) идут под этим мьютексом — Wrench не рассчитан на то, что
// в него одновременно лезут из двух ОС-потоков (script-таск компилирует,
// led-таск исполняет).
static SemaphoreHandle_t g_vm_mutex = nullptr;

// Задание на компиляцию, которое ставится в очередь script-таска.
// Живёт на стеке вызывающего (submit_compile_job блокируется до done),
// так что таску достаточно просто заполнить поля и отдать семафор.
struct CompileJob {
  std::string name;
  std::string source;
  bool continuous;
  uint8_t id;
  SemaphoreHandle_t done;
  bool ok;
  char error[128];
};

static QueueHandle_t g_job_queue = nullptr;

static void lib_led_count(WRValue *stackTop, const int argn, WRContext *)
{
    wr_makeInt(stackTop, g_led ? (int)g_led->get_count_led() : 0);
}

static void lib_led_speed(WRValue *stackTop, const int argn, WRContext *)
{
    wr_makeInt(stackTop, g_led ? (int)g_led->get_speed() : 0);
}

static void lib_led_setPixel(WRValue *stackTop, const int argn, WRContext *)
{
    if (argn >= 4 && g_led) {
        int i = stackTop[-4].asInt();
        int r = stackTop[-3].asInt();
        int g = stackTop[-2].asInt();
        int b = stackTop[-1].asInt();
        g_led->set_pixel((uint16_t)i, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    wr_makeInt(stackTop, 0);
}

static void lib_led_show(WRValue *stackTop, const int argn, WRContext *)
{
    if (g_led) g_led->show();
    wr_makeInt(stackTop, 0);
}

static void lib_led_clear(WRValue *stackTop, const int argn, WRContext *)
{
    if (g_led) g_led->off();
    wr_makeInt(stackTop, 0);
}

static void lib_led_getR(WRValue *stackTop, const int argn, WRContext *)
{
    auto [r, g, b] = g_led->get_color();
    (void)g; (void)b;
    wr_makeInt(stackTop, (int)r);
}

static void lib_led_getG(WRValue *stackTop, const int argn, WRContext *)
{
    auto [r, g, b] = g_led->get_color();
    (void)r; (void)b;
    wr_makeInt(stackTop, (int)g);
}

static void lib_led_getB(WRValue *stackTop, const int argn, WRContext *)
{
    auto [r, g, b] = g_led->get_color();
    (void)r; (void)g;
    wr_makeInt(stackTop, (int)b);
}

static void lib_led_delay(WRValue *stackTop, const int argn, WRContext *)
{
    int ms = (argn >= 1) ? stackTop[-1].asInt() : 0;
    if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms));
    wr_makeInt(stackTop, 0);
}

struct ScriptEffect {
    WRContext *ctx;
};

// Вызывается из led::task() — другой таск, отсюда и мьютекс.
static void script_apply(led *self, void *user_data)
{
    ScriptEffect *fx = static_cast<ScriptEffect *>(user_data);
    if (!fx || !fx->ctx) return;

    if (xSemaphoreTake(g_vm_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "script_apply: не удалось захватить mutex VM, кадр пропущен");
        return;
    }
    wr_callFunction(fx->ctx, "step");
    xSemaphoreGive(g_vm_mutex);
}

// Собственно компиляция + регистрация — выполняется только внутри
// script_engine_task(), никогда напрямую из вызывающего кода.
static void process_job(CompileJob *job)
{
    job->ok = false;
    job->error[0] = 0;

    if (!g_w || !g_led) {
        snprintf(job->error, sizeof(job->error), "engine not initialised");
        return;
    }
    if (job->source.empty()) {
        snprintf(job->error, sizeof(job->error), "empty source");
        return;
    }
    if (g_led->is_valid_mode(job->id)) {
        snprintf(job->error, sizeof(job->error), "id %u already in use", job->id);
        return;
    }

    if (xSemaphoreTake(g_vm_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        snprintf(job->error, sizeof(job->error), "vm busy, try again");
        return;
    }

    char errMsg[128] = {0};
    unsigned char *bytecode = nullptr;
    int bytecode_len = 0;

    WRError err = wr_compile(job->source.c_str(), (int)job->source.size(), &bytecode, &bytecode_len,
                             errMsg, WR_INCLUDE_GLOBALS | WR_NON_STRICT_VAR);
    if (err != WR_ERR_None) {
        snprintf(job->error, sizeof(job->error), "compile: %s", errMsg[0] ? errMsg : "unknown error");
        if (bytecode) wr_free(bytecode);
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    WRContext *ctx = wr_newContext(g_w, bytecode, bytecode_len, true);
    if (!ctx) {
        snprintf(job->error, sizeof(job->error), "wr_newContext failed: err=%d", (int)wr_getLastError(g_w));
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    wr_executeContext(ctx);

    if (!wr_getFunction(ctx, "step")) {
        snprintf(job->error, sizeof(job->error), "script does not define step()");
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    ScriptEffect *fx = (ScriptEffect *)malloc(sizeof(ScriptEffect));
    if (!fx) {
        snprintf(job->error, sizeof(job->error), "out of memory");
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        return;
    }
    fx->ctx = ctx;

    xSemaphoreGive(g_vm_mutex);

    // add_effect трогает effects_table у led, не WRState — отдельный мьютекс ей не нужен
    // (это единственный писатель, вызывается только отсюда).
    g_led->add_effect(job->id, job->name.c_str(), job->continuous, &script_apply, fx);

    ESP_LOGI(TAG, "registered script effect id=%u name='%s' bytecode=%d B", job->id, job->name.c_str(), bytecode_len);
    job->ok = true;
}

static void script_engine_task(void *)
{
    CompileJob *job;
    while (true) {
        if (xQueueReceive(g_job_queue, &job, portMAX_DELAY) == pdTRUE) {
            process_job(job);
            xSemaphoreGive(job->done);
        }
    }
}

static bool submit_compile_job(const char *name, uint8_t id, bool continuous,
                                const char *source, int source_len,
                                char *error, size_t error_len)
{
    if (!g_job_queue) {
        if (error && error_len) snprintf(error, error_len, "engine not started");
        return false;
    }

    CompileJob job;
    job.name = name ? name : "";
    job.source.assign(source, source_len < 0 ? strlen(source) : (size_t)source_len);
    job.continuous = continuous;
    job.id = id;
    job.ok = false;
    job.error[0] = 0;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) {
        if (error && error_len) snprintf(error, error_len, "out of memory");
        return false;
    }

    CompileJob *job_ptr = &job;
    if (xQueueSend(g_job_queue, &job_ptr, pdMS_TO_TICKS(2000)) != pdTRUE) {
        vSemaphoreDelete(job.done);
        if (error && error_len) snprintf(error, error_len, "engine busy, try again");
        return false;
    }

    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);

    if (!job.ok && error && error_len) {
        strncpy(error, job.error, error_len - 1);
        error[error_len - 1] = 0;
    }
    return job.ok;
}

void script_engine_start(led *wLed)
{
    g_led = wLed;
    g_w   = wr_newState();
    g_vm_mutex = xSemaphoreCreateMutex();
    g_job_queue = xQueueCreate(4, sizeof(CompileJob*));

    wr_loadMathLib(g_w);

    wr_registerLibraryFunction(g_w, "led::count",    &lib_led_count);
    wr_registerLibraryFunction(g_w, "led::speed",    &lib_led_speed);
    wr_registerLibraryFunction(g_w, "led::setPixel", &lib_led_setPixel);
    wr_registerLibraryFunction(g_w, "led::show",     &lib_led_show);
    wr_registerLibraryFunction(g_w, "led::clear",    &lib_led_clear);
    wr_registerLibraryFunction(g_w, "led::getR",     &lib_led_getR);
    wr_registerLibraryFunction(g_w, "led::getG",     &lib_led_getG);
    wr_registerLibraryFunction(g_w, "led::getB",     &lib_led_getB);
    wr_registerLibraryFunction(g_w, "led::delay",    &lib_led_delay);

    // Стек с запасом: компилятор Wrench рекурсивный, на main-таске раньше
    // из-за этого приходилось раздувать CONFIG_ESP_MAIN_TASK_STACK_SIZE —
    // теперь это изолировано в собственной задаче и main это не касается.
    xTaskCreatePinnedToCore(script_engine_task, "ScriptEngine", 16 * 1024, nullptr, 5, nullptr, 1);

    ESP_LOGI(TAG, "script engine started");
}

bool script_engine_compile_and_register(const char *name, uint8_t id, bool continuous,
                                         const char *source, int source_len,
                                         char *error, size_t error_len)
{
    return submit_compile_job(name, id, continuous, source, source_len, error, error_len);
}

// ---------------------------------------------------------------------
// HTTP: POST /api/led/scripts?id=N&name=...&continuous=true, тело — исходник
// ---------------------------------------------------------------------

static esp_err_t send_json(httpd_req_t *req, int status, cJSON *resp)
{
    httpd_resp_set_status(req, status == 200 ? HTTPD_200 :
                                status == 400 ? HTTPD_400 :
                                status == 409 ? "409 Conflict" : HTTPD_500);
    httpd_resp_set_type(req, "application/json");
    char *json = cJSON_Print(resp);
    esp_err_t err = json ? httpd_resp_sendstr(req, json) : ESP_FAIL;
    if (json) free(json);
    cJSON_Delete(resp);
    return err;
}

static bool get_query_param(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) return false;

    char *query = (char *)malloc(qlen + 1);
    if (!query) return false;

    bool found = false;
    if (httpd_req_get_url_query_str(req, query, qlen + 1) == ESP_OK) {
        found = httpd_query_key_value(query, key, out, out_len) == ESP_OK;
    }
    free(query);
    return found;
}

static esp_err_t upload_script_handler(httpd_req_t *req)
{
    char id_str[8] = {0};
    char name[64] = "script";
    char continuous_str[8] = {0};

    if (!get_query_param(req, "id", id_str, sizeof(id_str))) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "missing ?id=");
        return send_json(req, 400, resp);
    }
    int id = atoi(id_str);
    if (id < 0 || id > 255) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "id must be 0..255");
        return send_json(req, 400, resp);
    }

    get_query_param(req, "name", name, sizeof(name));
    bool continuous = get_query_param(req, "continuous", continuous_str, sizeof(continuous_str))
                       && strcmp(continuous_str, "false") != 0 && strcmp(continuous_str, "0") != 0;

    char content_len_str[16];
    size_t content_len = 0;
    if (httpd_req_get_hdr_value_str(req, "Content-Length", content_len_str, sizeof(content_len_str)) == ESP_OK) {
        content_len = (size_t)atoi(content_len_str);
    }
    if (content_len == 0 || content_len > 16384) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "Content-Length required, max 16384 bytes");
        return send_json(req, 400, resp);
    }

    char *source = (char *)malloc(content_len + 1);
    if (!source) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "error", "out of memory");
        return send_json(req, 500, resp);
    }

    size_t received = 0;
    while (received < content_len) {
        int r = httpd_req_recv(req, source + received, content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(source);
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "error", "failed to read body");
            return send_json(req, 400, resp);
        }
        received += r;
    }
    source[content_len] = 0;

    char error[128] = {0};
    bool ok = script_engine_compile_and_register(name, (uint8_t)id, continuous, source, (int)content_len,
                                                  error, sizeof(error));
    free(source);

    cJSON *resp = cJSON_CreateObject();
    if (ok) {
        cJSON_AddBoolToObject(resp, "success", true);
        cJSON_AddNumberToObject(resp, "id", id);
        return send_json(req, 200, resp);
    }

    cJSON_AddBoolToObject(resp, "success", false);
    cJSON_AddStringToObject(resp, "error", error);
    bool conflict = strstr(error, "already in use") != nullptr;
    return send_json(req, conflict ? 409 : 400, resp);
}

void script_engine_register_handlers(httpd_handle_t httpd)
{
    httpd_uri_t upload_script = {
        .uri = "/api/led/scripts",
        .method = HTTP_POST,
        .handler = upload_script_handler,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(httpd, &upload_script);
}
