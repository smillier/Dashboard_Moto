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

// 3-pixel WS2812 addressable RGB bar, data line on GPIO8 (the SuperMini's
// onboard WS2812 pad). Pixel 0 reflects RaceBoxState, pixel 1 reflects the
// GoPro connection/recording state, pixel 2 reflects the heart rate
// monitor's BLE connection state.
inline constexpr int kRgbLedPin = 8;
inline constexpr int kRgbLedCount = 3;
inline constexpr int kRaceBoxLedIndex = 0;
inline constexpr int kGoProLedIndex = 1;
inline constexpr int kHeartRateLedIndex = 2;

// ---------------------------------------------------------------------
// Heart rate OLED
// ---------------------------------------------------------------------

// 0.96" SSD1306 128x64 I2C OLED. Wired to this board's silkscreen-labeled
// I2C pins (SDA/SCL), which are otherwise unused - no IO expander needed,
// I2C only takes 2 GPIOs.
inline constexpr int kOledSdaPin = 20;
inline constexpr int kOledSclPin = 19;
// Default address for this module; some clones ship at 0x3D instead - check
// yours if the display stays blank.
inline constexpr uint8_t kOledI2cAddr = 0x3C;
inline constexpr uint32_t kOledI2cFreqHz = 400000;
