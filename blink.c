/**
 * Complete BLE GATT Server for Raspberry Pi Pico W
 * Matches the functionality expected by bleCommunication.py
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

// GATT database handles - these will be set when the database is created
static uint16_t notify_char_handle;
static uint16_t write_char_handle;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;

// Command processing
static char command_buffer[256];
static int command_index = 0;

// BTstack callback registration
static btstack_packet_callback_registration_t hci_event_callback_registration;

// Advertisement data - device name "Nano33BLE"
static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,  // Flags
    0x0B, 0x09, 'P', 'i', 'c', 'o', 'B', 'e', 'a', 'c', 'o', 'n' // Complete Local Name
};

// GATT Database in binary format
// This defines our custom service and characteristics
static const uint8_t gatt_database[] = {
    // Primary Service Declaration - GAP Service
    0x00, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28,
    0x00, 0x18,
    
    // Device Name Characteristic Declaration
    0x00, 0x02, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28,
    0x02, 0x03, 0x00, 0x00, 0x2a,
    
    // Device Name Characteristic Value
    0x00, 0x03, 0x02, 0x00, 0x02, 0x00, 0x00, 0x2a,
    'N', 'a', 'n', 'o', '3', '3', 'B', 'L', 'E',
    
    // Primary Service Declaration - Custom Service
    0x00, 0x04, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28,
    0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x00, 0x00, 0xb1, 0x19,
    
    // Notify Characteristic Declaration
    0x00, 0x05, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28,
    0x10, 0x06, 0x00, 0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x01, 0x00, 0xb1, 0x19,
    
    // Notify Characteristic Value
    0x00, 0x06, 0x02, 0x00, 0x10, 0x00, 0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x01, 0x00, 0xb1, 0x19,
    0x00,
    
    // Client Characteristic Configuration Descriptor
    0x00, 0x07, 0x02, 0x00, 0x0A, 0x00, 0x02, 0x29,
    0x00, 0x00,
    
    // Write Characteristic Declaration  
    0x00, 0x08, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28,
    0x08, 0x09, 0x00, 0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x02, 0x00, 0xb1, 0x19,
    
    // Write Characteristic Value
    0x00, 0x09, 0x02, 0x00, 0x08, 0x00, 0x14, 0x12, 0x8a, 0x76, 0x04, 0xd1, 0x6c, 0x4f, 0x7e, 0x53, 0xf2, 0xe8, 0x02, 0x00, 0xb1, 0x19,
    0x00,
};

// Send notification to connected client
static void send_notification(const char* message) {
    if (connection_handle != HCI_CON_HANDLE_INVALID && notify_char_handle) {
        att_server_notify(connection_handle, notify_char_handle, (uint8_t*)message, strlen(message));
        printf("Sent: %s\n", message);
    }
}

// Process complete command
static void process_command(const char* cmd) {
    printf("Processing command: '%s'\n", cmd);
    
    if (strcmp(cmd, "led on") == 0) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        send_notification("LED turned ON");
        
    } else if (strcmp(cmd, "led off") == 0) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        send_notification("LED turned OFF");
        
    } else if (strcmp(cmd, "led?") == 0) {
        bool led_state = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);
        send_notification(led_state ? "LED is ON" : "LED is OFF");
        
    } else if (strlen(cmd) > 0) {
        // Echo back any other command with "pico says:" prefix
        char response[300];
        snprintf(response, sizeof(response), "pico says: %s", cmd);
        send_notification(response);
    }
}

// ATT read callback
static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle, 
                                  uint16_t offset, uint8_t * buffer, uint16_t buffer_size) {
    // Return empty data for characteristic reads
    if (buffer_size >= 1) {
        buffer[0] = 0x00;
        return 1;
    }
    return 0;
}

// ATT write callback - handles incoming commands
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, 
                              uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    
    if (att_handle == write_char_handle) {
        // Process incoming data character by character
        for (int i = 0; i < buffer_size; i++) {
            char c = buffer[i];
            
            if (c == '\0' || c == '\n' || c == '\r') {
                // End of command - process it
                if (command_index > 0) {
                    command_buffer[command_index] = '\0';
                    process_command(command_buffer);
                    command_index = 0;
                }
            } else if (command_index < sizeof(command_buffer) - 1) {
                // Add character to buffer
                command_buffer[command_index++] = c;
            }
        }
    }
    return 0;
}

// Bluetooth event handler
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET) return;
    
    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BTstack ready - starting advertising\n");
                
                // Set advertising data and start advertising
                gap_advertisements_set_params(0x30, 0x30, 0, 0, (bd_addr_t){0}, 0x07, 0x00);
                gap_advertisements_set_data(sizeof(adv_data), adv_data);
                gap_advertisements_enable(1);
                printf("Advertising as 'Nano33BLE'\n");
            }
            break;
            
        case HCI_EVENT_LE_META:
            switch (hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                    connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    printf("Client connected (handle: %04x)\n", connection_handle);
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);  // LED on when connected
                    
                    // Send required connection signals
                    sleep_ms(100);
                    send_notification("<CONNECTED>");
                    sleep_ms(100);
                    send_notification("<READY>");
                    break;
            }
            break;
            
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Client disconnected\n");
            connection_handle = HCI_CON_HANDLE_INVALID;
            command_index = 0;  // Reset command buffer
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);  // LED off when disconnected
            gap_advertisements_enable(1);  // Restart advertising
            break;
            
        case ATT_EVENT_CONNECTED:
            printf("ATT connected\n");
            break;
            
        case ATT_EVENT_DISCONNECTED:
            printf("ATT disconnected\n");
            break;
    }
}

int main() {
    stdio_init_all();
    printf("Starting BLE GATT Server for PicoBeacon compatibility...\n");
    
    // Initialize CYW43
    if (cyw43_arch_init() != 0) {
        printf("Failed to initialize CYW43\n");
        return 1;
    }
    
    // Initialize BTstack
    l2cap_init();
    sm_init();
    
    // Setup ATT server
    att_server_init((uint8_t*)gatt_database, &att_read_callback, &att_write_callback);
    
    // Set the characteristic handles (these correspond to the GATT database)
    notify_char_handle = 0x0006;  // Handle for notify characteristic value
    write_char_handle = 0x0009;   // Handle for write characteristic value
    
    // Register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    
    // Register for ATT events
    att_server_register_packet_handler(&packet_handler);
    
    // Turn on Bluetooth
    hci_power_control(HCI_POWER_ON);
    
    printf("GATT server ready. Waiting for connections...\n");
    printf("Device will appear as 'PicoBeacon'\n");
    printf("Supports commands: 'led on', 'led off', 'led?', and echo\n");
    
    // Main loop
    while (true) {
        btstack_run_loop_execute();
        sleep_ms(10);
    }
    
    return 0;
}