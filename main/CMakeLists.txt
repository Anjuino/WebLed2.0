idf_component_register(
    SRCS "main.cpp" 
    REQUIRES 
        server 
        led 
        device_manager 
        nvs_proxy 
        wifi_manager        
        nvs_flash 
        esp_timer
        mdns           
        device)
