#include "oled_display.h"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr char kTag[] = "oled_display";

constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr int kPages = kHeight / 8; // SSD1306 page addressing: 8 vertical px/page

// Also doubles as the blink-phase sampling rate for the connection-status
// dots (see IconState), so a display that missed an update (e.g. power-on
// before the first heart rate reading) never stays stale forever, and
// blinking dots stay reasonably smooth without redrawing on every
// main-loop tick.
constexpr int64_t kMinRefreshIntervalUs = 100 * 1000;

i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;
uint8_t g_framebuffer[kPages][kWidth];

bool g_last_valid = false;
uint16_t g_last_bpm = 0;
bool g_dirty = true;
int64_t g_last_refresh_us = 0;

// ---- top-strip connection-status icons -------------------------------
// Generic three-tier status, independent of each BLE module's own state
// type; set_racebox_state()/set_gopro_state()/set_heart_rate_connected()
// each map their argument down to this before storing it.
enum class IconState { SCANNING, CONNECTING, CONNECTED };

IconState g_racebox_icon = IconState::SCANNING;
IconState g_gopro_icon = IconState::SCANNING;
IconState g_hr_icon = IconState::SCANNING;

constexpr int64_t kSlowBlinkHalfPeriodUs = 500 * 1000;
constexpr int64_t kFastBlinkHalfPeriodUs = 150 * 1000;

bool icon_visible(IconState state, int64_t now) {
    switch (state) {
        case IconState::CONNECTED:
            return true;
        case IconState::CONNECTING:
            return (now / kFastBlinkHalfPeriodUs) % 2 == 0;
        case IconState::SCANNING:
        default:
            return (now / kSlowBlinkHalfPeriodUs) % 2 == 0;
    }
}

// ---- low-level I2C / SSD1306 command helpers ------------------------------

void write_cmds(const uint8_t* cmds, size_t count) {
    if (g_dev == nullptr) {
        return;
    }
    uint8_t buf[32];
    buf[0] = 0x00; // control byte: Co=0, D/C#=0 -> command stream
    memcpy(&buf[1], cmds, count);
    i2c_master_transmit(g_dev, buf, count + 1, 1000);
}

void write_cmd(uint8_t cmd) {
    write_cmds(&cmd, 1);
}

void write_page_data(uint8_t page, const uint8_t* data) {
    const uint8_t set_page_cmds[3] = {
        static_cast<uint8_t>(0xB0 + page), // set page start address
        0x00,                              // set lower column start address = 0
        0x10,                              // set higher column start address = 0
    };
    write_cmds(set_page_cmds, sizeof(set_page_cmds));

    uint8_t buf[kWidth + 1];
    buf[0] = 0x40; // control byte: Co=0, D/C#=1 -> data stream
    memcpy(&buf[1], data, kWidth);
    i2c_master_transmit(g_dev, buf, sizeof(buf), 1000);
}

void flush_framebuffer() {
    for (int page = 0; page < kPages; ++page) {
        write_page_data(static_cast<uint8_t>(page), g_framebuffer[page]);
    }
}

// ---- framebuffer drawing ---------------------------------------------------

void clear_framebuffer() {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

void fill_rect(int x, int y, int w, int h) {
    for (int py = y; py < y + h; ++py) {
        if (py < 0 || py >= kHeight) {
            continue;
        }
        const int page = py / 8;
        const uint8_t bit = static_cast<uint8_t>(1u << (py % 8));
        for (int px = x; px < x + w; ++px) {
            if (px < 0 || px >= kWidth) {
                continue;
            }
            g_framebuffer[page][px] |= bit;
        }
    }
}

// ---- 8x8 icon bitmaps -------------------------------------------------
// One byte per row, MSB = leftmost column. Small hand-drawn glyphs (no font
// library): a location dot for RaceBox (GPS lap timer), a camera for GoPro,
// a heart for the heart rate monitor.
constexpr uint8_t kIconRaceBox[8] = {0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18};
constexpr uint8_t kIconGoPro[8] = {0x3C, 0x7E, 0xFF, 0xBD, 0xA5, 0xBD, 0xFF, 0x00};
constexpr uint8_t kIconHeartRate[8] = {0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00};

constexpr int kIconSize = 8;
constexpr int kStatusDotSize = 4;
constexpr int kIconY = 1;
constexpr int kStatusDotY = kIconY + kIconSize + 2;

void draw_bitmap8(int x, int y, const uint8_t rows[8]) {
    for (int row = 0; row < kIconSize; ++row) {
        for (int col = 0; col < kIconSize; ++col) {
            if (rows[row] & (0x80 >> col)) {
                fill_rect(x + col, y + row, 1, 1);
            }
        }
    }
}

void draw_status_icon(int center_x, const uint8_t bitmap[8], IconState state, int64_t now) {
    draw_bitmap8(center_x - kIconSize / 2, kIconY, bitmap);
    if (icon_visible(state, now)) {
        fill_rect(center_x - kStatusDotSize / 2, kStatusDotY, kStatusDotSize, kStatusDotSize);
    }
}

// ---- 7-segment-style digit renderer ---------------------------------------
// Segment bits: a=0 (top), b=1 (top-right), c=2 (bottom-right), d=3 (bottom),
// e=4 (bottom-left), f=5 (top-left), g=6 (middle).
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

constexpr int kDigitW = 28;
constexpr int kDigitH = 48;
constexpr int kSegThickness = 6;
constexpr int kDigitGap = 6;

void draw_digit(int x, int y, uint8_t segments) {
    const int w = kDigitW;
    const int h = kDigitH;
    const int t = kSegThickness;
    const int half = h / 2;

    if (segments & kSegA) fill_rect(x + t, y, w - 2 * t, t);                  // top
    if (segments & kSegB) fill_rect(x + w - t, y + t, t, half - t);           // top-right
    if (segments & kSegC) fill_rect(x + w - t, y + half, t, half - t);        // bottom-right
    if (segments & kSegD) fill_rect(x + t, y + h - t, w - 2 * t, t);          // bottom
    if (segments & kSegE) fill_rect(x, y + half, t, half - t);                // bottom-left
    if (segments & kSegF) fill_rect(x, y + t, t, half - t);                   // top-left
    if (segments & kSegG) fill_rect(x + t, y + half - t / 2, w - 2 * t, t);   // middle
}

// Top strip is 16px tall (icon row + status dot row); the digit zone below
// it fills the remaining height exactly (kDigitH == kHeight - kDigitZoneY).
constexpr int kDigitZoneY = 16;

void render(bool valid, uint16_t bpm, int64_t now) {
    clear_framebuffer();

    draw_status_icon(24, kIconRaceBox, g_racebox_icon, now);
    draw_status_icon(64, kIconGoPro, g_gopro_icon, now);
    draw_status_icon(104, kIconHeartRate, g_hr_icon, now);

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

    for (size_t i = 0; i < count; ++i) {
        const char c = text[i];
        const uint8_t segments = (c == '-') ? kDashSegments : kDigitSegments[c - '0'];
        draw_digit(x, kDigitZoneY, segments);
        x += kDigitW + kDigitGap;
    }
}

} // namespace

void oled_display::init(int sda_gpio, int scl_gpio, uint8_t i2c_addr) {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = -1; // auto-select a free port
    bus_config.sda_io_num = static_cast<gpio_num_t>(sda_gpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(scl_gpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &g_bus));

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = i2c_addr;
    dev_config.scl_speed_hz = 400000;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_bus, &dev_config, &g_dev));

    static const uint8_t kInitCmds[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock divide ratio / oscillator frequency
        0xA8, 0x3F, // multiplex ratio = 64
        0xD3, 0x00, // display offset = 0
        0x40,       // display start line = 0
        0x8D, 0x14, // charge pump enable (module has no external VCC)
        0x20, 0x02, // memory addressing mode = page addressing
        0xA1,       // segment remap (column 127 -> SEG0)
        0xC8,       // COM output scan direction, remapped
        0xDA, 0x12, // COM pins hardware configuration
        0x81, 0xCF, // contrast control
        0xD9, 0xF1, // pre-charge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // entire display follows RAM content
        0xA6,       // normal display (not inverted)
        0xAF,       // display on
    };
    write_cmds(kInitCmds, sizeof(kInitCmds));

    clear_framebuffer();
    flush_framebuffer();

    ESP_LOGI(kTag, "SSD1306 OLED ready on SDA=%d SCL=%d addr=0x%02X", sda_gpio, scl_gpio, i2c_addr);
}

namespace {

IconState racebox_icon_state(RaceBoxState state) {
    switch (state) {
        case RaceBoxState::SCANNING:
            return IconState::SCANNING;
        case RaceBoxState::CONNECTING:
            return IconState::CONNECTING;
        case RaceBoxState::CONNECTED_IDLE:
        case RaceBoxState::RECORDING:
        case RaceBoxState::RECORDING_PAUSED:
        case RaceBoxState::MEMORY_LOCKED:
        default:
            return IconState::CONNECTED;
    }
}

IconState gopro_icon_state(conn_state_t conn) {
    switch (conn) {
        case CONN_STATE_SCANNING:
            return IconState::SCANNING;
        case CONN_STATE_CONNECTING:
            return IconState::CONNECTING;
        case CONN_STATE_READY:
        default:
            return IconState::CONNECTED;
    }
}

} // namespace

void oled_display::set_racebox_state(RaceBoxState state) {
    const IconState icon = racebox_icon_state(state);
    if (icon != g_racebox_icon) {
        g_racebox_icon = icon;
        g_dirty = true;
    }
}

void oled_display::set_gopro_state(conn_state_t conn) {
    const IconState icon = gopro_icon_state(conn);
    if (icon != g_gopro_icon) {
        g_gopro_icon = icon;
        g_dirty = true;
    }
}

void oled_display::set_heart_rate_connected(bool connected) {
    const IconState icon = connected ? IconState::CONNECTED : IconState::SCANNING;
    if (icon != g_hr_icon) {
        g_hr_icon = icon;
        g_dirty = true;
    }
}

void oled_display::set_heart_rate(bool valid, uint16_t bpm) {
    if (valid != g_last_valid || bpm != g_last_bpm) {
        g_last_valid = valid;
        g_last_bpm = bpm;
        g_dirty = true;
    }
}

void oled_display::tick() {
    if (g_dev == nullptr) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (!g_dirty && (now - g_last_refresh_us) < kMinRefreshIntervalUs) {
        return;
    }

    render(g_last_valid, g_last_bpm, now);
    flush_framebuffer();

    g_dirty = false;
    g_last_refresh_us = now;
}
