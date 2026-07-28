#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_state.h"
#include "ble_manager.h"
#include "buttons.h"
#include "config.h"
#include "gopro_ble.h"
#include "heart_rate_ble.h"
#include "oled_display.h"
#include "racebox_ble.h"
#include "status_led.h"
#include "version.h"

namespace {

constexpr char kTag[] = "main";

void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void init_racebox_button() {
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = 1ULL << kRaceBoxButtonPin;
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&btn_cfg);
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "%s v%s starting", FIRMWARE_NAME, FIRMWARE_VERSION);

    init_nvs();
    init_racebox_button();
    buttons_init();     // GoPro's GPIO2/3/4/18, ISR-driven, own task
    app_state_init();   // GoPro's shared conn/recording/mode state

    status_led::init(kRgbLedPin, kRgbLedCount);
    oled_display::init(kOledSdaPin, kOledSclPin, kOledI2cAddr);

    // Starts the shared NimBLE host + scan; racebox_ble, gopro_ble and
    // heart_rate_ble take over their own connection's GAP events once the
    // scan finds them (see ble_manager.cpp).
    ble_manager_init();

    bool last_pressed = false;
    int64_t last_change_us = 0;

    for (;;) {
        status_led::set_racebox_state(racebox_ble::state());
        status_led::set_gopro_state(app_state_get_conn(), app_state_get_recording());
        status_led::tick();

        uint16_t bpm = 0;
        const bool hr_valid = heart_rate_ble_get_bpm(&bpm);
        oled_display::set_heart_rate(hr_valid, bpm);
        oled_display::tick();

        const bool pressed = gpio_get_level(static_cast<gpio_num_t>(kRaceBoxButtonPin)) == 0;
        const int64_t now = esp_timer_get_time();
        if (pressed && !last_pressed && (now - last_change_us) > kRaceBoxButtonDebounceUs) {
            last_change_us = now;
            racebox_ble::toggle_recording();
        }
        last_pressed = pressed;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
