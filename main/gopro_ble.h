#ifndef GOPRO_BLE_H
#define GOPRO_BLE_H

#include <stdint.h>
#include <stdbool.h>

#include "host/ble_gap.h"
#include "host/ble_hs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GOPRO_PRESET_VIDEO,
    GOPRO_PRESET_PHOTO,
    GOPRO_PRESET_TIMELAPSE,
} gopro_preset_group_t;

/**
 * True if `fields` (parsed from a BLE_GAP_EVENT_DISC advertisement) looks
 * like a GoPro advertising the Control & Query service (0xFEA6).
 */
bool gopro_ble_matches(const struct ble_hs_adv_fields *fields);

/**
 * Connects to the device found by ble_manager's scan (already validated via
 * gopro_ble_matches()). Registers this module's own GAP event handler for
 * the resulting connection - GATT discovery, subscription, and (once the
 * link settles) bonding are all driven from there.
 */
void gopro_ble_connect(const ble_addr_t *addr);

/**
 * True once BLE_GAP_EVENT_CONNECT has completed successfully for this
 * device (regardless of whether discovery/bonding has finished yet). Used
 * by ble_manager to know whether to keep scanning for it.
 */
bool gopro_ble_is_connected(void);

/**
 * Sends the shutter command once connected, bonded and the
 * Control & Query characteristics have been discovered.
 * start = true  -> start recording / take photo
 * start = false -> stop recording
 */
void gopro_send_shutter(bool start);

/**
 * Puts the camera to sleep (Open GoPro command 0x05). The camera
 * disconnects itself right after. There is no separate "power on"
 * BLE command - a sleeping GoPro keeps advertising, and simply
 * connecting to it (which the shared scan will do automatically on
 * the next reconnect cycle) wakes it back up.
 */
void gopro_send_sleep(void);

/**
 * Switches the camera to a different preset group (Video / Photo /
 * Timelapse), i.e. what most people think of as "changing mode".
 */
void gopro_load_preset_group(gopro_preset_group_t group);

/** Returns true once we're connected, bonded, and ready to send commands. */
bool gopro_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif // GOPRO_BLE_H
