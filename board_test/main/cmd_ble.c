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
#include <nvs_flash.h>
#include <protocomm.h>
#include <protocomm_ble.h>

#include "cmd_ble.h"

static const char*	TAG = "cmd_ble";

typedef struct {
	protocomm_t*	pComm;
	bool			isRunning;
} control_t;


void initialize_ble(void)
{
	// ToDo ?
}

static control_t	control;

static struct {
    struct arg_int *sid;
    struct arg_end *end;
} ble_args;


static esp_err_t testHandler(
    uint32_t        session_id, /*!< Session ID for identifying protocomm client */
    const uint8_t  *inbuf,      /*!< Pointer to user provided input data buffer */
    ssize_t         inlen,      /*!< Length o the input buffer */
    uint8_t       **outbuf,     /*!< Pointer to output buffer allocated by handler */
    ssize_t        *outlen,     /*!< Length of the allocated output buffer */
    void           *priv_data   /*!< Private data passed to the handler (NULL if not used) */
)
{
	return ESP_OK;
}


static esp_err_t ble_on(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **)&ble_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ble_args.end, argv[0]);
        return ESP_FAIL;
    }

    uint16_t	sid = (uint16_t)ble_args.sid->ival[0];

    if ((control.pComm = protocomm_new()) == NULL) {
    	return ESP_ERR_NO_MEM;
    }

    protocomm_ble_name_uuid_t	nuLookup[] = {
    	{"test_endpoint", 0x7887}
    };
    int nuLookupSz = sizeof(nuLookup) / sizeof(nuLookup[0]);

    protocomm_ble_config_t	conf = {
    	.service_uuid = {
   	        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
   			0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00
    	},
		.nu_lookup_count = nuLookupSz,
		.nu_lookup = nuLookup
    };

    // Patch in the device name
    snprintf(conf.device_name, MAX_BLE_DEVNAME_LEN, "PW-IWO-%04X", sid);

    esp_err_t err;

    if ((err = protocomm_ble_start(control.pComm, &conf)) != ESP_OK) {
    	ESP_LOGE(TAG, "BLE start error 0x%x", err);
    	protocomm_delete(control.pComm);
    	return err;
    }

    protocomm_add_endpoint(control.pComm, "test_endpoint", testHandler, NULL);

    control.isRunning = true;
    return ESP_OK;
}

static esp_err_t ble_off()
{
	if (!control.isRunning) {
		return ESP_OK;
	}

    protocomm_remove_endpoint(control.pComm, "test_endpoint");
	protocomm_ble_stop(control.pComm);
	protocomm_delete(control.pComm);
	control.pComm = NULL;

	control.isRunning = false;

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
