/*
 * cs_self_test.c
 *
 *  Created on: Feb 26, 2020
 *      Author: wesd
 */
#include <esp32/rom/crc.h>
#include <nvs.h>
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_err.h>

#include "cs_common.h"
#include "cs_heap.h"
#include "cs_control.h"
#include "cs_binhex.h"
#include "cs_self_test.h"
#include "mfg_data.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_self_test"
#include "mod_debug.h"


/*
********************************************************************************
* Data types
********************************************************************************
*/


typedef struct {
	csSelfTestCfg_t		conf;
	httpd_req_t *		httpReq;
} testCtrl_t;


/*
********************************************************************************
* Local functions
********************************************************************************
*/

static esp_err_t loadHttpHandlers(testCtrl_t * pCtrl);

static esp_err_t unloadHttpHandlers(testCtrl_t * pCtrl);


/*
********************************************************************************
* Constant data
********************************************************************************
*/
static const char	nvsNamespace_factoryTest[] = {"ns_factory_test"};
static const char	nvsKey_selfTestDisable[]   = {"disable"};


/*
********************************************************************************
* Local data
********************************************************************************
*/
static testCtrl_t *	testCtrl;


esp_err_t csSelfTestInit(csSelfTestCfg_t * cfg)
{
	testCtrl_t *	pCtrl = testCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if (!csSelfTestIsEnabled())
		return ESP_ERR_INVALID_STATE;

	if (NULL == cfg)
		return ESP_ERR_INVALID_ARG;

	if (NULL == cfg->httpHandle) {
		gc_err("HTTP server not running");
		return ESP_FAIL;
	}

	// Allocate and initialize the control structure
	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		gc_err("Failed to allocate control structure");
		return ESP_ERR_NO_MEM;
	}

	pCtrl->conf = *cfg;

	// Register HTTP endpoints
	if (loadHttpHandlers(pCtrl) != ESP_OK)
		goto exitMem;

	testCtrl = pCtrl;
	return ESP_OK;

exitMem:
	cs_heap_free(pCtrl);
	return ESP_FAIL;
}


esp_err_t csSelfTestTerm(void)
{
	testCtrl_t *	pCtrl = testCtrl;
	if (NULL == pCtrl)
		return ESP_OK;

	// Unregister HTTP endpoints
	(void)unloadHttpHandlers(pCtrl);

	testCtrl = NULL;
	cs_heap_free(pCtrl);
	return ESP_OK;
}


bool csSelfTestIsActive(void)
{
	return (NULL == testCtrl) ? false : true;
}


bool csSelfTestIsEnabled(void)
{

	// Open the factory test namespace
	nvs_handle	nvs;
	if (nvs_open(nvsNamespace_factoryTest, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_dbg("nvs namespace (%s) not found", nvsNamespace_factoryTest);
		return true;
	}

	// Check for the factory test disable flag
	// Value doesn't matter, testing for the presence
	esp_err_t	status;
	int8_t		value;
	status = nvs_get_i8(nvs, nvsKey_selfTestDisable, &value);
	nvs_close(nvs);

	return (ESP_OK == status) ? false : true;
}


esp_err_t csSelfTestDisable(void)
{
	// Open the factory test namespace
	nvs_handle	nvs;
	if (nvs_open(nvsNamespace_factoryTest, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		return ESP_FAIL;
	}

	// Check for the factory test disable flag
	// Value doesn't matter, testing for the presence
	int8_t	value;
	if (nvs_get_i8(nvs, nvsKey_selfTestDisable, &value) != ESP_OK) {
		// Set the flag
		value = 1;
		(void)nvs_set_i8(nvs, nvsKey_selfTestDisable, value);
		nvs_commit(nvs);
	}

	nvs_close(nvs);
	return ESP_OK;
}

esp_err_t csSelfTestEnable(void)
{
	esp_err_t status;
	// Open the factory test namespace
	nvs_handle	nvs;
	if (nvs_open(nvsNamespace_factoryTest, NVS_READWRITE, &nvs) != ESP_OK) {
		gc_err("nvs_open failed");
		return ESP_FAIL;
	}

	// Delete the factory test disable flag
	// Value doesn't matter, testing for the presence
	status = nvs_erase_key(nvs, nvsKey_selfTestDisable);
	if(status == ESP_ERR_NVS_NOT_FOUND)
		status = ESP_OK;

	nvs_close(nvs);
	return status;
}



void csSelfTestSendOkResponse(httpd_req_t * pReq, const char * resp, cJSON * jData)
{
	cJSON *	jRoot;

	if ((jRoot = cJSON_CreateObject()) == NULL) {
		gc_err("Failed to allocate JSON object");
		return;
	}

	cJSON_AddStringToObject(jRoot, "result", resp);

	if (NULL != jData) {
		cJSON_AddItemReferenceToObject(jRoot, "data", jData);
	}

	char *	msgBuf;

	if ((msgBuf = cJSON_PrintUnformatted(jRoot)) == NULL) {
		gc_err("Failed to build JSON payload");
		goto exitObj;
	}

	int	msgLen = strlen(msgBuf);

	gc_textDump("Response", msgBuf, msgLen);

	httpd_resp_set_status(pReq, HTTPD_200);
	httpd_resp_set_type(pReq, HTTPD_TYPE_JSON);
    httpd_resp_send(pReq, msgBuf, msgLen);

	cJSON_free(msgBuf);
exitObj:
	cJSON_Delete(jRoot);
}


void csSelfTestSendErrResponse(httpd_req_t * pReq, const char * reason)
{
	cJSON *	jRoot;

	if ((jRoot = cJSON_CreateObject()) == NULL) {
		gc_err("Failed to allocate JSON object");
		return;
	}

	cJSON_AddStringToObject(jRoot, "result", "error");

	if (NULL != reason)
		cJSON_AddStringToObject(jRoot, "reason", reason);

	char *	msgBuf;

	if ((msgBuf = cJSON_PrintUnformatted(jRoot)) == NULL) {
		gc_err("Failed to build JSON payload");
		goto exitObj;
	}

	int	msgLen = strlen(msgBuf);

	gc_textDump("Error Response", msgBuf, msgLen);

	httpd_resp_set_status(pReq, HTTPD_200);
	httpd_resp_set_type(pReq, HTTPD_TYPE_JSON);
    httpd_resp_send(pReq, msgBuf, msgLen);

	cJSON_free(msgBuf);
exitObj:
	cJSON_Delete(jRoot);
}


static bool checkCoreCmds(httpd_req_t * req, const char * cmd, cJSON * jParam)
{
	bool	ret = true;

	if (strcmp(cmd, "identify") == 0) {
		csControlIdentify();
		csSelfTestSendOkResponse(req, "ok", NULL);
	} else if (strcmp(cmd, "reboot") == 0) {
		csSelfTestSendOkResponse(req, "ok", NULL);
		csControlReboot(200, csRebootReason_command);
	} else if (strcmp(cmd, "disable-test") == 0) {
		mfgDataEnable();
		if (csSelfTestDisable() == ESP_OK) {
			csSelfTestSendOkResponse(req, "ok", NULL);
			csControlReboot(200, csRebootReason_command);
		} else {
			csSelfTestSendErrResponse(req, "Failed to disable factory test");
		}
	} else {
		// Not a core command
		ret = false;
	}

	return ret;
}

/**
 * \brief Process a factory test payload
 *
 * Payload is expected to a JSON object of the form
 * {
 *   "cmd":"<command>",
 *   "params":{
 *     <command-dependant commands>
 *   }
 * }
 *
 * The required presence of the params object depends on the command
 *
 */
static void procMfgPayload(testCtrl_t * pCtrl, const char * payload)
{
	const char *	reason;

	cJSON *		jRoot = cJSON_Parse(payload);
	if (NULL == jRoot) {
		reason = "Payload is not proper JSON";
		goto exitError;
	}

	// Look for "tls_key" keyword
	char *	tls_key_b64;
	if ((tls_key_b64 = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "tls_key"))) == NULL) {
		reason = "Missing item: 'tls_key'";
		goto exitError;
	}

	// Look for "tls_cert" keyword
	char *	tls_cert_b64;
	if ((tls_cert_b64 = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "tls_cert"))) == NULL) {
		reason = "Missing item: 'tls_cert'";
		goto exitError;
	}

	// Look for "mac_addr_base" keyword
	char *	mac_addr_base;
	if ((mac_addr_base = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "mac_addr_base"))) == NULL) {
		reason = "Missing item: 'mac_addr_base'";
		goto exitError;
	}

	// Look for "serial_num" keyword
	char *	serial_num;
	if ((serial_num = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "serial_num"))) == NULL) {
		reason = "Missing item: 'serial_num'";
		goto exitError;
	}

	// Look for hw_version, optional
	char *	hw_version;
	hw_version = cJSON_GetStringValue(cJSON_GetObjectItem(jRoot, "hw_version"));


	// Look for prod_metadata, optional
	char * prod_metadata=NULL;
	cJSON *	jProdMeta= cJSON_GetObjectItem(jRoot, "prod_metadata");
	if(jProdMeta)
		prod_metadata = cJSON_PrintUnformatted(jProdMeta);

	/**Save values to RAM**/
	//prod_metadata
	if(prod_metadata){
		if(coreMfgData.prod_metadata)
			cs_heap_free(coreMfgData.prod_metadata);
		coreMfgData.prod_metadata = cs_heap_calloc(1, strlen(prod_metadata)+1);
		sprintf(coreMfgData.prod_metadata, prod_metadata);
		cJSON_free(prod_metadata);
	}
	//hwVersion
	if(hw_version){
		snprintf(coreMfgData.hwVersion,8,hw_version);
	}

	//tlsKeyB64
	if(coreMfgData.tlsKeyB64)
		cs_heap_free(coreMfgData.tlsKeyB64);
	coreMfgData.tlsKeyB64 = cs_heap_calloc(1, strlen(tls_key_b64)+1);
	sprintf(coreMfgData.tlsKeyB64, tls_key_b64);
	//tlsCertB64
	if(coreMfgData.tlsCertB64)
		cs_heap_free(coreMfgData.tlsCertB64);
	coreMfgData.tlsCertB64 = cs_heap_calloc(1, strlen(tls_cert_b64)+1);
	sprintf(coreMfgData.tlsCertB64, tls_cert_b64);
	//serialNum
	sprintf(coreMfgData.serialNum, serial_num);
	//macAddrBase
	char temp_string[3] = { 0 };
	for (int i = 0; i < 6; i++) {
		memcpy(temp_string, &mac_addr_base[i * 3], 2);
		coreMfgData.macAddrBase[i] = strtol(temp_string, NULL, 16);
	}

	if(mfgDataSave()==ESP_OK){
		mfgDataEnable();
		csSelfTestSendOkResponse(pCtrl->httpReq, "ok", NULL);
		goto exitJson;
	}
	else{
		reason = "NVS Error";
		goto exitError;
	}
exitError:
	csSelfTestSendErrResponse(pCtrl->httpReq, reason);
exitJson:
	if (jRoot) {
		cJSON_Delete(jRoot);
	}
}

/**
 * \brief Process a factory test payload
 *
 * Payload is expected to a JSON object of the form
 * {
 *   "cmd":"<command>",
 *   "params":{
 *     <command-dependant commands>
 *   }
 * }
 *
 * The required presence of the params object depends on the command
 *
 */
static void procCmdPayload(testCtrl_t * pCtrl, const char * payload)
{
	const char *	reason;

	cJSON *		jRoot = cJSON_Parse(payload);
	if (NULL == jRoot) {
		reason = "Payload is not proper JSON";
		goto exitError;
	}

	// Look for "test" object
	cJSON *	jTest;
	if ((jTest = cJSON_GetObjectItem(jRoot, "test")) == NULL) {
		reason = "Missing object: 'test'";
		goto exitError;
	}

	// Look for "cmd" keyword
	char *	action;
	if ((action = cJSON_GetStringValue(cJSON_GetObjectItem(jTest, "cmd"))) == NULL) {
		reason = "Missing item: 'cmd'";
		goto exitError;
	}

	// Look for params, optional depending on the action
	cJSON *	jParam = cJSON_GetObjectItem(jTest, "params");

	// First check for core commands
	if (checkCoreCmds(pCtrl->httpReq, action, jParam)) {
		// Core command was handled, response was sent
		goto exitJson;
	}

	// Now check for application-level commands

	// Shorthand reference to application-specific command table
	csSelfTestCmdTab_t *	cmdTab   = pCtrl->conf.cmd.tab;
	int						cmdTabSz = pCtrl->conf.cmd.tabSz;
	void *					cbData   = pCtrl->conf.cmd.cbData;

	// Find and call the handler for this action
	csSelfTestCmdTab_t *	tab;
	int				i;
	for (i = 0, tab = cmdTab; i < cmdTabSz; i++, tab++) {
		if (strcmp(action, tab->name) == 0) {
			// Found the handler, check if params is required
			if (tab->paramsRequired && NULL == jParam) {
				reason = "Missing required 'params'";
				goto exitError;
			} else {
				// Okay to proceed
				gc_dbg("Executing \"%s\"", action);
				tab->handler(cbData, pCtrl->httpReq, jParam);
				goto exitJson;
			}
		}
	}

	// If this point is reached, the action handler was not found
	gc_err("command \"%s\" not supported", action);
	reason = "cmd not supported";

exitError:
	csSelfTestSendErrResponse(pCtrl->httpReq, reason);
exitJson:
	if (jRoot) {
		cJSON_Delete(jRoot);
	}
}


/*
********************************************************************************
********************************************************************************
** HTTP Handlers
********************************************************************************
********************************************************************************
*/


static int readPayload(httpd_req_t * req, uint8_t * buf, int len)
{
	int		rdCount;
	int		recvLen;

	for (rdCount = 0; rdCount < len; rdCount += recvLen) {
		recvLen = httpd_req_recv(req, ((char *)buf + rdCount), (len - rdCount));
		if (recvLen <= 0) {
			// Premature end of transfer
			return -1;
		}
	}

	return rdCount;
}


static esp_err_t handleTestCmd(httpd_req_t * req)
{
	testCtrl_t *	pCtrl = req->user_ctx;
	pCtrl->httpReq = req;

	int	contentLen = req->content_len;

	gc_dbg("readCmdData (%u bytes)", contentLen);

	const char *	resp;
	int				dataBufSz = contentLen + 1;
	char *			dataBuf   = cs_heap_malloc(dataBufSz);

	if (NULL == dataBuf) {
		gc_err("Failed to allocate %d bytes", dataBufSz);
		resp = HTTPD_500;
		goto errorExit;
	}

	// Read the payload
	int		rdLen;
	rdLen = readPayload(req, (uint8_t *)dataBuf, contentLen);
	if (rdLen <= 0) {
		resp = HTTPD_400;
		goto errorExit;
	}

	// Null-terminate the payload and process it
	*(dataBuf + rdLen) = '\0';

	// The HTTP response will be sent from this function
	procCmdPayload(pCtrl, dataBuf);

	cs_heap_free(dataBuf);
	return ESP_OK;

errorExit:
	httpd_resp_set_status(pCtrl->httpReq, resp);
	httpd_resp_send(pCtrl->httpReq, NULL, 0);
	if (dataBuf)
		cs_heap_free(dataBuf);
	return ESP_OK;
}


static esp_err_t handleWriteMfg(httpd_req_t * req)
{
	testCtrl_t *	pCtrl = req->user_ctx;
	pCtrl->httpReq = req;

	int	contentLen = req->content_len;

	gc_dbg("readMfgData (%u bytes)", contentLen);

	const char *	resp;
	int				dataBufSz = contentLen + 1;
	char *			dataBuf   = cs_heap_malloc(dataBufSz);

	if (NULL == dataBuf) {
		gc_err("Failed to allocate %d bytes", dataBufSz);
		resp = HTTPD_500;
		goto errorExit;
	}

	// Read the payload
	int		rdLen;
	rdLen = readPayload(req, (uint8_t *)dataBuf, contentLen);
	if (rdLen <= 0) {
		resp = HTTPD_400;
		goto errorExit;
	}

	// Null-terminate the payload and process it
	*(dataBuf + rdLen) = '\0';

	// The HTTP response will be sent from this function
	procMfgPayload(pCtrl, dataBuf);

	cs_heap_free(dataBuf);
	return ESP_OK;

errorExit:
	httpd_resp_set_status(pCtrl->httpReq, resp);
	httpd_resp_send(pCtrl->httpReq, NULL, 0);
	if (dataBuf)
		cs_heap_free(dataBuf);
	return ESP_OK;
}

static esp_err_t handleReadMfg(httpd_req_t * req)
{
	testCtrl_t *	pCtrl = req->user_ctx;
	pCtrl->httpReq = req;

	const char *	resp_code = HTTPD_200;

	cJSON *	jRoot;

	if ((jRoot = cJSON_CreateObject()) == NULL) {
		gc_err("Failed to allocate JSON object");
		resp_code = HTTPD_500;
		goto errorExit;
	}

	char	macStr[20]={0};
	int		len;

	len = snprintf(
		macStr, sizeof(macStr),
		"%02X:%02X:%02X:%02X:%02X:%02X",
		coreMfgData.macAddrBase[0],  coreMfgData.macAddrBase[1], coreMfgData.macAddrBase[2],
		coreMfgData.macAddrBase[3],  coreMfgData.macAddrBase[4], coreMfgData.macAddrBase[5]
	);
	if (len >= sizeof(macStr)) {
		resp_code = HTTPD_500;
		goto errorExit;
	}


	if(coreMfgData.tlsKeyB64)
		cJSON_AddStringToObject(jRoot, "tls_key", coreMfgData.tlsKeyB64);
	if(coreMfgData.tlsCertB64)
		cJSON_AddStringToObject(jRoot, "tls_cert", coreMfgData.tlsCertB64);
	cJSON_AddStringToObject(jRoot, "mac_addr_base", macStr);
	cJSON_AddStringToObject(jRoot, "serial_num", coreMfgData.serialNum);
	cJSON_AddStringToObject(jRoot, "hw_version", coreMfgData.hwVersion);
	if(coreMfgData.prod_metadata)
		cJSON_AddStringToObject(jRoot, "prod_metadata", coreMfgData.prod_metadata);

	char *	msgBuf;

	if ((msgBuf = cJSON_PrintUnformatted(jRoot)) == NULL) {
		gc_err("Failed to build JSON payload");
		goto exitObj;
	}

	int	msgLen = strlen(msgBuf);

	gc_textDump("Response", msgBuf, msgLen);


	httpd_resp_set_status(pCtrl->httpReq, HTTPD_200);
	httpd_resp_set_type(pCtrl->httpReq, HTTPD_TYPE_JSON);
    httpd_resp_send(pCtrl->httpReq, msgBuf, msgLen);

	cJSON_Delete(jRoot);
	cJSON_free(msgBuf);
	return ESP_OK;

exitObj:
	cJSON_Delete(jRoot);
errorExit:
	httpd_resp_set_status(pCtrl->httpReq, resp_code);
	httpd_resp_send(pCtrl->httpReq, NULL, 0);
	return ESP_OK;

}

// Table of HTTP handler descriptors
static const struct httpd_uri csHandlerTab[] = {
	{
		.uri     = "/cs-test/cmd",
	    .method  = HTTP_POST,
	    .handler = handleTestCmd
	},
	{
		.uri     = "/v1/write-mfg-json",
	    .method  = HTTP_POST,
	    .handler = handleWriteMfg
	},
	{
		.uri     = "/v1/read-mfg-json",
	    .method  = HTTP_GET,
	    .handler = handleReadMfg
	},

};
static const int csHandlerTabSz = sizeof(csHandlerTab) / (sizeof(struct httpd_uri));


static esp_err_t loadHttpHandlers(testCtrl_t * pCtrl)
{
	// Shorthand to the configuration structure
	csSelfTestCfg_t *	cfg = &pCtrl->conf;

	int							i;
	const struct httpd_uri *	tab;

	// Load the core handlers
	for (i = 0, tab = csHandlerTab; i < csHandlerTabSz; i++, tab++) {
		// Make a local copy of the descriptor
		struct httpd_uri	desc = *tab;

		// Attach our control structure to the descriptor
		desc.user_ctx = pCtrl;

		// Register the handler
		esp_err_t	status;
		status = httpd_register_uri_handler(cfg->httpHandle, &desc);
		if (ESP_OK != status) {
			gc_err("Failed to register HTTP handler for \"%s\"", desc.uri);
			return status;
		}
	}

	// Load any application-specific handlers
	for (i = 0, tab = cfg->httpHandler.tab; i < cfg->httpHandler.tabSz; i++, tab++) {
		// Make a local copy of the descriptor
		struct httpd_uri	desc = *tab;

		// Attach application callback data to the descriptor
		desc.user_ctx = cfg->httpHandler.cbData;

		// Register the handler
		esp_err_t	status;
		status = httpd_register_uri_handler(cfg->httpHandle, &desc);
		if (ESP_OK != status) {
			gc_err("Failed to register HTTP handler for \"%s\"", desc.uri);
			return status;
		}
	}

	return ESP_OK;
}


static esp_err_t unloadHttpHandlers(testCtrl_t * pCtrl)
{
	// Shorthand to the configuration structure
	csSelfTestCfg_t *	cfg = &pCtrl->conf;

	int							i;
	const struct httpd_uri *	tab;
	int							errCt = 0;

	// Unload core handlers
	for (i = 0, tab = csHandlerTab; i < csHandlerTabSz; i++, tab++) {
		int	status;

		status = httpd_unregister_uri_handler(cfg->httpHandle, tab->uri, tab->method);
		if (ESP_OK != status) {
			gc_err("Failed to unregister HTTP handler for \"%s\"", tab->uri);
			errCt += 1;
			// Keep going
		}
	}

	// Unload application-specific handlers, if any
	for (i = 0, tab = cfg->httpHandler.tab; i < cfg->httpHandler.tabSz; i++, tab++) {
		int	status;

		status = httpd_unregister_uri_handler(cfg->httpHandle, tab->uri, tab->method);
		if (ESP_OK != status) {
			gc_err("Failed to unregister HTTP handler for \"%s\"", tab->uri);
			errCt += 1;
			// Keep going
		}
	}

	return (0 == errCt) ? ESP_OK : ESP_FAIL;
}
