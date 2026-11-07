idf_component_register(
    SRCS "src/sntp_time.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
