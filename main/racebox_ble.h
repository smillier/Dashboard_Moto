#pragma once

#include "host/ble_gap.h"
#include "host/ble_hs.h"

#include "racebox_state.h"

// RaceBox UBX protocol handling, built directly on ESP-IDF's NimBLE host (no
// Arduino/NimBLE-Arduino wrapper). The NimBLE host itself (nimble_port_init,
// ble_hs_cfg, the scan loop) is owned by ble_manager, which dispatches
// matching advertisements here to connect; this module then owns everything
// about that one connection (GATT discovery, subscription, UBX I/O,
// recording state).
namespace racebox_ble {

// True if `fields` (parsed from a BLE_GAP_EVENT_DISC advertisement) looks
// like a RaceBox advertising the configured name prefix.
bool matches(const struct ble_hs_adv_fields& fields);

// Connects to the device found by ble_manager's scan (already validated via
// matches()). Registers this module's own GAP event handler for the
// resulting connection, so all further events for it (connect complete,
// GATT traffic, disconnect) are routed here rather than through the shared
// scan handler.
void connect(uint8_t own_addr_type, const ble_addr_t& addr);

// True once BLE_GAP_EVENT_CONNECT has completed successfully for this
// device (regardless of whether GATT discovery has finished yet). Used by
// ble_manager to know whether to keep scanning for it.
bool is_connected();

// Current bridge state, used to drive the status LED. Safe to call from any
// task; the underlying state is only ever written from the NimBLE host task.
RaceBoxState state();

// Starts standalone recording if it is currently stopped, stops it
// otherwise. No-op until the current recording status is known.
void toggle_recording();

} // namespace racebox_ble
