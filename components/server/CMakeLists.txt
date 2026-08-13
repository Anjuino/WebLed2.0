idf_component_register(
    SRCS "src/server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server mbedtls esp_netif cjson wifi_manager device_manager led
)

if(CONFIG_SERVER_HTTPS)
    target_add_binary_data(${COMPONENT_LIB} "cert/cert.crt" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/private.key" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/ca.crt" EMBED_FILES)
else()

endif()
