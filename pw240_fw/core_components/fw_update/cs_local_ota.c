/*
 * cs_local_ota.c
 *
 *  Created on: Jun 21, 2019
 *      Author: wesd
 */


#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_server.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "cJSON.h"
#include "esp_system.h"
#include "cs_local_ota.h"
#include "cs_ota_rollback.h"
#include "cs_common.h"
#include "cs_heap.h"
#include "cs_control.h"
#include "param_mgr.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"local_ota"
#include "mod_debug.h"

typedef enum {
	otaState_idle = 0,
	otaState_data,
	otaState_done,
	otaState_error
} otaState_t;

/**
 * \brief Status codes returned to HTTP client
 *
 * Do not change the existing values
 *
 */
typedef enum {
	otaStatus_idle       = 0,
	otaStatus_inProgress = 1,
	otaStatus_complete   = 2,
	otaStatus_error      = 3
} otaStatus_t;

typedef enum {
	otaFileType_null  = 0,
	otaFileType_mcu,
	otaFileType_emtr
} otaFileType_t;

typedef struct {
	httpd_handle_t			httpHandle;
	otaState_t				state;
	otaFileType_t			fileType;
	otaStatus_t				status;
	esp_ota_handle_t		otaHandle;
	const esp_partition_t *	otaPart;
	uint32_t				fileSz;
	uint32_t				rxCount;
	char					dataBuf[4096];
} otaCtrl_t;


static esp_err_t loadHttpHandlers(otaCtrl_t * pCtrl);
static esp_err_t unloadHttpHandlers(otaCtrl_t * pCtrl);


static otaCtrl_t *	otaCtrl;


/**
 * \brief Initialize local OTA firmware update
 *
 */
esp_err_t csLocalOtaInit(httpd_handle_t httpHandle)
{
	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	// Allocate and initialize the control structure
	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		gc_err("Failed to allocate control structure");
		return ESP_ERR_NO_MEM;
	}

	pCtrl->httpHandle = httpHandle;

	esp_err_t	status;

	status = loadHttpHandlers(pCtrl);
	if (ESP_OK == status) {
		gc_dbg("Local OTA update is ready");
		otaCtrl = pCtrl;
	} else {
		gc_err("Failed to start local OTA update support");
		cs_heap_free(pCtrl);
	}

	return status;
}


esp_err_t csLocalOtaTerm(void)
{
	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL == pCtrl)
		return ESP_OK;

	gc_dbg("Local OTA update stopped");

	// De-register HTTP handles
	(void)unloadHttpHandlers(pCtrl);

	// Release the control structure
	otaCtrl = NULL;
	cs_heap_free(pCtrl);
	return ESP_OK;
}


static esp_err_t getStartParams(otaCtrl_t * pCtrl, httpd_req_t * req)
{
	// Allocate a buffer large enough to receive the payload and space for terminator
	char *	buf   = pCtrl->dataBuf;
	int		bufSz = sizeof(pCtrl->dataBuf);

	esp_err_t	status = ESP_FAIL;

	if (req->content_len >= bufSz) {
		gc_err("HTTP payload too large (%d bytes) for buffer", req->content_len);
		goto exitStatus;
	}

	// Read the payload
	int	recvLen;
	recvLen = httpd_req_recv(req, buf, req->content_len);
	if (recvLen < req->content_len) {
		gc_err("HTTP recv length %d, expected %d", recvLen, req->content_len);
		goto exitStatus;
	}

	// Terminate the payload string
	buf[recvLen] = 0;

	gc_textDump("Payload", buf, recvLen);

	// Parse the JSON payload
	cJSON *	jRoot;
	if ((jRoot = cJSON_Parse(buf)) == NULL) {
		gc_err("Payload is not valid JSON");
		goto exitStatus;
	}

	// Check for optional file type
	const char *	typeStr;

	typeStr = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "file_type"));
	if (NULL == typeStr) {
		// Default to MCU image
		pCtrl->fileType = otaFileType_mcu;
	} else if (strcmp(typeStr, "mcu") == 0) {
		// Select MCU image
		pCtrl->fileType = otaFileType_mcu;
	} else if (strcmp(typeStr, "smcu") == 0) {
		// Select MCU image
		pCtrl->fileType = otaFileType_mcu;
	} else {
		gc_err("\"%s\" is not a recognized file type", typeStr);
		goto exitJson;
	}

	// Get the file size parameter
	cJSON *	jItem;

	if ((jItem = cJSON_GetObjectItem(jRoot, "file_size")) == NULL) {
		gc_err("Not found: \"file_size\"");
		goto exitJson;
	}
	if (!cJSON_IsNumber(jItem)) {
		gc_err("Not a Number: \"file_size\"");
		goto exitJson;
	}

	pCtrl->fileSz = (uint32_t)jItem->valueint;
	status = ESP_OK;

exitJson:
	cJSON_Delete(jRoot);
exitStatus:
	return status;
}


static esp_err_t handleUpdateStart(httpd_req_t * req)
{
	gc_dbg("handleUpdateStart");

	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	const char *	resp = HTTPD_200;
	esp_err_t		status;

	switch(pCtrl->state)
	{
	case otaState_idle:
	case otaState_done:
	case otaState_error:

		// Set up for the transfer

		// Get parameters
		gc_dbg("Get OTA parameters");
		status = getStartParams(pCtrl, req);
		if (ESP_OK != status) {
			pCtrl->status = otaStatus_error;
			resp = HTTPD_400;
			break;
		}

		if (otaFileType_mcu == pCtrl->fileType) {
			gc_dbg("Get next update partition");
			pCtrl->otaPart = esp_ota_get_next_update_partition(NULL);
			if (NULL == pCtrl->otaPart) {
				gc_err("  failed");
				pCtrl->status = otaStatus_error;
				resp = HTTPD_500;
				break;
			}

	#if 0
			gc_dbg("OTA part name   : %s", pCtrl->otaPart->label);
			gc_dbg("OTA part address: %u", pCtrl->otaPart->address);
			gc_dbg("OTA part size   : %u", pCtrl->otaPart->size);
	#endif

			gc_dbg("Begin OTA update");
			status = esp_ota_begin(pCtrl->otaPart, OTA_SIZE_UNKNOWN, &pCtrl->otaHandle);
			if (ESP_OK == status) {
				gc_dbg("Ready to receive data");
				pCtrl->rxCount = 0;
				pCtrl->status = otaStatus_inProgress;
				pCtrl->state  = otaState_data;
			} else {
				gc_err("esp_ota_begin error %x", status);
				pCtrl->status = otaStatus_error;
				resp = HTTPD_500;
			}
		} else {
			gc_err("Local OTA update for file type %d not supported", pCtrl->fileType);
			pCtrl->status = otaStatus_error;
			resp = HTTPD_400;
		}
		break;

	default:
		resp = HTTPD_500;
		break;
	}

	httpd_resp_set_status(req, resp);
	return httpd_resp_send(req, NULL, 0);
}


static esp_err_t handleUpdateData(httpd_req_t * req)
{
	gc_dbg("handleUpdateData (%u bytes)", req->content_len);

	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	const char *	resp = HTTPD_200;

	if (otaState_data != pCtrl->state) {
		resp = HTTPD_400;
		goto exitReq;
	}

	// Handle file data
	esp_err_t	status;
	uint32_t	rdCount;
	int			recvLen;

	for (rdCount = 0; rdCount < req->content_len; rdCount += recvLen) {
		recvLen = httpd_req_recv(req, pCtrl->dataBuf, sizeof(pCtrl->dataBuf));
		if (0 < recvLen) {
			status = esp_ota_write(pCtrl->otaHandle, pCtrl->dataBuf, recvLen);
			if (ESP_OK != status) {
				gc_err("esp_ota_write error %x", status);
				pCtrl->status = otaStatus_error;
				pCtrl->state  = otaState_error;
				resp = HTTPD_500;
				goto exitReq;
			}

			pCtrl->rxCount += recvLen;
		} else {
			gc_err("httpd_req_recv error %x", recvLen);
			pCtrl->status = otaStatus_error;
			pCtrl->state  = otaState_error;
			resp = HTTPD_500;
			goto exitReq;
		}
	}

	gc_dbg("Received %u bytes", pCtrl->rxCount);

	status = esp_ota_end(pCtrl->otaHandle);
	if (ESP_OK != status) {
		gc_err("esp_ota_end error %x", status);
		pCtrl->status = otaStatus_error;
		pCtrl->state  = otaState_error;
		resp = HTTPD_500;
		goto exitReq;
	}

	status = esp_ota_set_boot_partition(pCtrl->otaPart);
	if (ESP_OK == status) {
		csOtaTypeSet(csOtaType_local);
	} else {
		gc_err("esp_ota_set_boot_partition error %x", status);
		pCtrl->status = otaStatus_error;
		pCtrl->state  = otaState_error;
		resp = HTTPD_500;
		goto exitReq;
	}

	gc_dbg("OTA update completed");
	pCtrl->status = otaStatus_complete;
	pCtrl->state  = otaState_done;

exitReq:
	httpd_resp_set_status(req, resp);
	return httpd_resp_send(req, NULL, 0);
}


static esp_err_t handleUpdateFinish(httpd_req_t * req)
{
	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	bool			reboot = false;
	const char *	resp = HTTPD_200;

	switch (pCtrl->state)
	{
	case otaState_idle:
	case otaState_data:
		resp = HTTPD_400;
		break;

	case otaState_done:
		pCtrl->state  = otaState_idle;
		reboot = true;
		break;

	case otaState_error:
		pCtrl->state  = otaState_idle;
		pCtrl->status = otaStatus_idle;
		break;
	}

	httpd_resp_set_status(req, resp);
	(void)httpd_resp_send(req, NULL, 0);

	if (reboot) {
		csControlReboot(300, csRebootReason_fwUpdate);
	}

	return ESP_OK;
}


static esp_err_t handleUpdateStatus(httpd_req_t * req)
{
	otaCtrl_t *	pCtrl = otaCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	char	msgBuf[40];
	int		msgLen;

	msgLen = snprintf(
		msgBuf, sizeof(msgBuf),
		"{\"status\":%d, \"recv_bytes\":%u}",
		pCtrl->status, pCtrl->rxCount
	);

	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    return httpd_resp_send(req, msgBuf, msgLen);
}


/**
 * \brief Set of descriptors to HTTP handlers
 */
static const struct httpd_uri otaUpdateStart = {
	.uri     = "/cs-ota/start",
    .method  = HTTP_POST,
    .handler = handleUpdateStart
};
static const struct httpd_uri otaUpdateData = {
	.uri     = "/cs-ota/data",
    .method  = HTTP_POST,
    .handler = handleUpdateData
};
static const struct httpd_uri otaUpdateFinish = {
	.uri     = "/cs-ota/finish",
    .method  = HTTP_POST,
    .handler = handleUpdateFinish
};
static const struct httpd_uri otaUpdateStatus = {
	.uri     = "/cs-ota/status",
    .method  = HTTP_GET,
    .handler = handleUpdateStatus
};


// Table of pointers to handler descriptors
static const struct httpd_uri *	handlerDesc[] = {
	&otaUpdateStart,
	&otaUpdateData,
	&otaUpdateFinish,
	&otaUpdateStatus
};
static const int handlerTabSz = sizeof(handlerDesc) / (sizeof(struct httpd_uri *));


static esp_err_t loadHttpHandlers(otaCtrl_t * pCtrl)
{
	int			i;
	esp_err_t	status;

	for (i = 0; i < handlerTabSz; i++) {
		const struct httpd_uri *	desc = handlerDesc[i];

		status = httpd_register_uri_handler(pCtrl->httpHandle, desc);
		if (ESP_OK != status) {
			gc_err("Failed to register HTTP handler for \"%s\"", desc->uri);
			return status;
		}
	}

	return ESP_OK;
}


static esp_err_t unloadHttpHandlers(otaCtrl_t * pCtrl)
{
	int		i;
	int		errCt = 0;

	for (i = 0; i < handlerTabSz; i++) {
		const struct httpd_uri *	desc = handlerDesc[i];
		int							status;

		status = httpd_unregister_uri_handler(pCtrl->httpHandle, desc->uri, desc->method);
		if (ESP_OK != status) {
			gc_err("Failed to unregister HTTP handler for \"%s\"", desc->uri);
			errCt += 1;
			// Keep going
		}
	}

	return (0 == errCt) ? ESP_OK : ESP_FAIL;
}
