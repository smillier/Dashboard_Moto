#include "lcd_display.h"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "lcd_display";

constexpr int kWidth = 128;
constexpr int kHeight = 128;

// The ST7735S's actual GRAM is 132x162; Waveshare's own LCD_1in44.c offsets
// CASET/RASET by (x=2, y=1) to center the 128x128 visible area in it (their
// "vertical"/default orientation - MADCTL 0x00). If the image ever looks
// shifted/mirrored on a clone panel, this offset or MADCTL is the first
// thing to check - same spirit as the OLED driver's I2C-address caveat.
constexpr int kColOffset = 2;
constexpr int kRowOffset = 1;

// Safety refresh interval even if nothing changed, so a display that
// missed an update (e.g. power-on before the first heart rate reading)
// never stays stale forever.
constexpr int64_t kMinRefreshIntervalUs = 250 * 1000;

// RGB565. Red on black - bold, legible, and thematically apt for a heart
// rate readout.
constexpr uint16_t kBgColor = 0x0000;
constexpr uint16_t kFgColor = 0xF800;

int g_dc_gpio = -1;
spi_device_handle_t g_spi = nullptr;

bool g_last_valid = false;
uint16_t g_last_bpm = 0;
bool g_dirty = true;
int64_t g_last_refresh_us = 0;

// ---- low-level SPI / ST7735S command helpers -------------------------------

void spi_write(const uint8_t* data, size_t len) {
    if (g_spi == nullptr || len == 0) {
        return;
    }
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(g_spi, &t));
}

void write_cmd(uint8_t cmd) {
    gpio_set_level(static_cast<gpio_num_t>(g_dc_gpio), 0);
    spi_write(&cmd, 1);
}

void write_data(const uint8_t* data, size_t len) {
    gpio_set_level(static_cast<gpio_num_t>(g_dc_gpio), 1);
    spi_write(data, len);
}

void write_data_u8(uint8_t v) {
    write_data(&v, 1);
}

void write_data_u16(uint16_t v) {
    const uint8_t buf[2] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFF)};
    write_data(buf, 2);
}

void set_window(int x0, int y0, int x1, int y1) {
    write_cmd(0x2A); // CASET
    write_data_u16(static_cast<uint16_t>(x0 + kColOffset));
    write_data_u16(static_cast<uint16_t>(x1 + kColOffset));
    write_cmd(0x2B); // RASET
    write_data_u16(static_cast<uint16_t>(y0 + kRowOffset));
    write_data_u16(static_cast<uint16_t>(y1 + kRowOffset));
    write_cmd(0x2C); // RAMWR
}

// ---- framebuffer-free drawing: set a window, then stream solid color ------

void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    set_window(x, y, x + w - 1, y + h - 1);

    static uint8_t chunk[512]; // 256 pixels/chunk
    for (size_t i = 0; i < sizeof(chunk); i += 2) {
        chunk[i] = static_cast<uint8_t>(color >> 8);
        chunk[i + 1] = static_cast<uint8_t>(color & 0xFF);
    }

    gpio_set_level(static_cast<gpio_num_t>(g_dc_gpio), 1);
    size_t total_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
    while (total_bytes > 0) {
        size_t n = total_bytes < sizeof(chunk) ? total_bytes : sizeof(chunk);
        spi_write(chunk, n);
        total_bytes -= n;
    }
}

void clear(uint16_t color) {
    fill_rect(0, 0, kWidth, kHeight, color);
}

// ---- 7-segment-style digit renderer ----------------------------------------
// Segment bits: a=0 (top), b=1 (top-right), c=2 (bottom-right), d=3 (bottom),
// e=4 (bottom-left), f=5 (top-left), g=6 (middle). Same table as the OLED
// driver this replaces, just bigger to fill the taller square panel.
constexpr uint8_t kSegA = 0x01, kSegB = 0x02, kSegC = 0x04, kSegD = 0x08, kSegE = 0x10, kSegF = 0x20, kSegG = 0x40;

constexpr uint8_t kDigitSegments[10] = {
    kSegA | kSegB | kSegC | kSegD | kSegE | kSegF,          // 0
    kSegB | kSegC,                                          // 1
    kSegA | kSegB | kSegG | kSegE | kSegD,                  // 2
    kSegA | kSegB | kSegG | kSegC | kSegD,                  // 3
    kSegF | kSegG | kSegB | kSegC,                          // 4
    kSegA | kSegF | kSegG | kSegC | kSegD,                  // 5
    kSegA | kSegF | kSegG | kSegE | kSegC | kSegD,          // 6
    kSegA | kSegB | kSegC,                                  // 7
    kSegA | kSegB | kSegC | kSegD | kSegE | kSegF | kSegG,  // 8
    kSegA | kSegB | kSegC | kSegD | kSegF | kSegG,          // 9
};
constexpr uint8_t kDashSegments = kSegG;

constexpr int kDigitW = 32;
constexpr int kDigitH = 88;
constexpr int kSegThickness = 10;
constexpr int kDigitGap = 8;

void draw_digit(int x, int y, uint8_t segments) {
    const int w = kDigitW;
    const int h = kDigitH;
    const int t = kSegThickness;
    const int half = h / 2;

    if (segments & kSegA) fill_rect(x + t, y, w - 2 * t, t, kFgColor);                  // top
    if (segments & kSegB) fill_rect(x + w - t, y + t, t, half - t, kFgColor);           // top-right
    if (segments & kSegC) fill_rect(x + w - t, y + half, t, half - t, kFgColor);        // bottom-right
    if (segments & kSegD) fill_rect(x + t, y + h - t, w - 2 * t, t, kFgColor);          // bottom
    if (segments & kSegE) fill_rect(x, y + half, t, half - t, kFgColor);                // bottom-left
    if (segments & kSegF) fill_rect(x, y + t, t, half - t, kFgColor);                   // top-left
    if (segments & kSegG) fill_rect(x + t, y + half - t / 2, w - 2 * t, t, kFgColor);   // middle
}

void render(bool valid, uint16_t bpm) {
    clear(kBgColor);

    char text[8];
    if (valid && bpm <= 999) {
        snprintf(text, sizeof(text), "%u", static_cast<unsigned>(bpm));
    } else {
        // No reading yet (or an implausible value from a malformed
        // notification) - show dashes rather than a misleading number.
        snprintf(text, sizeof(text), "--");
    }

    const size_t count = strlen(text);
    const int total_w = static_cast<int>(count) * kDigitW + static_cast<int>(count - 1) * kDigitGap;
    int x = (kWidth - total_w) / 2;
    const int y = (kHeight - kDigitH) / 2;

    for (size_t i = 0; i < count; ++i) {
        const char c = text[i];
        const uint8_t segments = (c == '-') ? kDashSegments : kDigitSegments[c - '0'];
        draw_digit(x, y, segments);
        x += kDigitW + kDigitGap;
    }
}

// ---- ST7735S register init sequence ----------------------------------------
// Ported as-is from Waveshare's LCD_1in44.c (LCD_1IN44_InitReg /
// LCD_1IN44_SetAttributes, vertical/default orientation) - this is the
// exact panel used on the 1.44" LCD HAT, just via a Pi header instead of
// this project's hand-wired GPIOs.
void init_registers() {
    write_cmd(0x11); // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    write_cmd(0x3A); // pixel format
    write_data_u8(0x05); // 16 bpp (RGB565)

    write_cmd(0xB1); // frame rate ctrl - normal mode
    write_data_u8(0x01);
    write_data_u8(0x2C);
    write_data_u8(0x2D);

    write_cmd(0xB2); // frame rate ctrl - idle mode
    write_data_u8(0x01);
    write_data_u8(0x2C);
    write_data_u8(0x2D);

    write_cmd(0xB3); // frame rate ctrl - partial mode
    write_data_u8(0x01);
    write_data_u8(0x2C);
    write_data_u8(0x2D);
    write_data_u8(0x01);
    write_data_u8(0x2C);
    write_data_u8(0x2D);

    write_cmd(0xB4); // column inversion
    write_data_u8(0x07);

    write_cmd(0xC0); // power ctrl 1
    write_data_u8(0xA2);
    write_data_u8(0x02);
    write_data_u8(0x84);

    write_cmd(0xC1); // power ctrl 2
    write_data_u8(0xC5);

    write_cmd(0xC2); // power ctrl 3
    write_data_u8(0x0A);
    write_data_u8(0x00);

    write_cmd(0xC3); // power ctrl 4
    write_data_u8(0x8A);
    write_data_u8(0x2A);

    write_cmd(0xC4); // power ctrl 5
    write_data_u8(0x8A);
    write_data_u8(0xEE);

    write_cmd(0xC5); // VCOM
    write_data_u8(0x0E);

    write_cmd(0x36); // MADCTL: vertical orientation, no mirror/swap
    write_data_u8(0x00);

    write_cmd(0xE0); // gamma+
    static const uint8_t kGammaPos[] = {0x0f, 0x1a, 0x0f, 0x18, 0x2f, 0x28, 0x20, 0x22,
                                         0x1f, 0x1b, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10};
    write_data(kGammaPos, sizeof(kGammaPos));

    write_cmd(0xE1); // gamma-
    static const uint8_t kGammaNeg[] = {0x0f, 0x1b, 0x0f, 0x17, 0x33, 0x2c, 0x29, 0x2e,
                                         0x30, 0x30, 0x39, 0x3f, 0x00, 0x07, 0x03, 0x10};
    write_data(kGammaNeg, sizeof(kGammaNeg));

    write_cmd(0x21); // display inversion on (matches the reference panel's "reverse")
    write_cmd(0x29); // display on
}

} // namespace

void lcd_display::init(int sclk_gpio, int mosi_gpio, int cs_gpio, int dc_gpio, int rst_gpio,
                        int bl_gpio, int clock_hz) {
    g_dc_gpio = dc_gpio;

    gpio_config_t out_cfg = {};
    out_cfg.pin_bit_mask = (1ULL << dc_gpio) | (1ULL << rst_gpio) | (1ULL << bl_gpio);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    gpio_set_level(static_cast<gpio_num_t>(bl_gpio), 1); // backlight on (no PWM dimming yet)

    // Hardware reset, matching Waveshare's own timing.
    gpio_set_level(static_cast<gpio_num_t>(rst_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(static_cast<gpio_num_t>(rst_gpio), 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(static_cast<gpio_num_t>(rst_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = sclk_gpio;
    bus_cfg.mosi_io_num = mosi_gpio;
    bus_cfg.miso_io_num = -1; // display has no readback line
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 512;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode = 0; // CPOL=0, CPHA=0, per the panel's datasheet
    dev_cfg.clock_speed_hz = clock_hz;
    dev_cfg.spics_io_num = cs_gpio;
    dev_cfg.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &g_spi));

    init_registers();
    clear(kBgColor);

    ESP_LOGI(kTag, "ST7735S LCD ready: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d @ %d Hz",
             sclk_gpio, mosi_gpio, cs_gpio, dc_gpio, rst_gpio, bl_gpio, clock_hz);
}

void lcd_display::set_heart_rate(bool valid, uint16_t bpm) {
    if (valid != g_last_valid || bpm != g_last_bpm) {
        g_last_valid = valid;
        g_last_bpm = bpm;
        g_dirty = true;
    }
}

void lcd_display::tick() {
    if (g_spi == nullptr) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (!g_dirty && (now - g_last_refresh_us) < kMinRefreshIntervalUs) {
        return;
    }

    render(g_last_valid, g_last_bpm);

    g_dirty = false;
    g_last_refresh_us = now;
}
