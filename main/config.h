#pragma once

#include <cstdint>

// ---------------------------------------------------------------------
// RaceBox (standalone recording bridge)
// ---------------------------------------------------------------------

// BLE advertised name prefix for the target device. RaceBox devices always
// advertise as "RaceBox Mini <serial>", "RaceBox Mini S <serial>" or
// "RaceBox Micro <serial>". Only Mini S and Micro support standalone
// recording, so this project targets Micro by default.
inline constexpr const char* kRaceBoxNamePrefix = "RaceBox Micro";

// Nordic-UART-style service used by RaceBox for its binary UBX protocol.
// (Kept here for reference; racebox_ble.cpp encodes these as NimBLE
// ble_uuid128_t constants since that's the form the NimBLE C API needs.)
inline constexpr const char* kUartServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
inline constexpr const char* kUartRxCharUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // write commands here
inline constexpr const char* kUartTxCharUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // subscribe for notifications

// Memory security code (see "Unlock Memory", message 0xFF 0x30, in the RaceBox
// BLE protocol doc). Only relevant if you enabled memory security on the
// device from the official RaceBox app. Leave at 0 if you never set one -
// unlocking is a harmless no-op on devices without security enabled.
inline constexpr uint32_t kMemoryUnlockCode = 0;

// Standalone recording configuration sent when the button starts a recording
// (message 0xFF 0x25). Values follow the "Recommended setup" in the protocol
// doc: all filters enabled, 5 kph / 30 s stationary cutoff, 30 s no-fix
// cutoff, 5 minute auto-shutdown.
inline constexpr uint8_t kRecordingDataRate = 0; // 0 = 25 Hz
inline constexpr uint8_t kRecordingFlags = 0x1F; // wait-for-fix, stationary, no-fix, auto-shutdown, wait-for-data
inline constexpr uint16_t kStationarySpeedThresholdMmS = 1389; // ~5 kph
inline constexpr uint16_t kStationaryIntervalSec = 30;
inline constexpr uint16_t kNoFixIntervalSec = 30;
inline constexpr uint16_t kAutoShutdownIntervalSec = 300;

// RaceBox recording toggle button (ESP32-C6 SuperMini onboard BOOT button).
inline constexpr int kRaceBoxButtonPin = 9; // active LOW
inline constexpr int64_t kRaceBoxButtonDebounceUs = 250 * 1000;

// ---------------------------------------------------------------------
// Status LED bar
// ---------------------------------------------------------------------

// 10-pixel WS2812 addressable RGB bar, data line on GPIO8. Only the first 3
// pixels are meaningful (0 = RaceBoxState, 1 = GoPro connection/recording
// state, 2 = heart rate monitor's BLE connection state); status_led::tick()
// explicitly blanks the remaining ones each frame so they don't show
// stale/garbage data from being left unaddressed downstream in the chain.
inline constexpr int kRgbLedPin = 8;
inline constexpr int kRgbLedCount = 10;
inline constexpr int kRaceBoxLedIndex = 0;
inline constexpr int kGoProLedIndex = 1;
inline constexpr int kHeartRateLedIndex = 2;

// ---------------------------------------------------------------------
// Heart rate LCD + joystick/buttons (Waveshare 1.44" LCD HAT, ST7735S)
// ---------------------------------------------------------------------

// This is a Raspberry Pi HAT (2x20 header), hand-wired here rather than
// plugged in - see README for the full pinout table. Replaces the SSD1306
// heart-rate OLED and the GPIO18 physical reset button.
//
// All 14 signals below were chosen from the SuperMini's free GPIOs that are
// *not* strapping pins (strapping pins on this chip: GPIO4/5/8/9/15), so
// none of this wiring can glitch boot mode selection even if a button is
// held down at power-on.

// -- Display (4-wire SPI, 128x128) --
inline constexpr int kLcdSclkPin = 18;
inline constexpr int kLcdMosiPin = 19;
inline constexpr int kLcdCsPin = 20;
inline constexpr int kLcdDcPin = 21;
inline constexpr int kLcdRstPin = 22;
inline constexpr int kLcdBlPin = 23; // backlight, driven HIGH (no PWM dimming yet)
inline constexpr int kLcdSpiClockHz = 10 * 1000 * 1000; // matches Waveshare's own reference driver

// -- 5-way joystick --
inline constexpr int kJoyUpPin = 0;
inline constexpr int kJoyDownPin = 1;
inline constexpr int kJoyLeftPin = 6;
inline constexpr int kJoyRightPin = 7;
inline constexpr int kJoyPressPin = 14;

// -- KEY1/2/3 --
// KEY1 replaces the old GPIO18 physical reset button. KEY2/KEY3 and the
// joystick directions are wired and debounced but not bound to any action
// yet - reserved for a future on-screen menu.
inline constexpr int kHatKey1Pin = 16; // -> device reset
inline constexpr int kHatKey2Pin = 17; // reserved
inline constexpr int kHatKey3Pin = 3;  // reserved
