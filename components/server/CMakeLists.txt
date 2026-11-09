idf_component_register(
    SRCS "src/server.cpp" "src/ota.cpp" "src/helper_server.cpp" "src/auth.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server esp_http_client mbedtls esp_netif cjson wifi_manager device_manager led app_update esp_https_ota efuse scripting filesystem sntp_time device mdns nvs_proxy
)

if(CONFIG_SERVER_HTTPS)
    target_add_binary_data(${COMPONENT_LIB} "cert/cert.crt" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/private.key" EMBED_FILES)
    target_add_binary_data(${COMPONENT_LIB} "cert/ca.crt" EMBED_FILES)
endif()

# Remote OTA always talks to the update server over HTTPS, so its CA is
# embedded regardless of the local web-server protocol (HTTP or HTTPS).
target_add_binary_data(${COMPONENT_LIB} "cert/remote_ca.crt" EMBED_TXTFILES)
idf_build_get_property(python PYTHON)
set(WEB_PAGE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
set(WEB_PAGE_GZ  "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
add_custom_command(
    OUTPUT "${WEB_PAGE_GZ}"
    COMMAND ${python} "${CMAKE_CURRENT_SOURCE_DIR}/web/gzip_page.py" "${WEB_PAGE_SRC}" "${WEB_PAGE_GZ}"
    DEPENDS "${WEB_PAGE_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/web/gzip_page.py"
    COMMENT "Compressing web page (gzip)"
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${WEB_PAGE_GZ}" BINARY DEPENDS "${WEB_PAGE_GZ}")
