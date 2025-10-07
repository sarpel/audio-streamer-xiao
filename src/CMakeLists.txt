idf_component_register(
    SRCS "main.cpp"
         "modules/i2s_handler.cpp"
         "modules/network_manager.cpp"
         "modules/tcp_streamer.cpp"
         "modules/buffer_manager.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        esp_driver_i2s
        esp_wifi
        esp_event
        esp_netif
        esp_system
        esp_timer
        nvs_flash
        lwip
        esp_hw_support
        freertos
)
