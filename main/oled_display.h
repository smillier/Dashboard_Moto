#pragma once

#include <cstdint>

// Drives a 0.96" SSD1306 128x64 I2C OLED showing the current heart rate
// reading as large digits (or "--" until a reading arrives). No external
// font/graphics library: digits are drawn as filled rectangles using a
// small self-contained 7-segment-style renderer, consistent with the rest
// of this project's direct-driver style.
namespace oled_display {

void init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);
void set_heart_rate(bool valid, uint16_t bpm);

// Call periodically (e.g. every main-loop iteration); only actually
// redraws/pushes over I2C when the displayed value changes or a periodic
// safety-refresh interval has elapsed, so it doesn't hammer the bus.
void tick();

} // namespace oled_display
