#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "led.h"
#include "ota.h"

class server {
  private:

    class led *wLed = nullptr;
    httpd_handle_t m_server = nullptr;
    OtaManager m_ota;

    #ifdef CONFIG_SERVER_HTTPS
      void checkCertificate();
    #endif

  public:
    server(class led *_led);
    ~server();

    esp_err_t start();
    void stop();
    bool isRunning() const { return m_server != nullptr; }
};

#endif // HTTPS_SERVER_H
