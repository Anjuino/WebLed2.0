idf_component_register(
    SRCS "src/server.cpp" "src/ota.cpp" "src/http_json.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server esp_http_client mbedtls esp_netif cjson wifi_manager device_manager led app_update esp_https_ota efuse
)

if(CONFIG_SERVER_HTTPS)
    target_add_binary_data(${COMPONENT_LIB} "cert/cert.crt" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/private.key" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/ca.crt" EMBED_FILES)
else()

endif()
