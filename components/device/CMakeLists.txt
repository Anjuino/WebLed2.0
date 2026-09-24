idf_component_register(
    SRCS "src/device.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_proxy server sntp_time cjson device_manager led scripting filesystem button light_sensor Automation
)
