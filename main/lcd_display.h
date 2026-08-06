#pragma once

#include <cstdint>

// Drives the display half of a Waveshare 1.44" LCD HAT (ST7735S, 128x128,
// 4-wire SPI) showing the current heart rate reading as large digits (or
// "--" until a reading arrives). Same direct-driver philosophy as the
// SSD1306 OLED this replaces: no external graphics library, digits are
// drawn as filled rectangles via a small self-contained 7-segment-style
// renderer. The register init sequence and the panel's pixel offset are
// ported from Waveshare's own reference driver (LCD_1in44.c) rather than
// guessed, since this is a real hardware bring-up.
namespace lcd_display {

void init(int sclk_gpio, int mosi_gpio, int cs_gpio, int dc_gpio, int rst_gpio, int bl_gpio,
          int clock_hz);
void set_heart_rate(bool valid, uint16_t bpm);

// Call periodically (e.g. every main-loop iteration); only actually
// redraws/pushes over SPI when the displayed value changes or a periodic
// safety-refresh interval has elapsed, so it doesn't hammer the bus.
void tick();

} // namespace lcd_display
