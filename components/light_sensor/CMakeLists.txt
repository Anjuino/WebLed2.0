idf_component_register(
    SRCS "src/light_sensor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc
)
