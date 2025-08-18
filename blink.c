// BLE peripheral for Raspberry Pi Pico W using att_db_util
// - Fixed 5 Hz streaming of current time since boot (ms) once notifications are enabled
// - Commands: "led on", "led off", "led?" (responses via notify)
// - Sends <READY>\n only after CCCD enables notifications
// - Uses ATT_EVENT_CAN_SEND_NOW + tiny queue for reliable notifies

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "btstack_run_loop.h"
#include "att_db_util.h"

static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static btstack_packet_callback_registration_t hci_event_cb;

static bool     led_on = false;
static uint16_t notify_val_handle = 0;
static uint16_t write_val_handle  = 0;
static uint16_t notify_cccd_handle = 0;   // value_handle + 1
static int      notify_enabled = 0;

// --- simple single-message queue for notifications ---
#define MSG_MAX 128
static char pending_msg[MSG_MAX];
static uint16_t pending_len = 0;

static void queue_note(const char *s){
    if (!s) return;
    if (connection_handle == HCI_CON_HANDLE_INVALID || !notify_enabled) return;
    if (pending_len) return; // drop if previous hasn't been sent yet (5 Hz won't backlog)
    size_t n = strlen(s);
    if (n > MSG_MAX) n = MSG_MAX;
    memcpy(pending_msg, s, n);
    pending_len = (uint16_t)n;
    att_server_request_can_send_now_event(connection_handle);
}

static inline uint32_t now_ms(void){ return to_ms_since_boot(get_absolute_time()); }

// --- fixed 5 Hz streaming (every ~200 ms) ---
static btstack_timer_source_t stream_timer;

static void restart_stream_timer(void){
    if (!notify_enabled || connection_handle == HCI_CON_HANDLE_INVALID) return;
    btstack_run_loop_set_timer(&stream_timer, 200);   // 5 Hz
    btstack_run_loop_add_timer(&stream_timer);
}

static void stream_cb(btstack_timer_source_t *ts){
    (void)ts;
    if (notify_enabled && connection_handle != HCI_CON_HANDLE_INVALID){
        char line[MSG_MAX];
        snprintf(line, sizeof(line), "TIME=%.3f\n", now_ms() / 1000.0f);
        queue_note(line);
    }
    restart_stream_timer();
}

// UUIDs (little-endian bytes)
static const uint8_t UUID_SVC_19B1_0000[16] = {0x14,0x12,0x8a,0x76,0x04,0xd1,0x6c,0x4f,0x7e,0x53,0xf2,0xe8,0x00,0x00,0xb1,0x19};
static const uint8_t UUID_CHR_19B1_0001[16] = {0x14,0x12,0x8a,0x76,0x04,0xd1,0x6c,0x4f,0x7e,0x53,0xf2,0xe8,0x01,0x00,0xb1,0x19};
static const uint8_t UUID_CHR_19B1_0002[16] = {0x14,0x12,0x8a,0x76,0x04,0xd1,0x6c,0x4f,0x7e,0x53,0xf2,0xe8,0x02,0x00,0xb1,0x19};

// Advertise: flags + Complete List of 128-bit UUIDs (your service)
static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,
    0x11, 0x07,
    0x14,0x12,0x8a,0x76,0x04,0xd1,0x6c,0x4f,0x7e,0x53,0xf2,0xe8,0x00,0x00,0xb1,0x19
};

// Scan response: Complete Local Name "PicoBLE"
static uint8_t scan_resp[] = {
    0x08, 0x09, 'P','i','c','o','B','L','E'
};

static void set_led(bool on){
    led_on = on;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

static void send_led_status(void){
    queue_note(led_on ? "LED=ON\n" : "LED=OFF\n");
}

// --- ATT callbacks ---
static uint16_t att_read_cb(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset,
                            uint8_t *buffer, uint16_t buffer_size){
    (void)con_handle; (void)att_handle; (void)offset;
    if (buffer && buffer_size){
        buffer[0] = 0x00;  // keep a 1-byte readable value
        return 1;
    }
    return 0;
}

// NOTE: write callback wants non-const data* per btstack typedef
static int att_write_cb(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode,
                        uint16_t offset, uint8_t *data, uint16_t len){
    (void)con_handle; (void)transaction_mode; (void)offset;

    // Client toggled notifications?
    if (att_handle == notify_cccd_handle){
        int en = 0;
        if (len >= 2){
            uint16_t v = data[0] | (data[1] << 8);
            en = (v & 0x0001) ? 1 : 0;
        }
        notify_enabled = en;
        if (notify_enabled){
            queue_note("<READY>\n");
            restart_stream_timer();    // start fixed 5 Hz stream
        } else {
            pending_len = 0;
        }
        return 0;
    }

    // Command written to our write characteristic?
    if (att_handle == write_val_handle){
        if (len == 0 || len > 120){ queue_note("ERR\n"); return 0; }

        char cmd[128];
        memcpy(cmd, data, len); cmd[len] = '\0';

        // trim
        char *p = cmd; while (*p && isspace((unsigned char)*p)) ++p;
        size_t L = strlen(p);
        while (L && isspace((unsigned char)p[L-1])) p[--L] = '\0';

        // lowercase
        char low[128] = {0};
        for (size_t i=0; i<L && i<sizeof(low)-1; ++i) low[i] = (char)tolower((unsigned char)p[i]);

        if (strcmp(low,"led on")==0){
            set_led(true);  queue_note("OK\n");
        } else if (strcmp(low,"led off")==0){
            set_led(false); queue_note("OK\n");
        } else if (strcmp(low,"led?")==0 || strcmp(low,"led ?")==0){
            send_led_status();
        } else if (strcmp(low,"test")==0){
            queue_note("OK\n");
        } else {
            char out[MSG_MAX];
            snprintf(out,sizeof(out),"pico says: %s\n", p);
            queue_note(out);
        }
        return 0;
    }

    return 0;
}

// --- HCI / ATT event handler ---
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING){
                gap_advertisements_set_params(0x30, 0x30, 0, 0, (bd_addr_t){0}, 0x07, 0x00);
                gap_advertisements_set_data(sizeof(adv_data), adv_data);
                gap_scan_response_set_data(sizeof(scan_resp), scan_resp);
                gap_advertisements_enable(1);
                printf("Advertising (UUID + name in scan response)\n");
            }
            break;

        case HCI_EVENT_LE_META: {
            uint8_t sub = hci_event_le_meta_get_subevent_code(packet);
            if (sub == HCI_SUBEVENT_LE_CONNECTION_COMPLETE){
                connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                printf("Connected (handle %04x)\n", connection_handle);
                // 30–50 ms interval is fine for >=5 Hz notify
                gap_request_connection_parameter_update(connection_handle, 24, 40, 0, 400);
                if (notify_enabled) restart_stream_timer();
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected\n");
            connection_handle = HCI_CON_HANDLE_INVALID;
            notify_enabled = 0;
            pending_len = 0;
            gap_advertisements_enable(1);
            break;

        case ATT_EVENT_CAN_SEND_NOW:
            if (notify_enabled && pending_len > 0 && connection_handle != HCI_CON_HANDLE_INVALID){
                att_server_notify(connection_handle, notify_val_handle,
                                  (const uint8_t*)pending_msg, pending_len);
                pending_len = 0;
            }
            break;

        default:
            break;
    }
}

static void build_gatt(void){
    att_db_util_init();

    // GAP: Device Name "PicoBLE"
    att_db_util_add_service_uuid16(0x1800);
    att_db_util_add_characteristic_uuid16(
        0x2A00, ATT_PROPERTY_READ,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE,
        (uint8_t*)"PicoBLE", 7
    );

    // GATT: Service Changed
    uint8_t svc_changed_val[4] = {0x00,0x00,0x00,0x00};
    att_db_util_add_service_uuid16(0x1801);
    att_db_util_add_characteristic_uuid16(
        0x2A05, ATT_PROPERTY_INDICATE,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE,
        svc_changed_val, sizeof(svc_changed_val)
    );

    // Custom service + characteristics
    att_db_util_add_service_uuid128(UUID_SVC_19B1_0000);

    // Notify (READ | NOTIFY) with 1-byte initial value
    uint8_t init_notify_val[1] = {0x00};
    notify_val_handle = att_db_util_add_characteristic_uuid128(
        UUID_CHR_19B1_0001,
        ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE,
        init_notify_val, sizeof(init_notify_val)
    );

    // In att_db_util, CCCD is allocated immediately after the value handle
    notify_cccd_handle = (uint16_t)(notify_val_handle + 1);

    // Write (WRITE w/ response) dynamic
    write_val_handle = att_db_util_add_characteristic_uuid128(
        UUID_CHR_19B1_0002,
        ATT_PROPERTY_WRITE | ATT_PROPERTY_DYNAMIC,
        ATT_SECURITY_NONE, ATT_SECURITY_NONE,
        NULL, 0
    );
}

int main(void){
    stdio_init_all();
    for (int i = 0; i < 100; ++i) sleep_ms(10);
    printf("BLE: PicoBLE 5Hz time streamer (att_db_util)\n");

    if (cyw43_arch_init() != 0){
        printf("CYW43 init failed\n");
        return 1;
    }
    set_led(false); // OFF at start

    l2cap_init();
    sm_init();

    build_gatt();
    printf("notify=0x%04x write=0x%04x cccd=0x%04x\n",
           notify_val_handle, write_val_handle, notify_cccd_handle);

    // init stream timer
    btstack_run_loop_set_timer_handler(&stream_timer, stream_cb);

    att_server_init(att_db_util_get_address(), &att_read_cb, &att_write_cb);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);
    att_server_register_packet_handler(&packet_handler);

    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
