/*
 * Analysis Module - Gateway
 * 2024
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_hs.h"
#include "sys/time.h"

#include "sdkconfig.h"

#include "esp_check_err.h"
#include "button.h"
#include "led.h"
#include "white_list.h"
#include "analysis_module.h"
#include "app_packet.h"


#define DEBUGGING   // enables ESP_CHECK macro (see more esp_check_err.h)
#define GPIO_LED    GPIO_NUM_8
#define GPIO_BUTTON GPIO_NUM_3

#define RSSI_ACCEPTABLE_LVL     -50         // acceptable rssi level for connection
#define DEEP_SLEEP_CYCLE_TIME   5 * 1000000 // in us (5 s)

#define MAC_STR_SIZE 3 * 6

// enumeration of possible modes for this device
// these modes determine the current state or functionality of the device
// UNSPECIFIED_MODE  - default or undefined mode
// REGISTRATION_MODE - device is in the process of registering other devices (sensors)
// DELETION_MODE     - device is in the process of deleting other devices (sensors)
typedef enum {
    UNSPECIFIED_MODE = 0,
    REGISTRATION_MODE = 1,
    DELETION_MODE = 2

} g_device_mode_t;


g_device_mode_t g_device_mode = UNSPECIFIED_MODE; // current mode, UNSPECIFIED_MODE by default
uint8_t g_ble_addr_type;        // addr type, set automatically in ble_hs_id_infer_auto()
const char* g_tag_am = "AM";    // tag used in ESP_CHECK


// button process callbacks (see more button.h)
void on_short_button_press();
void on_medium_button_press();
void on_long_button_press();

void init_ble();
void ble_app_on_sync(void);
void host_task();
static int ble_gap_event(struct ble_gap_event *event, void *arg);
void connect_if_interesting(struct ble_hs_adv_fields *fields, struct ble_gap_disc_desc *disc_desc);
void delete_if_reachable(struct ble_hs_adv_fields *fields, struct ble_gap_disc_desc *disc_desc);
static int read_time(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
void get_mac_str(uint8_t* addr, char (*mac_str)[MAC_STR_SIZE]);


void app_main(void)
{
    // inits led (see more led.h)
    led_init(GPIO_LED);

    // set up button cnfg and init button (see more button.h)
    button_cnfg_t button_cnfg = {
            .gpio_num = GPIO_BUTTON,
            .short_button_press_period_ms = 1000,
            .medium_button_press_period_ms = 5000,
            .long_button_press_period_ms = 10000,
            .on_short_button_press_cb = on_short_button_press,
            .on_medium_button_press_cb = on_medium_button_press,
            .on_long_button_press_cb  = on_long_button_press
    };
    button_init(button_cnfg);

    //init white list (see more white_list.h)
    init_white_list();

    // init NVS
    ESP_CHECK(nvs_flash_init(), g_tag_am);

    // init BLE
    init_ble();

    // get wakeup cause and do corresponding actions
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    switch (wakeup_cause)
    {
        case ESP_SLEEP_WAKEUP_GPIO:
        {
            // wakeup from gpio means that device was asleep and user
            // pressed on a button. next actions could be: registration,
            // deletion or just wakeup (needed for debug now)
            force_interupt();
            ESP_LOGI(g_tag_am, "Waking up from GPIO.");

            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER:
        {
            // wakeup from timer means that device is periodically sends data
            ESP_LOGI(g_tag_am, "Waking up from timer.");
            led_turn_on(); // turn on to show that device is awaken

            // set discovery parameters
            ESP_LOGI(g_tag_am, "Scanning for data.......");
            struct ble_gap_disc_params disc_params;
            disc_params.itvl = 0x0040;          // interval between window start
            disc_params.window = 0x0020;        // scan window duration
            disc_params.filter_policy = 1;      // scan only devices from white list
            disc_params.limited = 0;            // any discovery mode
            disc_params.passive = 1;            // no scan requests
            disc_params.filter_duplicates = 0;  // all packages, even duplicates

            // get white list with addrs to set in ble_gap_wl_set for scan
            ble_addr_t *wl_addrs;
            get_addr_white_list(&wl_addrs);
            ESP_CHECK(ble_gap_wl_set(wl_addrs, white_list_len), g_tag_am);
            free(wl_addrs);

            // start scanning for 1 s
            int32_t scan_duration_ms = 1 * 1000;
            ble_gap_disc(g_ble_addr_type, scan_duration_ms, &disc_params, ble_gap_event, NULL);

            // if white list is not empty, then we have registered
            // devices to get data from => enable timer wakeup
            // if not, we will just go to deepsleep until gpio wakeup
            if (!white_list_is_empty())
                ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), g_tag_am);

            break;
        }
        default:
        {
            // if we woke up from another cause, that means something
            // went wrong, so go back to sleep
            ESP_LOGI(g_tag_am, "Waking up from other cause.");
            ESP_LOGI(g_tag_am, "Go to sleep.");
            esp_deep_sleep_start();
            break;
        }
    }
}



// inits nimble, gap & gatt services
void init_ble()
{
    nimble_port_init();
    ble_svc_gap_device_name_set("Nemivika-AM");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // configure gatt characteristics
    const struct ble_gatt_chr_def gatt_chr_time = {
            .uuid = BLE_UUID16_DECLARE(0x2A2B),   // UUID Current Time
            .access_cb = read_time,
            .flags = BLE_GATT_CHR_F_READ};    // readable characteristic

    // TODO add battery info chr
    // configure gatt services
    const struct ble_gatt_svc_def gatt_svc_cnfg = {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = BLE_UUID16_DECLARE(0x180A), // UUID Device Information
            .characteristics = (struct ble_gatt_chr_def[]){gatt_chr_time, {0}}};


    // set configuration
    struct ble_gatt_svc_def gatt_svc_cnfgs[] = {gatt_svc_cnfg, {0}};
    ble_gatts_count_cfg(gatt_svc_cnfgs);
    ble_gatts_add_svcs(gatt_svc_cnfgs);

    // set the callback function to be executed when the ble stack is synchronised
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // init FreeRTOS task for nimble
    nimble_port_freertos_init(host_task);
}

void ble_app_on_sync(void)
{
    // infer and set the ble addr type
    ble_hs_id_infer_auto(0, &g_ble_addr_type);
}

// main nimble host task, handles the ble stack processing
void host_task()
{
    nimble_port_run();              // start nimble processing loop
    nimble_port_freertos_deinit();  // deinit FreeRTOS
}


// gap event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_DISC:
        {
            // if new device was discovered, we may:
            // - connect for registration
            // - connect for deletion
            // - get data from sensor
            ESP_LOGI(g_tag_am, "DISCOVERED new device!");

            struct ble_gap_disc_desc *disc_desc = &event->disc;
            struct ble_hs_adv_fields fields;
            ble_hs_adv_parse_fields(&fields, disc_desc->data, disc_desc->length_data);

            if (fields.name_len > 0)
                ESP_LOGI(g_tag_am, "Name: %.*s", fields.name_len, fields.name);

            char mac_str[MAC_STR_SIZE];
            get_mac_str(disc_desc->addr.val, &mac_str);
            ESP_LOGI(g_tag_am, "MAC: %s", mac_str);
            ESP_LOGI(g_tag_am, "RSSI: %d", disc_desc->rssi);

            ESP_LOGI(g_tag_am, "Packet len = %d", fields.mfg_data_len);
            ESP_LOGI(g_tag_am, "Header = 0x%x%x", fields.mfg_data[0], fields.mfg_data[1]);

            // if this device is in registration mode try to connect
            // if this device is in deletion mode try to connect
            if(g_device_mode == REGISTRATION_MODE)
                connect_if_interesting(&fields, disc_desc);
            else if(g_device_mode == DELETION_MODE)
                delete_if_reachable(&fields, disc_desc);
            else
            {
                // if this device is in unspecified mode try to get data
                // firstly, check package format, header must be DATA_HEADER meaning
                // discovered device has data to retrieve
                uint16_t header;
                uint8_t buffer[fields.mfg_data_len - HEADER_SIZE];
                if(open_packet(&header, buffer, fields.mfg_data, fields.mfg_data_len) == -1)
                {
                    ESP_LOGE(g_tag_am, "Error opening packet!!!");
                    ESP_LOGE(g_tag_am, "Header: %d", header);
                }
                else if (header == DATA_HEADER)
                {
                    // now we can get only temperature data, so go
                    // straight to conversation
                    ESP_LOGI(g_tag_am, "Header: %d", header);
                    ESP_LOGI(g_tag_am, "Temp: %f", convert_temp_data_to_float(buffer[0], buffer[1]));
                    ESP_LOGI(g_tag_am, "Temp raw: msb = %x lsb = %x\n", buffer[0], buffer[1]);

                    // push temperature data for storage (see file analysis_module.h)
                    ESP_CHECK(push_temp_data(convert_temp_data_to_float(buffer[0], buffer[1])), g_tag_am);
                }
            }
            break;
        }
        case BLE_GAP_EVENT_CONNECT:
        {
            // if device is connected, we may:
            // - register new device (sensor)
            // - delete registered device (sensor)
            struct ble_gap_conn_desc conn_desc;
            ble_gap_conn_find(event->connect.conn_handle, &conn_desc);

            // check status, if everything okay, then
            if (event->connect.status == 0)
            {
                ESP_LOGI(g_tag_am, "CONNECTION established!");

                char our_mac[MAC_STR_SIZE];
                char peer_mac[MAC_STR_SIZE];
                get_mac_str(conn_desc.our_id_addr.val, &our_mac);
                get_mac_str(conn_desc.peer_id_addr.val, &peer_mac);
                ESP_LOGI(g_tag_am, "MAC this device id addr:\t%s", our_mac);
                ESP_LOGI(g_tag_am, "MAC connected device id addr:\t%s", peer_mac);

                // if this device is in registration mode, try to disconnect
                // if this device is in deletion mode, delete from white list and try to disconnect
                if (g_device_mode == REGISTRATION_MODE)
                {
                    // start fast blink, meaning that registration was successful
                    led_start_blink(100, 100);
                    ESP_LOGI(g_tag_am, "Registration is completed.");
                }
                else if (g_device_mode == DELETION_MODE && white_list_contains_addr(&conn_desc.peer_id_addr))
                {
                    // delete from white list
                    bool deleted = remove_from_white_list_by_addr(&conn_desc.peer_id_addr) == ESP_OK ? true : false;
                    if (deleted)
                    {
                        // start slow blink, meaning that deletion was successful
                        led_start_blink(700, 700);
                        ESP_LOGI(g_tag_am, "Deletion is completed.");
                    }
                    else
                    {
                        ESP_LOGI(g_tag_am, "Deletion failed.");
                    }
                }
                // try to disconnect
                ESP_LOGI(g_tag_am, "Try to disconnect...");
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            else    // if error, remove addr from white list
            {
                ESP_LOGI(g_tag_am, "CONNECTION is NOT established!");
                remove_from_white_list_by_addr(&conn_desc.peer_id_addr);
            }

            // print info about white list
            ESP_LOGI(g_tag_am, "White List: len = %u", white_list_len);
            for(int i = 0; i < white_list_len; i++)
            {
                char wl_mac[MAC_STR_SIZE];
                get_mac_str(white_list[i].device_addr.val, &wl_mac);
                ESP_LOGI(g_tag_am, "WL[%d] = {%s}", i, wl_mac);
            }

            break;
        }
        case BLE_GAP_EVENT_DISCONNECT:
        {
            // print that device is disconnected
            char peer_mac[MAC_STR_SIZE];
            get_mac_str(event->disconnect.conn.peer_id_addr.val, &peer_mac);
            ESP_LOGI(g_tag_am, "DISCONNECTED with %s! The reason - %d.", peer_mac, event->disconnect.reason);

            break;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
        {
            // if device completed scan, we may:
            // - start analysis (if it was periodic scan for data)
            // - go to sleep (TODO if it was scanning for registration or deletion for too long)

            ESP_LOGI(g_tag_am, "Scanning is complete. Start analysis...");
            // start analysis of human state (see file analysis_module.h)
            liferate_t state = start_analysis();
            if (state == NORMAL)
                ESP_LOGI(g_tag_am, "State is NORMAL. The code is: %d", state);
            else if (state == CRITICAL)
                ESP_LOGI(g_tag_am, "State is CRITICAL. The code is: %d", state);
            else if (state == VERY_CRITICAL)
                ESP_LOGI(g_tag_am, "State is VERY CRITICAL. The code is: %d", state);
            else
            {
                ESP_LOGW(g_tag_am, "State is UNKNOWN. The code is: %d", state);
                ESP_LOGW(g_tag_am, "No new data was recorded.");
            }

            // turn led off before sleep
            led_turn_off();

            ESP_LOGI(g_tag_am, "Go to sleep...");
            esp_deep_sleep_start();
            break;
        }
        default:
            ESP_LOGI(g_tag_am, "Default.");
            break;
    }
    return 0;
}


// pressing on button during 1 - 5 s causes entering or exiting
// registration mode
void on_medium_button_press()
{
    // if device isn't in registration or deletion mode now,
    // then it could enter registration mode
    if (g_device_mode != REGISTRATION_MODE && g_device_mode != DELETION_MODE)
    {
        // set registration mode and turn led on
        g_device_mode = REGISTRATION_MODE;
        led_turn_on();

        ESP_LOGI(g_tag_am, "Entering register mode.");
        ESP_LOGI(g_tag_am, "Scanning for registration.......");

        // for registration, device starts discovery
        struct ble_gap_disc_params disc_params;
        disc_params.itvl = 0x0040;
        disc_params.window = 0x0020;
        disc_params.filter_policy = 0;      // scan all devices
        disc_params.limited = 0;            // any discovery mode
        disc_params.passive = 1;            // no scan requests
        disc_params.filter_duplicates = 0;  // all packages, even duplicates

        // set duration to forever (TODO not forever but some time) until device is found
        int32_t scan_duration_ms = BLE_HS_FOREVER;
        ble_gap_disc(g_ble_addr_type, scan_duration_ms, &disc_params, ble_gap_event, NULL);
    }
    else if (g_device_mode == REGISTRATION_MODE)
    {
        // if device is in registration mode now, that means user exit this mode
        ESP_LOGI(g_tag_am, "Quiting register mode.");

        // if white list is not empty, then we have registered
        // devices to get data from => enable timer wakeup.
        // if not, we will just go to deepsleep until gpio wakeup
        if (!white_list_is_empty())
            ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), g_tag_am);

        // turn led off as signal for exiting registration mode
        led_turn_off();

        // set device into unspecified mode and go to sleep
        g_device_mode = UNSPECIFIED_MODE;
        esp_deep_sleep_start();
    }
}


// pressing on button during 5 and more s causes entering or exiting
// deletion mode
void on_long_button_press()
{
    // if device isn't in registration or deletion mode now,
    // then it could enter deletion mode
    if (g_device_mode != DELETION_MODE && g_device_mode != REGISTRATION_MODE && !white_list_is_empty())
    {
        // set deletion mode and turn led on
        g_device_mode = DELETION_MODE;
        led_turn_on();

        ESP_LOGI(g_tag_am, "Entering deletion mode.");
        ESP_LOGI(g_tag_am, "Scanning for deletion.......");

        // for deletion, device starts discovery
        struct ble_gap_disc_params disc_params;
        disc_params.itvl = 0;
        disc_params.window = 0;
        disc_params.filter_policy = 1;      // scan only devices from white list (we can delete only registrated ones)
        disc_params.limited = 0;            // any discovery mode
        disc_params.passive = 1;            // no scan requests
        disc_params.filter_duplicates = 0;  // all packages, even duplicates

        // get white list with addrs to set in ble_gap_wl_set for scan
        ble_addr_t *wl_addrs;
        get_addr_white_list(&wl_addrs);
        ESP_CHECK(ble_gap_wl_set(wl_addrs, white_list_len), g_tag_am);
        free(wl_addrs);

        // set duration to forever (TODO not forever but some time) until device is found
        int32_t scan_duration_ms = BLE_HS_FOREVER;
        ble_gap_disc(g_ble_addr_type, scan_duration_ms, &disc_params, ble_gap_event, NULL);
    }
    else if (g_device_mode == DELETION_MODE)
    {
        // if device is in deletion mode now, that means user exit this mode
        ESP_LOGI(g_tag_am, "Quiting deletion mode.");

        // if white list is not empty, then we have registered
        // devices to get data from => enable timer wakeup.
        // if not, we will just go to deepsleep until gpio wakeup
        if (!white_list_is_empty())
            ESP_CHECK(esp_sleep_enable_timer_wakeup(DEEP_SLEEP_CYCLE_TIME), g_tag_am);

        // turn led off as signal for exiting registration mode
        led_turn_off();

        // set device into unspecified mode and go to sleep
        g_device_mode = UNSPECIFIED_MODE;
        esp_deep_sleep_start();
    }
}


// no action on button press under 1 s
void on_short_button_press()
{

}


// check if device (sensor) is reachable and interesting to
// connect for registration (adds sensor to white list)
void connect_if_interesting(struct ble_hs_adv_fields *fields, struct ble_gap_disc_desc *disc_desc)
{
    // check rssi
    if (disc_desc->rssi < RSSI_ACCEPTABLE_LVL)
        return;

    // check is analysis module-gateway is interested in
    // device with its uuid
    int8_t uuid_in_inter_index = -1; // index of interesting uuid among all uuids of device willing to connect
    for (int i = 0; i < fields->num_uuids16; i++)
        if(uuid_is_interesting(&fields->uuids16[i]))
        {
            uuid_in_inter_index = i;
            break;
        }

    // if interesting uuid was found and white list doesn't contain this mac
    if ( (uuid_in_inter_index != -1) && (!white_list_contains_addr(&disc_desc->addr) || white_list_is_empty()) )
    {
        // check package format, header must be REG_HEADER meaning
        // discovered device wants registration too
        uint16_t header;
        uint8_t* buffer = NULL;
        if(open_packet(&header, buffer, fields->mfg_data, fields->mfg_data_len) == -1)
        {
            ESP_LOGE(g_tag_am, "Error opening packet!!!");
            ESP_LOGE(g_tag_am, "Header: %d", header);
            return;
        }

        ESP_LOGI(g_tag_am, "Header: %d", header);

        // if header is REG_HEADER, stop discovery and connect
        // connection establishment needed for reliability and
        // confirmation of devices on both sides
        if (header == REG_HEADER)
        {
            char mac_str[MAC_STR_SIZE];
            get_mac_str(disc_desc->addr.val, &mac_str);
            ESP_LOGI(g_tag_am, "Device %s is interesting.", mac_str);

            ble_gap_disc_cancel(); // stop scan before connection initialisation
            push_to_white_list(fields->uuids16[uuid_in_inter_index], disc_desc->addr); // add to white list new addr
            ble_gap_connect(g_ble_addr_type, &disc_desc->addr, BLE_HS_FOREVER, NULL, ble_gap_event, NULL); // connect
        }
    }
}


// check if device (sensor) is reachable and connect for
// deletion (deletes sensor from white list)
void delete_if_reachable(struct ble_hs_adv_fields *fields, struct ble_gap_disc_desc *disc_desc)
{
    // check rssi
    if (disc_desc->rssi < RSSI_ACCEPTABLE_LVL)
        return;

    // check if white list contains this device
    // (if device is registred it must be in white list)
    if (!white_list_contains_addr(&disc_desc->addr))
        return;

    // check package format, header must be DEL_HEADER meaning
    // discovered device wants deletion too
    uint16_t header;
    uint8_t* buffer = NULL;
    if(open_packet(&header, buffer, fields->mfg_data, fields->mfg_data_len) == -1)
    {
        ESP_LOGE(g_tag_am, "Error opening packet!!!");
        ESP_LOGE(g_tag_am, "Header: %d", header);
        return;
    }

    ESP_LOGI(g_tag_am, "Header: %d", header);

    // if header is DEL_HEADER, stop discovery and connect
    // connection establishment needed for reliability and
    // confirmation of devices on both sides
    if (header == DEL_HEADER)
    {
        ble_gap_disc_cancel();
        ble_gap_connect(g_ble_addr_type, &disc_desc->addr, BLE_HS_FOREVER, NULL, ble_gap_event, NULL);
    }
}


// callback for accessing time of analysis module-gateway
// to synchronise sensors
static int read_time(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // TODO get time for reading and further synchronisation
    ESP_LOGI("TIME", "Start reading time....");

    const char* str = "Hello from the server";
    int rc = os_mbuf_append(ctxt->om, str, strlen(str));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}


// makes string with mac addr for printing
void get_mac_str(uint8_t* addr, char(*mac_str)[MAC_STR_SIZE])
{
    for (int i = 0; i < 6; i++)
        sprintf(*mac_str+(i*3), "%02X:", addr[5-i]);

    (*mac_str)[MAC_STR_SIZE - 1] = '\0';
}
