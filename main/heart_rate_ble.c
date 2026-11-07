/*
 * heart_rate_ble.c
 *
 * Minimal NimBLE central implementation for the standard Bluetooth SIG
 * Heart Rate service (0x180D), as advertised by most chest straps and
 * sports watches (e.g. Polar H10, Wahoo TICKR):
 *   1. Is handed an advertisement matching service 0x180D by ble_manager's
 *      shared scan (see heart_rate_ble_matches())
 *   2. Connects to it (heart_rate_ble_connect())
 *   3. Discovers the Heart Rate service and its Heart Rate Measurement
 *      characteristic (0x2A37)
 *   4. Subscribes (writes CCCD) to it
 *   5. Parses each notification per the GATT Heart Rate Measurement
 *      format and exposes the latest bpm reading
 *
 * No bonding is requested: unlike the GoPro, the standard Heart Rate
 * service doesn't require it, so this module stays as simple as
 * racebox_ble (connect, discover, subscribe, done).
 */

#include <string.h>
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"

#include "ble_manager.h"
#include "heart_rate_ble.h"

static const char *TAG = "heart_rate_ble";

#define HR_SVC_UUID16 0x180D
#define HR_MEASUREMENT_CHR_UUID16 0x2A37
#define CCCD_UUID16 0x2902

static ble_addr_t s_hr_addr;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start_handle, s_svc_end_handle;
static uint16_t s_hrm_val_handle;
static uint16_t s_hrm_cccd_handle;

static uint16_t s_bpm;
static bool s_bpm_valid = false;

static int hr_gap_event(struct ble_gap_event *event, void *arg);

/* ---- Step 4: discover the CCCD and subscribe -------------------------- */

static int hr_on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle,
                           const struct ble_gatt_dsc *dsc, void *arg)
{
    if (error->status == 0 && dsc != NULL) {
        if (s_hrm_cccd_handle == 0 && ble_uuid_u16(&dsc->uuid.u) == CCCD_UUID16) {
            s_hrm_cccd_handle = dsc->handle;
        }
        return 0;
    }

    if (s_hrm_cccd_handle == 0) {
        ESP_LOGW(TAG, "Heart Rate Measurement CCCD not found");
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    uint8_t enable_notify[2] = {0x01, 0x00};
    ble_gattc_write_flat(s_conn_handle, s_hrm_cccd_handle, enable_notify, sizeof(enable_notify), NULL, NULL);
    ESP_LOGI(TAG, "Subscribed to Heart Rate Measurement notifications");
    return 0;
}

/* ---- Step 3: discover characteristics of the Heart Rate service ------- */

static int hr_on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr,
                           void *arg)
{
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_u16(&chr->uuid.u) == HR_MEASUREMENT_CHR_UUID16) {
            s_hrm_val_handle = chr->val_handle;
        }
        return 0;
    }

    if (s_hrm_val_handle == 0) {
        ESP_LOGW(TAG, "Heart Rate Measurement characteristic not found");
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    /* ble_gattc_disc_all_dscs's start_handle is the characteristic VALUE
     * handle itself (NimBLE starts the real ATT search at start_handle+1
     * internally) - passing val_handle+1 here would skip straight past the
     * CCCD, which normally sits at val_handle+1, and search from
     * val_handle+2 instead. */
    int rc = ble_gattc_disc_all_dscs(s_conn_handle, s_hrm_val_handle, s_svc_end_handle, hr_on_dsc_disc, NULL);
    if (rc != 0) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

/* ---- Step 2: discover the Heart Rate service by UUID ------------------- */

static int hr_on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service,
                           void *arg)
{
    if (error->status == 0 && service != NULL) {
        s_svc_start_handle = service->start_handle;
        s_svc_end_handle = service->end_handle;
        return 0;
    }

    if (s_svc_end_handle == 0) {
        ESP_LOGW(TAG, "Heart Rate service not found");
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    int rc = ble_gattc_disc_all_chrs(s_conn_handle, s_svc_start_handle, s_svc_end_handle, hr_on_chr_disc, NULL);
    if (rc != 0) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

static void hr_start_discovery(void)
{
    s_svc_start_handle = 0;
    s_svc_end_handle = 0;
    s_hrm_val_handle = 0;
    s_hrm_cccd_handle = 0;

    ble_uuid16_t svc_uuid = BLE_UUID16_INIT(HR_SVC_UUID16);
    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &svc_uuid.u, hr_on_svc_disc, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "disc_svc_by_uuid failed rc=%d", rc);
    }
}

/* ---- Step 1: advertisement filtering / connect ------------------------- */

bool heart_rate_ble_matches(const struct ble_hs_adv_fields *fields)
{
    for (int i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == HR_SVC_UUID16) {
            return true;
        }
    }
    return false;
}

void heart_rate_ble_connect(const ble_addr_t *addr)
{
    ESP_LOGI(TAG, "Found Heart Rate Monitor advertisement, connecting...");
    s_hr_addr = *addr;

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        ble_manager_resume_scan_if_needed();
        return;
    }

    rc = ble_gap_connect(own_addr_type, &s_hr_addr, 30000, NULL, hr_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed rc=%d", rc);
        ble_manager_resume_scan_if_needed();
    }
}

bool heart_rate_ble_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool heart_rate_ble_get_bpm(uint16_t *out_bpm)
{
    if (!s_bpm_valid) {
        return false;
    }
    *out_bpm = s_bpm;
    return true;
}

/* ---- Heart Rate Measurement notification parsing ------------------------
 * GATT Heart Rate Measurement (0x2A37): byte 0 is a flags field.
 *   bit 0: 0 = next byte is uint8 bpm, 1 = next 2 bytes are uint16 bpm (LE)
 *   bits 1-2: sensor contact status (ignored here)
 *   bit 3: energy expended present (uint16 follows, ignored here)
 *   bit 4: one or more RR-Interval uint16 values follow (ignored here)
 * Only the bpm value is needed for this display, so everything after it is
 * parsed-past but not used. */
static void handle_hrm_notification(const uint8_t *data, uint16_t len)
{
    if (len < 2) {
        return;
    }

    const uint8_t flags = data[0];
    if (flags & 0x01) {
        if (len < 3) {
            return;
        }
        s_bpm = (uint16_t)(data[1] | (data[2] << 8));
    } else {
        s_bpm = data[1];
    }

    // Log only on change (notifications typically arrive ~1/s and often
    // repeat the same value) so this stays useful in the serial monitor
    // without spamming it.
    static uint16_t s_last_logged_bpm = 0;
    if (!s_bpm_valid || s_bpm != s_last_logged_bpm) {
        ESP_LOGI(TAG, "Heart rate: %u bpm", (unsigned)s_bpm);
        s_last_logged_bpm = s_bpm;
    }

    s_bpm_valid = true;
}

/* ---- master GAP event handler ------------------------------------------ */

static int hr_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Connected. Discovering Heart Rate service...");
            s_conn_handle = event->connect.conn_handle;
            s_bpm_valid = false;
            // Connecting to this peer cancelled the shared scan; resume it
            // now so the RaceBox/GoPro scan keeps going independently of
            // this heart-rate-monitor connection's own progress.
            ble_manager_resume_scan_if_needed();
            hr_start_discovery();
        } else {
            ESP_LOGE(TAG, "Connection failed, status=%d", event->connect.status);
            ble_manager_resume_scan_if_needed();
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle != s_hrm_val_handle) {
            return 0;
        }
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        uint8_t buf[32];
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
        handle_hrm_notification(buf, len);
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected, reason=%d. Rescanning...", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_bpm_valid = false;
        ble_manager_resume_scan_if_needed();
        return 0;

    default:
        return 0;
    }
}
