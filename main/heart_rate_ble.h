#ifndef HEART_RATE_BLE_H
#define HEART_RATE_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "host/ble_gap.h"
#include "host/ble_hs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * True if `fields` (parsed from a BLE_GAP_EVENT_DISC advertisement) looks
 * like a standard BLE Heart Rate Monitor (service 0x180D).
 */
bool heart_rate_ble_matches(const struct ble_hs_adv_fields *fields);

/**
 * Connects to the device found by ble_manager's scan (already validated via
 * heart_rate_ble_matches()). Registers this module's own GAP event handler
 * for the resulting connection: discovers the Heart Rate service,
 * subscribes to Heart Rate Measurement notifications. No bonding is
 * requested - the standard Heart Rate service doesn't require it.
 */
void heart_rate_ble_connect(const ble_addr_t *addr);

/**
 * True once BLE_GAP_EVENT_CONNECT has completed successfully for this
 * device. Used by ble_manager to know whether to keep scanning for it.
 */
bool heart_rate_ble_is_connected(void);

/**
 * Latest heart rate reading in beats per minute. Returns false (and leaves
 * *out_bpm unset) until the first valid Heart Rate Measurement notification
 * has been received since connecting, or after a disconnect.
 */
bool heart_rate_ble_get_bpm(uint16_t *out_bpm);

#ifdef __cplusplus
}
#endif

#endif // HEART_RATE_BLE_H
