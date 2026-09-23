#ifndef HTTPS_SERVER_H
#define HTTPS_SERVER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "led.h"
#include "ota.h"
#include "filesystem.h"
#include "wifimanager.h"
#include "helper_server.h"
#include "device.h"

class server {
  private:
    httpd_handle_t m_server = nullptr;
    OtaManager m_ota;
    class wifimanager *wifi = nullptr;
    class device *controller = nullptr;

    #ifdef CONFIG_SERVER_HTTPS
      void checkCertificate();
    #endif

  public:
    server(class wifimanager *_wifi, class device *_device);
    ~server();

    esp_err_t start();
    void stop();
    bool isRunning() const { return m_server != nullptr; }
};

#endif // HTTPS_SERVER_H
