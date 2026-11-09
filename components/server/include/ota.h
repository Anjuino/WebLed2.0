#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <string>
#include "esp_err.h"
#include "esp_http_server.h"
#include "nvs_proxy.h"

class led;

class OtaManager {

  private:
    std::atomic<bool> m_in_progress{false};
    NVSProxy m_settings;
    std::string m_server;
    std::string m_token = "None";

    std::mutex m_update_mutex;
    bool m_update_available = false;
    std::string m_update_version;
    size_t m_update_size = 0;

    std::atomic<int> m_progress{-1};
    std::mutex m_status_mutex;
    std::string m_last_error;

    led *m_led = nullptr;

    void load_config();

  public:
    OtaManager();

    void register_handlers(httpd_handle_t httpd);

    bool in_progress() const { return m_in_progress.load(); }
    void set_in_progress(bool v) { m_in_progress.store(v); }

    int progress() const { return m_progress.load(); }
    void set_progress(int v) { m_progress.store(v); }

    void set_led(led *l) { m_led = l; }

    // Начало любого обновления (remote/local): гасим ленту и помечаем процесс.
    void begin_update();
    // Конец обновления: ok=false — возвращаем ленту, ok=true — оставляем выключенной до ребута.
    void end_update(bool ok);

    void set_error(const std::string &e);
    std::string last_error();

    const std::string &server() const { return m_server; }
    const std::string &token() const { return m_token; }
    std::string base_url() const { return std::string("https://") + m_server; }

    bool set_server(const std::string &value);
    bool set_token(const std::string &value);

    void set_update_info(bool available, const std::string &version, size_t size);
    bool update_available();
    std::string update_version();
    size_t update_size();

    esp_err_t write_stream(size_t total_size,
                            int (*reader)(void *ctx, char *buf, int max_len),
                            void *ctx);

    esp_err_t install_from_url(const char *url, const char *version);
};

#endif // OTA_MANAGER_H
