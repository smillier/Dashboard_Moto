#include "ble_manager.h"

#include "esp_err.h"
#include "esp_log.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "gopro_ble.h"
#include "heart_rate_ble.h"
#include "racebox_ble.h"

// Declared by the NimBLE "store/config" component; provides persistent
// (NVS-backed) storage for the GoPro's bonding keys. No public header is
// shipped for it in every IDF version, so it's forward-declared here
// exactly like ESP-IDF's own blecent/bleprph examples do.
extern "C" void ble_store_config_init(void);

namespace {

constexpr char kTag[] = "ble_manager";

uint8_t g_own_addr_type = 0;

// Shared scan handler: only cares about BLE_GAP_EVENT_DISC. Whichever
// protocol module recognizes the advertisement takes over the resulting
// connection's GAP events directly (registered at ble_gap_connect() time
// inside racebox_ble::connect() / gopro_ble_connect()).
int scan_event_handler(struct ble_gap_event* event, void* arg) {
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return 0;
    }

    if (!racebox_ble::is_connected() && racebox_ble::matches(fields)) {
        ble_gap_disc_cancel();
        racebox_ble::connect(g_own_addr_type, event->disc.addr);
        return 0;
    }

    if (!gopro_ble_is_connected() && gopro_ble_matches(&fields)) {
        ble_gap_disc_cancel();
        gopro_ble_connect(&event->disc.addr);
        return 0;
    }

    if (!heart_rate_ble_is_connected() && heart_rate_ble_matches(&fields)) {
        ble_gap_disc_cancel();
        heart_rate_ble_connect(&event->disc.addr);
        return 0;
    }

    return 0;
}

void start_scan() {
    if (racebox_ble::is_connected() && gopro_ble_is_connected() && heart_rate_ble_is_connected()) {
        return; // all three peers already connected, nothing left to look for
    }

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive = 0;
    disc_params.filter_duplicates = 1;

    const int rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &disc_params, scan_event_handler, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(kTag, "ble_gap_disc failed: %d", rc);
    }
}

void on_reset(int reason) {
    ESP_LOGW(kTag, "NimBLE host reset, reason=%d", reason);
}

void on_sync() {
    ble_hs_id_infer_auto(0, &g_own_addr_type);
    start_scan();
}

void host_task(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

} // namespace

void ble_manager_init(void) {
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* --- Security parameters: Just Works bonding, Legacy Pairing ---
     * Only the GoPro connection ever calls ble_gap_security_initiate()
     * (see gopro_ble.c); the RaceBox connection never requests security
     * and is unaffected by these being enabled - there is only one
     * ble_hs_cfg shared by both peers.
     *
     * CONFIRMED FIX for GoPro/ESP32-C6 pairing: use Legacy Pairing, not
     * LE Secure Connections. With sm_sc=1, the GoPro terminated the
     * connection (disconnect reason 531 / HCI 0x13) every time security
     * was requested, even after GATT discovery settled the link and
     * after narrowing key distribution to ENC only. Switching to Legacy
     * Pairing (sm_sc=0) fixed it - bonding now completes and
     * BLE_GAP_EVENT_ENC_CHANGE reports status=0. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    /* Persist bond info (LTK) to flash so we don't have to re-pair with
     * the GoPro after every reboot. */
    ble_store_config_init();

    /* Note: we deliberately do NOT call ble_svc_gap_init() / device_name_set()
     * or ble_svc_gatt_init() here. Those live in NimBLE's GAP/GATT
     * *services* (what a peripheral exposes to advertise itself), only
     * compiled in when the peripheral role is enabled. This firmware is
     * central-only for both peers (CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n),
     * so calling them would fail with "undefined reference". */

    nimble_port_freertos_init(host_task);
}

void ble_manager_resume_scan_if_needed(void) {
    start_scan();
}
