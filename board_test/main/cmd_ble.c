/* Copyright (C) Grid Connect, Inc - All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* Written by Evandro Copercini <evandro@gridconnect.com>, May 2019
*/

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <simple_ble.h>
#include <nvs_flash.h>

#include "cmd_ble.h"

void initialize_ble(void)
{
	// ToDo ?
}


static esp_err_t ble_off()
{
    esp_err_t ret = ESP_OK;
    ret = simple_ble_stop();
    if (ret) {
        ESP_LOGE(__func__, "BLE stop failed");
        return ret;
    }
    ret = simple_ble_deinit();
    return ret;
}

static struct {
    struct arg_int *sid;
    struct arg_end *end;
} ble_args;


static void bleCbRead(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void bleCbWrite(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void bleCbExecWrite(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void bleCbConnect(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void bleCbDisconnect(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static void bleCbSetMTU(esp_gatts_cb_event_t event, esp_gatt_if_t p_gatts_if, esp_ble_gatts_cb_param_t *param)
{

}

static esp_err_t ble_on(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **)&ble_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ble_args.end, argv[0]);
        return ESP_FAIL;
    }   
    
    uint16_t	sid = (uint16_t)ble_args.sid->ival[0];

    uint8_t service_uuid128[16] = {
        /* LSB <--------------------------------------------------------------------------------> MSB */
        //first uuid, 16bit, [12],[13] is the value
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    };

    service_uuid128[12] = (uint8_t)(sid >> 0);
    service_uuid128[13] = (uint8_t)(sid >> 8);

    static char	ble_device_name[32];
    snprintf(ble_device_name, sizeof(ble_device_name), "PW-IWO-%04X", sid);

    /* The length of adv data must be less than 31 bytes */
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp        = false,
        .include_name        = true,
        .include_txpower     = true,
        .min_interval        = 0x0006,
        .max_interval        = 0x0010,
        .appearance          = ESP_BLE_APPEARANCE_UNKNOWN,
        .manufacturer_len    = 0,
        .p_manufacturer_data = NULL,
        .service_data_len    = 0,
        .p_service_data      = NULL,
        .service_uuid_len    = sizeof(service_uuid128),
        .p_service_uuid      = service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    esp_ble_adv_params_t adv_params = {
        .adv_int_min         = 0x20,
        .adv_int_max         = 0x40,
        .adv_type            = ADV_TYPE_IND,
        .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
        .channel_map         = ADV_CHNL_ALL,
        .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    esp_err_t err;

    // Initialize NVS.
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(__func__, "NVS init error code 0x%x", err);
        return err;
    }

    simple_ble_cfg_t *ble_config = simple_ble_init();
    if (ble_config == NULL) {
        ESP_LOGE(__func__, "Ran out of memory for BLE config");
        return ESP_ERR_NO_MEM;
    }

    /* Set function pointers required for simple BLE layer */
    ble_config->read_fn         = bleCbRead;
    ble_config->write_fn        = bleCbWrite;
    ble_config->exec_write_fn   = bleCbExecWrite;
    ble_config->disconnect_fn   = bleCbDisconnect;
    ble_config->connect_fn      = bleCbConnect;
    ble_config->set_mtu_fn      = bleCbSetMTU;

    /* Set parameters required for advertising */
    ble_config->adv_data        = adv_data;
    ble_config->adv_params      = adv_params;
    ble_config->device_name     = ble_device_name;

    err = simple_ble_start(ble_config);
    if (err != ESP_OK) {
        ESP_LOGE(__func__, "simple_ble_start failed w/ error code 0x%x", err);
        simple_ble_deinit();
        return err;
    }

    ESP_LOGD(__func__, "Waiting for client to connect ......");
    return ESP_OK;
}

static void register_ble_on() 
{
    ble_args.sid = arg_int1(NULL, NULL, "<service id>", "4-digit hex value preceded by 0x, e.g. 0xA23C");
    ble_args.end = arg_end(1);
    
    const static esp_console_cmd_t cmd = {
        .command = "BLE-ON",
        .help = "The UUT will respond with \"OK\", turn on BLE, and set the advertisement to the service id value.",
        .hint = NULL,
        .func = &ble_on,
        .argtable = &ble_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static void register_ble_off() {
    const static esp_console_cmd_t cmd = {
        .command = "BLE-OFF",
        .help = "The UUT will shut down the BLE interface and respond with \"OK\"",
        .hint = NULL,
        .func = ble_off
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

void register_ble()
{
    register_ble_on();
    register_ble_off();
}
