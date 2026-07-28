#include "status_led.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "led_strip.h"

#include "config.h"

namespace {

constexpr int64_t kSlowBlinkUs = 500 * 1000;
constexpr int64_t kFastBlinkUs = 150 * 1000;
constexpr int64_t kGoProBlinkUs = 400 * 1000; // matches the original single-LED GoPro firmware's toggle rate
constexpr uint8_t kBrightness = 40;           // keep the small LED bar comfortable to look at

led_strip_handle_t g_strip = nullptr;

// ---- pixel 0: RaceBox ----------------------------------------------------
RaceBoxState g_racebox_state = RaceBoxState::SCANNING;
int64_t g_racebox_last_toggle_us = 0;
bool g_racebox_blink_on = true;

// ---- pixel 1: GoPro --------------------------------------------------------
conn_state_t g_gopro_conn = CONN_STATE_SCANNING;
bool g_gopro_recording = false;
int64_t g_gopro_last_toggle_us = 0;
bool g_gopro_blink_on = true;

void advance_blink(int64_t blink_period_us, int64_t now, int64_t* last_toggle_us, bool* blink_on) {
    if (blink_period_us == 0) {
        *blink_on = true;
    } else if (now - *last_toggle_us >= blink_period_us) {
        *last_toggle_us = now;
        *blink_on = !*blink_on;
    }
}

void racebox_color(int64_t now, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = *g = *b = 0;

    int64_t blink_period_us = 0;
    switch (g_racebox_state) {
        case RaceBoxState::SCANNING:
        case RaceBoxState::RECORDING_PAUSED:
            blink_period_us = kSlowBlinkUs;
            break;
        case RaceBoxState::CONNECTING:
        case RaceBoxState::MEMORY_LOCKED:
            blink_period_us = kFastBlinkUs;
            break;
        case RaceBoxState::CONNECTED_IDLE:
        case RaceBoxState::RECORDING:
            blink_period_us = 0; // solid
            break;
    }
    advance_blink(blink_period_us, now, &g_racebox_last_toggle_us, &g_racebox_blink_on);

    if (!g_racebox_blink_on) {
        return;
    }

    switch (g_racebox_state) {
        case RaceBoxState::SCANNING:
            *b = kBrightness; // blue, slow blink
            break;
        case RaceBoxState::CONNECTING:
            *g = kBrightness; // cyan, fast blink
            *b = kBrightness;
            break;
        case RaceBoxState::CONNECTED_IDLE:
            *g = kBrightness; // green, solid
            break;
        case RaceBoxState::RECORDING:
            *r = kBrightness; // red, solid
            break;
        case RaceBoxState::RECORDING_PAUSED:
            *r = kBrightness; // orange, slow blink
            *g = kBrightness / 2;
            break;
        case RaceBoxState::MEMORY_LOCKED:
            *r = kBrightness; // purple, fast blink
            *b = kBrightness;
            break;
    }
}

void gopro_color(int64_t now, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = *g = *b = 0;

    const bool blinking = g_gopro_recording || g_gopro_conn == CONN_STATE_SCANNING;
    advance_blink(blinking ? kGoProBlinkUs : 0, now, &g_gopro_last_toggle_us, &g_gopro_blink_on);

    if (!g_gopro_blink_on) {
        return;
    }

    if (g_gopro_recording) {
        *r = kBrightness; // red, blinking - overrides the connection-state color
        return;
    }

    switch (g_gopro_conn) {
        case CONN_STATE_SCANNING:
            *b = kBrightness; // blue, blinking
            break;
        case CONN_STATE_CONNECTING:
            *r = kBrightness; // solid yellow-ish (discovery/bonding in progress)
            *g = kBrightness * 3 / 4;
            break;
        case CONN_STATE_READY:
            *g = kBrightness; // green, solid
            break;
    }
}

} // namespace

void status_led::init(int gpio_num, int led_count) {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio_num;
    strip_config.max_leds = led_count;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10 MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip));
    led_strip_clear(g_strip);
}

void status_led::set_racebox_state(RaceBoxState state) {
    if (state != g_racebox_state) {
        g_racebox_state = state;
        g_racebox_blink_on = true;
        g_racebox_last_toggle_us = esp_timer_get_time();
    }
}

void status_led::set_gopro_state(conn_state_t conn, bool recording) {
    if (conn != g_gopro_conn || recording != g_gopro_recording) {
        g_gopro_conn = conn;
        g_gopro_recording = recording;
        g_gopro_blink_on = true;
        g_gopro_last_toggle_us = esp_timer_get_time();
    }
}

void status_led::tick() {
    if (g_strip == nullptr) {
        return;
    }

    const int64_t now = esp_timer_get_time();

    uint8_t r, g, b;
    racebox_color(now, &r, &g, &b);
    led_strip_set_pixel(g_strip, kRaceBoxLedIndex, r, g, b);

    gopro_color(now, &r, &g, &b);
    led_strip_set_pixel(g_strip, kGoProLedIndex, r, g, b);

    led_strip_refresh(g_strip);
}
