#include "racebox_ble.h"

#include <cstring>

#include "esp_err.h"
#include "esp_log.h"

#include "host/ble_hs.h"
#include "host/util/util.h"

#include "ble_manager.h"
#include "config.h"
#include "ubx_protocol.h"

namespace {

constexpr char kTag[] = "racebox_ble";

// UUIDs as required by BLE_UUID128_INIT: reverse byte order of the standard
// "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"-style textual (big-endian) UUID.
const ble_uuid128_t kUartServiceUuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
const ble_uuid128_t kUartRxCharUuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
const ble_uuid128_t kUartTxCharUuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
const ble_uuid16_t kCccdUuid = BLE_UUID16_INIT(0x2902);

const size_t kNamePrefixLen = std::strlen(kRaceBoxNamePrefix);

uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t g_svc_start_handle = 0;
uint16_t g_svc_end_handle = 0;
uint16_t g_rx_val_handle = 0;
uint16_t g_tx_val_handle = 0;
uint16_t g_tx_cccd_handle = 0;

RaceBoxState g_state = RaceBoxState::SCANNING;
bool g_memory_secured = false;
bool g_memory_unlocked = true;
bool g_unlock_attempted = false;
bool g_status_known = false;
uint8_t g_recording_state = 0; // 0 = idle, 1 = recording, 2 = paused

void handle_ubx_message(uint8_t msg_class, uint8_t msg_id, const uint8_t* payload, uint16_t payload_len);
ubx::Parser g_parser(handle_ubx_message);

int gap_event_handler(struct ble_gap_event* event, void* arg);

void reset_connection_state() {
    g_svc_start_handle = 0;
    g_svc_end_handle = 0;
    g_rx_val_handle = 0;
    g_tx_val_handle = 0;
    g_tx_cccd_handle = 0;
    g_status_known = false;
    g_memory_secured = false;
    g_memory_unlocked = true;
    g_unlock_attempted = false;
    g_recording_state = 0;
}

void update_display_state() {
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        g_state = RaceBoxState::SCANNING;
        return;
    }
    if (g_memory_secured && !g_memory_unlocked) {
        g_state = RaceBoxState::MEMORY_LOCKED;
        return;
    }
    if (!g_status_known) {
        g_state = RaceBoxState::CONNECTING;
        return;
    }
    switch (g_recording_state) {
        case 1:
            g_state = RaceBoxState::RECORDING;
            break;
        case 2:
            g_state = RaceBoxState::RECORDING_PAUSED;
            break;
        default:
            g_state = RaceBoxState::CONNECTED_IDLE;
            break;
    }
}

bool send_raw(uint8_t msg_class, uint8_t msg_id, const uint8_t* payload, uint16_t payload_len) {
    if (g_rx_val_handle == 0) {
        return false;
    }
    uint8_t buf[32];
    const size_t len = ubx::buildPacket(msg_class, msg_id, payload, payload_len, buf, sizeof(buf));
    if (len == 0) {
        return false;
    }
    // RaceBox mirrors the Nordic UART Service convention, where the RX
    // characteristic is written without response for throughput.
    return ble_gattc_write_no_rsp_flat(g_conn_handle, g_rx_val_handle, buf, len) == 0;
}

void request_recording_status() {
    send_raw(ubx::CLASS_RACEBOX, ubx::ID_RECORDING_STATUS, nullptr, 0);
}

void send_unlock(uint32_t code) {
    const uint8_t payload[4] = {
        static_cast<uint8_t>(code & 0xFF),
        static_cast<uint8_t>((code >> 8) & 0xFF),
        static_cast<uint8_t>((code >> 16) & 0xFF),
        static_cast<uint8_t>((code >> 24) & 0xFF),
    };
    send_raw(ubx::CLASS_RACEBOX, ubx::ID_UNLOCK_MEMORY, payload, sizeof(payload));
}

void send_recording_config(bool enable) {
    uint8_t payload[12] = {};
    payload[0] = enable ? 1 : 0;
    payload[1] = kRecordingDataRate;
    payload[2] = kRecordingFlags;
    payload[3] = 0; // reserved
    payload[4] = kStationarySpeedThresholdMmS & 0xFF;
    payload[5] = (kStationarySpeedThresholdMmS >> 8) & 0xFF;
    payload[6] = kStationaryIntervalSec & 0xFF;
    payload[7] = (kStationaryIntervalSec >> 8) & 0xFF;
    payload[8] = kNoFixIntervalSec & 0xFF;
    payload[9] = (kNoFixIntervalSec >> 8) & 0xFF;
    payload[10] = kAutoShutdownIntervalSec & 0xFF;
    payload[11] = (kAutoShutdownIntervalSec >> 8) & 0xFF;

    send_raw(ubx::CLASS_RACEBOX, ubx::ID_RECORDING_CONFIG, payload, sizeof(payload));
}

void handle_ubx_message(uint8_t msg_class, uint8_t msg_id, const uint8_t* payload, uint16_t payload_len) {
    if (msg_class != ubx::CLASS_RACEBOX) {
        return;
    }

    switch (msg_id) {
        case ubx::ID_RECORDING_STATUS: {
            if (payload_len < 12) {
                break; // malformed/unexpected short reply, ignore
            }
            g_recording_state = payload[0];
            const uint8_t security = payload[2];
            g_memory_secured = (security & 0x01) != 0;
            g_memory_unlocked = (security & 0x02) != 0;
            g_status_known = true;

            if (g_memory_secured && !g_memory_unlocked && !g_unlock_attempted) {
                g_unlock_attempted = true;
                send_unlock(kMemoryUnlockCode);
            }
            update_display_state();
            break;
        }
        case ubx::ID_STATE_CHANGE: {
            if (payload_len < 1) {
                break;
            }
            g_recording_state = payload[0];
            g_status_known = true;
            update_display_state();
            break;
        }
        case ubx::ID_ACK: {
            if (payload_len >= 2 && payload[0] == ubx::CLASS_RACEBOX && payload[1] == ubx::ID_UNLOCK_MEMORY) {
                g_memory_unlocked = true;
                update_display_state();
            }
            break;
        }
        case ubx::ID_NACK: {
            if (payload_len >= 2 && payload[0] == ubx::CLASS_RACEBOX && payload[1] == ubx::ID_UNLOCK_MEMORY) {
                g_memory_unlocked = false;
                update_display_state();
            }
            break;
        }
        default:
            break;
    }
}

// Discovers the CCCD descriptor of the TX characteristic. Called once per
// descriptor found (error->status == 0), then a final time with
// error->status == BLE_HS_EDONE once the [start_handle, end_handle] range
// has been fully walked. Because a descriptor always sits between its own
// characteristic value and the next characteristic declaration, the first
// 0x2902 match in this range is guaranteed to belong to the TX characteristic.
int on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error* error, uint16_t chr_val_handle,
                 const struct ble_gatt_dsc* dsc, void* arg) {
    if (error->status == 0 && dsc != nullptr) {
        if (g_tx_cccd_handle == 0 && ble_uuid_cmp(&dsc->uuid.u, &kCccdUuid.u) == 0) {
            g_tx_cccd_handle = dsc->handle;
        }
        return 0;
    }

    if (g_tx_cccd_handle == 0) {
        ESP_LOGW(kTag, "TX CCCD not found");
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    const uint8_t enable_notify[2] = {0x01, 0x00};
    ble_gattc_write_flat(g_conn_handle, g_tx_cccd_handle, enable_notify, sizeof(enable_notify), nullptr, nullptr);

    request_recording_status();
    update_display_state();
    return 0;
}

// Discovers every characteristic in the UART service, called once per
// characteristic (error->status == 0), then a final time with
// error->status == BLE_HS_EDONE once done.
int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg) {
    if (error->status == 0 && chr != nullptr) {
        if (ble_uuid_cmp(&chr->uuid.u, &kUartRxCharUuid.u) == 0) {
            g_rx_val_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &kUartTxCharUuid.u) == 0) {
            g_tx_val_handle = chr->val_handle;
        }
        return 0;
    }

    if (g_rx_val_handle == 0 || g_tx_val_handle == 0) {
        ESP_LOGW(kTag, "UART RX/TX characteristic not found");
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    const int rc = ble_gattc_disc_all_dscs(g_conn_handle, g_tx_val_handle + 1, g_svc_end_handle, on_dsc_disc, nullptr);
    if (rc != 0) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

// Discovers the UART service by UUID, called once on a match
// (error->status == 0), then a final time with error->status == BLE_HS_EDONE.
int on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_svc* service,
                 void* arg) {
    if (error->status == 0 && service != nullptr) {
        g_svc_start_handle = service->start_handle;
        g_svc_end_handle = service->end_handle;
        return 0;
    }

    if (g_svc_end_handle == 0) {
        ESP_LOGW(kTag, "UART service not found");
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    const int rc = ble_gattc_disc_all_chrs(g_conn_handle, g_svc_start_handle, g_svc_end_handle, on_chr_disc, nullptr);
    if (rc != 0) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

int gap_event_handler(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ble_manager_resume_scan_if_needed();
                return 0;
            }

            g_conn_handle = event->connect.conn_handle;
            reset_connection_state();
            g_state = RaceBoxState::CONNECTING;

            const int rc = ble_gattc_disc_svc_by_uuid(g_conn_handle, &kUartServiceUuid.u, on_svc_disc, nullptr);
            if (rc != 0) {
                ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT: {
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            reset_connection_state();
            g_state = RaceBoxState::SCANNING;
            ble_manager_resume_scan_if_needed();
            return 0;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (event->notify_rx.attr_handle != g_tx_val_handle) {
                return 0;
            }
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t buf[512];
            if (len > sizeof(buf)) {
                len = sizeof(buf);
            }
            os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
            g_parser.feed(buf, len);
            return 0;
        }

        default:
            return 0;
    }
}

} // namespace

bool racebox_ble::matches(const struct ble_hs_adv_fields& fields) {
    if (fields.name == nullptr || fields.name_len < kNamePrefixLen) {
        return false;
    }
    return std::memcmp(fields.name, kRaceBoxNamePrefix, kNamePrefixLen) == 0;
}

void racebox_ble::connect(uint8_t own_addr_type, const ble_addr_t& addr) {
    g_state = RaceBoxState::CONNECTING;
    const int rc = ble_gap_connect(own_addr_type, &addr, 30000, nullptr, gap_event_handler, nullptr);
    if (rc != 0) {
        ESP_LOGW(kTag, "ble_gap_connect failed: %d", rc);
        ble_manager_resume_scan_if_needed();
    }
}

bool racebox_ble::is_connected() {
    return g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

RaceBoxState racebox_ble::state() {
    return g_state;
}

void racebox_ble::toggle_recording() {
    if (g_rx_val_handle == 0 || !g_status_known || g_state == RaceBoxState::MEMORY_LOCKED) {
        return;
    }
    send_recording_config(g_recording_state == 0);
}
