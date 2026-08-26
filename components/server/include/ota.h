#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <atomic>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

class OtaManager {

  private:
    std::atomic<bool> m_in_progress{false};

  public:
    void register_handlers(httpd_handle_t httpd);

    bool in_progress() const { return m_in_progress.load(); }
    void set_in_progress(bool v) { m_in_progress.store(v); }

    esp_err_t write_stream(size_t total_size,
                            int (*reader)(void *ctx, char *buf, int max_len),
                            void *ctx);

    esp_err_t fetch_from_url(const char *url);
};

#endif // OTA_MANAGER_H
