#include "auth.h"
#include "nvs_proxy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <string>
#include <utility>

static const char *TAG = "auth";

#define STORAGE_AUTH "auth"
#define KEY_AUTH_USER "user"
#define KEY_AUTH_PASS "pass"

#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"
#define MAX_AUTH_USER 32
#define MAX_AUTH_PASS 64

static SemaphoreHandle_t auth_mutex = nullptr;
static std::string auth_user;
static std::string auth_pass;
static std::string auth_token;   // base64("user:pass")
static bool auth_ready = false;

static NVSProxy &store()
{
  static NVSProxy settings(STORAGE_AUTH, false);
  return settings;
}

// base64("user:pass") — именно эту строку браузер присылает после "Basic ".
// Вызывается под auth_mutex.
static bool build_token_locked()
{
  std::string raw = auth_user + ":" + auth_pass;
  size_t out_len = 0;
  size_t out_size = ((raw.size() + 2) / 3) * 4 + 1;
  std::string out(out_size, '\0');

  int rc = mbedtls_base64_encode(
      reinterpret_cast<unsigned char *>(out.data()), out.size(), &out_len,
      reinterpret_cast<const unsigned char *>(raw.data()), raw.size());
  if (rc != 0) {
    ESP_LOGE(TAG, "base64 encode failed: %d", rc);
    return false;
  }

  out.resize(out_len);
  auth_token = std::move(out);
  return true;
}

void auth_init()
{
  if (auth_ready) return;

  if (auth_mutex == nullptr) {
    auth_mutex = xSemaphoreCreateMutex();
    if (auth_mutex == nullptr) {
      ESP_LOGE(TAG, "failed to create mutex");
      return;
    }
  }

  if (xSemaphoreTake(auth_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    ESP_LOGE(TAG, "failed to take mutex");
    return;
  }

  auth_user = store().get(KEY_AUTH_USER, DEFAULT_USER);
  auth_pass = store().get(KEY_AUTH_PASS, DEFAULT_PASS);
  if (auth_user.empty()) auth_user = DEFAULT_USER;
  if (auth_pass.empty()) auth_pass = DEFAULT_PASS;

  auth_ready = build_token_locked();

  xSemaphoreGive(auth_mutex);

  if (auth_ready) {
    ESP_LOGI(TAG, "basic auth ready, user=%s", auth_user.c_str());
  }
}

bool auth_check(httpd_req_t *req)
{
  if (!auth_ready) auth_init();

  char header[256];
  bool ok = false;

  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK &&
      auth_mutex != nullptr &&
      xSemaphoreTake(auth_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    ok = auth_ready &&
         strncmp(header, "Basic ", 6) == 0 &&
         auth_token == (header + 6);
    xSemaphoreGive(auth_mutex);
  }

  if (ok) return true;

  // Без WWW-Authenticate: браузер не должен показывать свой системный Basic-диалог.
  // Страница входа рисуется самим приложением.
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
  return false;
}

bool auth_get_user(std::string &user)
{
  if (!auth_ready) auth_init();
  if (auth_mutex == nullptr) return false;

  if (xSemaphoreTake(auth_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
  user = auth_user;
  xSemaphoreGive(auth_mutex);
  return true;
}

bool auth_set_credentials(const char *user, const char *pass, const char **error)
{
  if (error) *error = nullptr;

  if (user == nullptr || user[0] == '\0') {
    if (error) *error = "логин не может быть пустым";
    return false;
  }

  if (strlen(user) > MAX_AUTH_USER) {
    if (error) *error = "логин длиннее 32 символов";
    return false;
  }

  if (strchr(user, ':') != nullptr) {
    if (error) *error = "логин не должен содержать двоеточие";
    return false;
  }

  if (pass != nullptr && strlen(pass) > MAX_AUTH_PASS) {
    if (error) *error = "пароль длиннее 64 символов";
    return false;
  }

  if (!auth_ready) auth_init();
  if (auth_mutex == nullptr ||
      xSemaphoreTake(auth_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    if (error) *error = "внутренняя ошибка";
    return false;
  }

  std::string new_user(user);
  std::string new_pass = auth_pass;
  if (pass != nullptr && pass[0] != '\0') new_pass = pass;

  if (new_pass.empty()) {
    xSemaphoreGive(auth_mutex);
    if (error) *error = "пароль не может быть пустым";
    return false;
  }

  bool ok = store().set(KEY_AUTH_USER, new_user);
  if (ok) ok = store().set(KEY_AUTH_PASS, new_pass);
  if (ok) ok = store().commit();

  if (!ok) {
    xSemaphoreGive(auth_mutex);
    if (error) *error = "не удалось сохранить в NVS";
    return false;
  }

  auth_user = new_user;
  auth_pass = new_pass;
  auth_ready = build_token_locked();

  xSemaphoreGive(auth_mutex);

  if (!auth_ready) {
    if (error) *error = "не удалось обновить токен";
    return false;
  }

  ESP_LOGI(TAG, "credentials changed, user=%s", auth_user.c_str());
  return true;
}
