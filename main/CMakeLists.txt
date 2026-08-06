idf_component_register(
    SRCS
        "main.cpp"
        "ble_manager.cpp"
        "status_led.cpp"
        "lcd_display.cpp"
        "racebox_ble.cpp"
        "ubx_protocol.cpp"
        "gopro_ble.c"
        "gopro_control.c"
        "heart_rate_ble.c"
        "app_state.c"
        "buttons.c"
        "hat_input.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_timer nvs_flash
)
