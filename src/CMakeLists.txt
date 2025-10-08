idf_component_register(
    SRCS "main.cpp"
         "modules/i2s_handler.cpp"
         "modules/network_manager.cpp"
         "modules/tcp_streamer.cpp"
         "modules/buffer_manager.cpp"
         "modules/config_manager.cpp"
         "modules/web_server.cpp"
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
        esp_http_server
        json
)

# Embed web UI files
target_add_binary_data(${COMPONENT_LIB} "../data/index.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/config.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/monitor.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/css/style.css" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/js/api.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/js/utils.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/js/app.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/js/config.js" TEXT)
target_add_binary_data(${COMPONENT_LIB} "../data/js/monitor.js" TEXT)

