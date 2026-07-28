#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

// Owns the single NimBLE host (nimble_port_init, ble_hs_cfg, the FreeRTOS
// host task) and the single BLE scan loop shared by racebox_ble, gopro_ble
// and heart_rate_ble. Only one ble_gap_disc() can run at a time, so this
// module is the sole place that starts/stops scanning; it dispatches each
// matching advertisement to whichever protocol module recognizes it, which
// then takes over that connection's own GAP events directly (see
// racebox_ble.h / gopro_ble.h / heart_rate_ble.h).
//
// Plain C API (not a C++ namespace) so racebox_ble.cpp (C++) and
// gopro_ble.c / heart_rate_ble.c (C) can all call it uniformly.
#ifdef __cplusplus
extern "C" {
#endif

// Brings up the NimBLE host and, once it syncs, starts the shared scan.
// Call once from app_main(), after racebox_ble/gopro_ble/heart_rate_ble's
// own state is ready to receive events (they have no init of their own to
// call first).
void ble_manager_init(void);

// Re-starts the shared scan if at least one of RaceBox/GoPro/heart-rate
// monitor is not yet connected; a no-op if all three already are. Called by
// racebox_ble/gopro_ble/heart_rate_ble whenever a connection attempt fails
// or an existing connection drops.
void ble_manager_resume_scan_if_needed(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_MANAGER_H
