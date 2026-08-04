idf_component_register(SRCS "main.cpp" 
                            REQUIRES led device_manager nvs_proxy esp_littlefs wifi_manager nvs_flash esp_event esp_netif esp_wifi mdns server) 
