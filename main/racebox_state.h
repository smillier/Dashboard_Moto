#pragma once

// Overall RaceBox bridge state, driven by BLE connection status and the
// RaceBox's own standalone-recording status/state-change messages.
// status_led maps each value to a color/blink pattern on pixel 0 of the
// status bar.
enum class RaceBoxState {
    SCANNING,         // looking for the RaceBox over BLE
    CONNECTING,       // link established, waiting for the first status reply
    CONNECTED_IDLE,   // connected, standalone recording is not running
    RECORDING,        // standalone recording is running
    RECORDING_PAUSED, // recording running but paused (stationary/no-fix filter)
    MEMORY_LOCKED,    // memory security enabled and unlock failed/rejected
};
