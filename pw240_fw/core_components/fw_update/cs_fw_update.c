/*
 * cs_fw_update.c
 *
 *  Created on: Mar 19, 2019
 *      Author: wesd
 */

#include "fw_update.h"
#include "cs_heap.h"
#include "cJSON.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "cs_ota_rollback.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"fw_upgrade"
#include "mod_debug.h"

#define	DEBUG_OTA//todo jonw remove

typedef struct {
	esp_http_client_config_t	httpConf;
	char						url[4000];
} fwUpdateCtrl_t;

// Enable DEBUG_OTA to see the OTA download progress
#ifdef DEBUG_OTA

static esp_err_t	httpEvent(esp_http_client_event_t * evt)
{
	static int	totalBytes;

	switch(evt->event_id)
	{
	case HTTP_EVENT_ERROR:
		gc_err("HTTP error");
		break;

	case HTTP_EVENT_ON_CONNECTED:
		gc_dbg("Connected");
		totalBytes = 0;
		break;

	case HTTP_EVENT_ON_DATA:
		totalBytes += evt->data_len;
		gc_dbg("Received %d bytes", evt->data_len);
		break;

	case HTTP_EVENT_ON_FINISH:
		gc_dbg("Transfer finished, total bytes received: %d", totalBytes);
		break;

	case HTTP_EVENT_DISCONNECTED:
		gc_dbg("Disconnected");
		break;

	default:
		break;
	}

	return ESP_OK;
}

#endif



esp_err_t csFwUpdate(esp_http_client_config_t * configIn)
{
	esp_err_t	status = ESP_FAIL;

	fwUpdateCtrl_t *	pCtrl;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	esp_http_client_config_t *	pConf = &pCtrl->httpConf;
	memset(pConf, 0, sizeof(*pConf));
	memcpy(pConf, configIn, sizeof(*pConf));
	snprintf(pCtrl->url, sizeof(pCtrl->url), configIn->url);

#ifdef DEBUG_OTA
	pConf->event_handler = httpEvent;
#endif

	pConf->url            = pCtrl->url;
	pConf->buffer_size    = 4096;
	pConf->buffer_size_tx = 2048;
	pConf->timeout_ms = 3000;
	pConf->keep_alive_enable = true;

	gc_dbg("Performing update");
	// Perform the update
	if ((status = esp_https_ota(pConf)) == ESP_OK) {
		gc_dbg("Update succeeded");
		csOtaTypeSet(csOtaType_remote);
	} else {
		gc_dbg("Update failed");
	}

	cs_heap_free(pCtrl);
	return status;
}


const char * csFwUpdateFailReason(esp_err_t errCode)
{
	switch (errCode)
	{
	case ESP_ERR_OTA_VALIDATE_FAILED:
		return "Invalid image";
	case ESP_ERR_NO_MEM:
		return "Not enough free memory";
	case ESP_ERR_INVALID_SIZE:
		return "URL string too large";
	case ESP_ERR_FLASH_OP_TIMEOUT:
	case ESP_ERR_FLASH_OP_FAIL:
		return "Flash write failed";
	case ESP_ERR_HTTP_CONNECT:
		return "HTTP error";
	default:
		return "General";
	}
}
