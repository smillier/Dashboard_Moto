#pragma once

#include <cstdint>

#include "app_state.h"
#include "racebox_state.h"

// Drives a 0.96" SSD1306 128x64 I2C OLED. Top strip: three small icons
// (RaceBox, GoPro, Heart Rate, left to right) each with a status dot below
// it - solid when connected, slow-blinking while scanning, fast-blinking
// while connecting - mirroring the blink semantics of the WS2812 status bar
// in status_led.cpp. Below that: the current heart rate reading as large
// digits (or "--" until a reading arrives). No external font/graphics
// library: everything is drawn as filled rectangles / small hardcoded
// bitmaps, consistent with the rest of this project's direct-driver style.
namespace oled_display {

void init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);

// Mirrors status_led's per-device setters (same state types) so main.cpp
// can feed both displays from the same state reads.
void set_racebox_state(RaceBoxState state);
void set_gopro_state(conn_state_t conn);
void set_heart_rate_connected(bool connected);

void set_heart_rate(bool valid, uint16_t bpm);

// Call periodically (e.g. every main-loop iteration); only actually
// redraws/pushes over I2C when the displayed value changes, an icon's blink
// phase changes, or a periodic safety-refresh interval has elapsed, so it
// doesn't hammer the bus.
void tick();

} // namespace oled_display
