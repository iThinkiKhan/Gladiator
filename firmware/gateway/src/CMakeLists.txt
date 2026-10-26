idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." "../../shared"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server esp_http_client app_update nvs_flash
    esp_driver_uart esp_driver_gpio esp_driver_usb_serial_jtag esp_timer espressif__cjson mbedtls lwip)
