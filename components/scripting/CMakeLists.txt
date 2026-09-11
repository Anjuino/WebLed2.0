idf_component_register(
    SRCS "src/wrench.cpp" "src/script_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES led freertos esp_http_server cjson
)
