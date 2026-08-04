idf_component_register(
    SRCS "src/wifimanager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_event esp_netif esp_wifi nvs_proxy cjson
)