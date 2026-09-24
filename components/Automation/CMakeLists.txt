idf_component_register(
    SRCS "src/automation.cpp" "src/i_button.cpp" "src/i_light_sensor.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver cjson button light_sensor
    PRIV_REQUIRES device
)
