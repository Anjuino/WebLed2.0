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
#include <stdarg.h>
#include <string>

static const char *TAG = "ScriptEngine";

static led   *g_led = nullptr;
static FileSystem   *g_fs = nullptr;
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
  bool registered;     // эффект зарегистрирован сейчас; false при ok — только проверен (id занят старой версией)
  char error[160];
};

// ошибка задания: текст — вызывающему (веб/лог загрузки) и сразу в лог,
// чтобы провал компиляции не терялся молча
static void job_fail(CompileJob *job, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(job->error, sizeof(job->error), fmt, ap);
    va_end(ap);
    ESP_LOGW(TAG, "script '%s' id=%u: %s", job->name.c_str(), job->id, job->error);
}

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

// эффект с этим id, если это скрипт (у встроенных apply другой), иначе nullptr
static const LedEffect *find_script_effect(uint8_t id)
{
    size_t count = 0;
    const LedEffect *fx = g_led->effects(&count);
    for (size_t i = 0; i < count; i++) {
        if (fx[i].id == id) return fx[i].apply == &script_apply ? &fx[i] : nullptr;
    }
    return nullptr;
}

// Собственно компиляция + регистрация — выполняется только внутри
// script_engine_task(), никогда напрямую из вызывающего кода.
static void process_job(CompileJob *job)
{
    job->ok = false;
    job->registered = false;
    job->error[0] = 0;

    if (!g_w || !g_led) {
        job_fail(job, "engine not initialised");
        return;
    }
    if (job->source.empty()) {
        job_fail(job, "empty source");
        return;
    }

    // id занят тем же скриптом (пересохранение) — новую версию только проверяем:
    // замены эффекта на лету пока нет, применится после перезагрузки
    bool validate_only = false;
    if (g_led->is_valid_mode(job->id)) {
        const LedEffect *same = find_script_effect(job->id);
        if (!same || same->name != job->name) {
            job_fail(job, "id %u already in use", job->id);
            return;
        }
        validate_only = true;
    }

    if (xSemaphoreTake(g_vm_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        job_fail(job, "vm busy, try again");
        return;
    }

    char errMsg[150] = {0};
    unsigned char *bytecode = nullptr;
    int bytecode_len = 0;

    WRError err = wr_compile(job->source.c_str(), (int)job->source.size(), &bytecode, &bytecode_len,
                             errMsg, WR_INCLUDE_GLOBALS | WR_NON_STRICT_VAR);
    if (err != WR_ERR_None) {
        job_fail(job, "compile: %s", errMsg[0] ? errMsg : "unknown error");
        if (bytecode) wr_free(bytecode);
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    WRContext *ctx = wr_newContext(g_w, bytecode, bytecode_len, true);
    if (!ctx) {
        job_fail(job, "wr_newContext failed: err=%d", (int)wr_getLastError(g_w));
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    WRValue *top = wr_executeContext(ctx);

    // верхний уровень обязан завершиться. Прерван по лимиту инструкций — значит бесконечный
    // (или слишком долгий) цикл. Такой контекст нельзя регистрировать: каждый вызов step()
    // продолжал бы этот цикл, и задача Led съела бы ядро до watchdog
    if (wr_getYieldInfo(ctx)) {
        job_fail(job, "top-level code did not finish (infinite loop?) - put the loop logic into step()");
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        return;
    }
    if (!top && wr_getLastError(g_w) != WR_ERR_None) {
        job_fail(job, "runtime error in top-level code: err=%d", (int)wr_getLastError(g_w));
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    if (!wr_getFunction(ctx, "step")) {
        job_fail(job, "script does not define step()");
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        return;
    }

    if (validate_only) {
        wr_destroyContext(ctx);
        xSemaphoreGive(g_vm_mutex);
        ESP_LOGI(TAG, "script '%s' id=%u: OK, applies after reboot (id is held by the current version)",
                 job->name.c_str(), job->id);
        job->ok = true;
        return;
    }

    ScriptEffect *fx = (ScriptEffect *)malloc(sizeof(ScriptEffect));
    if (!fx) {
        job_fail(job, "out of memory");
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
    job->registered = true;
}

// Загрузка скриптов с диска при старте. Выполняется на этой же задаче,
// поэтому идти через g_job_queue/submit_compile_job нельзя — обработчик
// очереди ниже (while(true)) ещё не запущен, и задача встанет в дедлок,
// ожидая сама себя. Вместо этого process_job() дергается напрямую.
static void script_engine_load_from_fs()
{
    std::vector<fileinfo_t> list = g_fs->getfilelist();
    for (auto &item : list) {
        std::string file = g_fs->getfile(item.name.c_str());
        if (file.empty()) continue;

        cJSON *root = cJSON_Parse(file.c_str());
        cJSON *id = cJSON_GetObjectItem(root, "id");
        cJSON *continuous = cJSON_GetObjectItem(root, "continuous");
        cJSON *script = cJSON_GetObjectItem(root, "text");

        if (cJSON_IsNumber(id) && cJSON_IsBool(continuous) && cJSON_IsString(script)) {
            CompileJob boot_job{};
            boot_job.name = item.name;
            boot_job.source = script->valuestring;
            boot_job.continuous = cJSON_IsTrue(continuous);
            boot_job.id = (uint8_t)id->valueint;

            process_job(&boot_job);

            if (boot_job.ok) {
                ESP_LOGI(TAG, "loaded script '%s' id=%u", item.name.c_str(), boot_job.id);
            }   // ошибку уже написал job_fail
        } else {
            ESP_LOGW(TAG, "skip '%s': invalid script file format", item.name.c_str());
        }

        cJSON_Delete(root);
    }
}

static void script_engine_task(void *)
{
    CompileJob *job;

    script_engine_load_from_fs();

    while (true) {
        if (xQueueReceive(g_job_queue, &job, portMAX_DELAY) == pdTRUE) {
            process_job(job);
            xSemaphoreGive(job->done);
        }
    }
}

static bool submit_compile_job(const char *name, uint8_t id, bool continuous,
                                const char *source, int source_len,
                                char *error, size_t error_len, bool *registered)
{
    if (registered) *registered = false;
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
    job.registered = false;
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

    if (registered) *registered = job.registered;
    if (!job.ok && error && error_len) {
        strncpy(error, job.error, error_len - 1);
        error[error_len - 1] = 0;
    }
    return job.ok;
}

void script_engine_start(led *wLed, class FileSystem *_fs)
{
    g_led = wLed;
    g_fs = _fs;
    g_w   = wr_newState();
    wr_setInstructionsPerSlice(g_w, 100000);
    g_vm_mutex = xSemaphoreCreateMutex();
    g_job_queue = xQueueCreate(2, sizeof(CompileJob*));

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

    xTaskCreatePinnedToCore(script_engine_task, "ScriptEngine", 10 * 1024, nullptr, 5, nullptr, 1);

    ESP_LOGI(TAG, "script engine started");
}

bool script_engine_compile_and_register(const char *name, uint8_t id, bool continuous,
                                         const char *source, int source_len,
                                         char *error, size_t error_len, bool *registered)
{
    return submit_compile_job(name, id, continuous, source, source_len, error, error_len, registered);
}