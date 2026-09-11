idf_component_register(
    SRCS "main.cpp" 
    REQUIRES server led device_manager nvs_proxy esp_littlefs wifi_manager server
                    nvs_flash esp_event esp_netif esp_wifi mdns esp_timer
                    scripting)
