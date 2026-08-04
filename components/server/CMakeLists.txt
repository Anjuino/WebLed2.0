idf_component_register(
    SRCS "src/https_server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server mbedtls esp_netif esp_https_server memory_manager cjson wifi_manager
)

target_add_binary_data(${COMPONENT_LIB} "cert/cert.crt" EMBED_FILES)
target_add_binary_data(${COMPONENT_LIB} "cert/private.key" EMBED_FILES)
target_add_binary_data(${COMPONENT_LIB} "cert/ca.crt" EMBED_FILES)