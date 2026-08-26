idf_component_register(
    SRCS
        "led.cpp"
        "led_effects.cpp"
        "include/led_strip/src/led_strip_api.c"
        "include/led_strip/src/led_strip_rmt_dev.c"
        "include/led_strip/src/led_strip_rmt_encoder.c"
    INCLUDE_DIRS 
        "include"
        "include/led_strip/include"
        "include/led_strip/interface"
    REQUIRES 
        driver nvs_proxy
)