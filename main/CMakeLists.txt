idf_component_register(
    SRCS
        "main.cpp"
        "ble_manager.cpp"
        "status_led.cpp"
        "oled_display.cpp"
        "racebox_ble.cpp"
        "ubx_protocol.cpp"
        "gopro_ble.c"
        "gopro_control.c"
        "heart_rate_ble.c"
        "app_state.c"
        "buttons.c"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_driver_i2c esp_timer nvs_flash
)
