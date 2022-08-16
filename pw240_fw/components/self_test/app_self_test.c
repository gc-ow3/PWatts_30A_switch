/*
 * app_self_test.c
 *
 *  Created on: Apr 3, 2019
 *      Author: wesd
 */

#include "esp32/rom/crc.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_http_server.h"

#include "cs_platform.h"
#include "cs_control.h"
#include "cs_binhex.h"
#include "emtr_drv.h"
#include "cap1298_drv.h"
#include "app_led_mgr.h"
#include "outlet_mgr.h"
#include "mfg_data.h"
#include "app_self_test.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"app_self_test"
#include "mod_debug.h"


static void setCmdTab(csSelfTestCfg_t * cfg, void * cbData);


esp_err_t appSelfTestInit(appSelfTestCfg_t * cfg)
{
	if (!csSelfTestIsEnabled()) {
		return ESP_ERR_INVALID_STATE;
	}

	if (csSelfTestIsActive()) {
		return ESP_OK;
	}

	if (NULL == cfg) {
		return ESP_ERR_INVALID_ARG;
	}

	if (NULL == cfg->httpHandle) {
		gc_err("HTTP server not running");
		return ESP_ERR_INVALID_ARG;
	}

	// Configure the core self-test support
	csSelfTestCfg_t	csConf = {
		.httpHandle  = cfg->httpHandle,
		// No application-specific HTTP handlers
		.httpHandler = {
			.tab     = NULL,
			.tabSz   = 0,
			.cbData  = NULL
		},
	};

	// Attach the application command table
	setCmdTab(&csConf, NULL);

	esp_err_t	status;
	if ((status = csSelfTestInit(&csConf)) != ESP_OK) {
		gc_err("csSelfTestInit error %d", status);
		return status;
	}

	gc_dbg("Self-test interface is started");
	return ESP_OK;
}


esp_err_t appSelfTestTerm(void)
{
	gc_dbg("Self-test interface is stopped");
	csSelfTestTerm();
	return ESP_OK;
}


static void cmdReadInfo(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	const char *	reason = "";

	cJSON *	jData = cJSON_CreateObject();
	if (NULL == jData) {
		reason = "No memory";
		goto errExit;
	}

	// Build string version of MAC address
	char	macStr[20];
	int		len;

	len = snprintf(
		macStr, sizeof(macStr),
		"%02X:%02X:%02X:%02X:%02X:%02X",
		coreMfgData.macAddrBase[0],  coreMfgData.macAddrBase[1], coreMfgData.macAddrBase[2],
		coreMfgData.macAddrBase[3],  coreMfgData.macAddrBase[4], coreMfgData.macAddrBase[5]
	);
	if (len >= sizeof(macStr)) {
		reason = "MAC string too long";
		goto errExit;
	}

	// Add manufacturing information to the object
	cJSON_AddStringToObject(jData, "model", appInfo.model);
	cJSON_AddStringToObject(jData, "sn", coreMfgData.serialNum);
	cJSON_AddStringToObject(jData, "mac", macStr);
	cJSON_AddStringToObject(jData, "hwver_mcu", coreMfgData.hwVersion);
	cJSON_AddStringToObject(jData, "fwver_mcu", appFwVer);
	cJSON_AddStringToObject(jData, "fwver_emtr", emtrDrvGetFwVersion());


	csSelfTestSendOkResponse(req, "ok", jData);
	return;

errExit:
csSelfTestSendErrResponse(req, reason);
}


static void cmdReadSelfTest(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	if (0 == appSelfTest.failCount) {
		csSelfTestSendOkResponse(req, "ok", NULL);
		return;
	}

	// Build list of failed tests
	const char *	strList[4];
	int				strCount = 0;

	if (!appSelfTest.cap1298Present) {
		strList[strCount++] = "CAP";
	}

	if (!appSelfTest.emtrPresent) {
		strList[strCount++] = "EMTR";
	}

	cJSON *	jData = cJSON_CreateStringArray(strList, strCount);
	csSelfTestSendOkResponse(req, "fail", jData);
}


static void cmdReadTouch(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	// Read and report the state of the touch sensors
	uint8_t	bitMask = cap1298DrvActiveSensors();

	cJSON *	jData = cJSON_CreateObject();

	cJSON_AddBoolToObject(jData, "t1", (bitMask & CAP_TOUCH_PAD2) ? true : false);
	cJSON_AddBoolToObject(jData, "t2", (bitMask & CAP_TOUCH_PAD4) ? true : false);
	cJSON_AddBoolToObject(jData, "t3", (bitMask & CAP_TOUCH_PAD8) ? true : false);

	csSelfTestSendOkResponse(req, "ok", jData);
}


static void cmdReadTouchRegs(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	cJSON *	jArray = cJSON_CreateArray();

	int			pad;
	touchReg_t	regs;

	for (pad = 1; pad <= 8; pad++) {
		if (cap1298ReadTouchRegs(pad, &regs) == ESP_OK) {
			cJSON * jItem = cJSON_CreateObject();

			cJSON_AddNumberToObject(jItem, "pad", pad);
			cJSON_AddNumberToObject(jItem, "base_ct", regs.baseCount);
			cJSON_AddNumberToObject(jItem, "delta_ct", regs.deltaCount);

			cJSON_AddItemToArray(jArray, jItem);
		}
	}

	csSelfTestSendOkResponse(req, "ok", jArray);
}


static void cmdLedOn(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	ledMgrTurnLedOn(csLedNum_wifi);
	ledMgrTurnLedOn(csLedNum_status);
	ledMgrTurnLedOn(csLedNum_socket1);
	ledMgrTurnLedOn(csLedNum_socket2);

	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdLedOff(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	ledMgrTurnLedOff(csLedNum_wifi);
	ledMgrTurnLedOff(csLedNum_status);
	ledMgrTurnLedOff(csLedNum_socket1);
	ledMgrTurnLedOff(csLedNum_socket2);

	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdLedColorRed(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	ledMgrSetLedColor(csLedNum_wifi, csLedColor_red);
	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdLedColorGrn(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	ledMgrSetLedColor(csLedNum_wifi, csLedColor_green);
	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdLedColorBlu(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	ledMgrSetLedColor(csLedNum_wifi, csLedColor_blue);
	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdOutletOn(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	outletMgrSetSocket(1, true, callCtx_local, appDeltaSrc_internal);
	outletMgrSetSocket(2, true, callCtx_local, appDeltaSrc_internal);

	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdOutletOff(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	(void)jParam;

	outletMgrSetSocket(1, false, callCtx_local, appDeltaSrc_internal);
	outletMgrSetSocket(2, false, callCtx_local, appDeltaSrc_internal);

	csSelfTestSendOkResponse(req, "ok", NULL);
}


static void cmdReadOutlet(void * cbData, httpd_req_t * req, cJSON * jParam)
{
	cJSON *	jData = cJSON_CreateArray();

	int	i;
	for (i = 0; i < NUM_SOCKETS; i++) {
		int		oNum = 1 + i;

		// Create a sub-object
		cJSON *	jObj = cJSON_CreateObject();

		// Add outlet information to the object
		cJSON_AddNumberToObject(jObj, "outlet", oNum);
		cJSON_AddBoolToObject(jObj, "plug", emtrDrvIsPlugInserted(oNum));
		cJSON_AddBoolToObject(jObj, "on", emtrDrvIsSocketOn(oNum));

		// Add JSON object to the array
		cJSON_AddItemToArray(jData, jObj);
	}

	csSelfTestSendOkResponse(req, "ok", jData);
}


/**
 * \brief Table of actions and related handlers
 */
static csSelfTestCmdTab_t	cmdTab[] = {
	{
		.name           = "read-info",
		.handler        = cmdReadInfo,
		.paramsRequired = false
	},
	{
		.name           = "read-self-test",
		.handler        = cmdReadSelfTest,
		.paramsRequired = false
	},
	{
		.name           = "read-touch",
		.handler        = cmdReadTouch,
		.paramsRequired = false
	},
	{
		.name           = "read-touch-regs",
		.handler        = cmdReadTouchRegs,
		.paramsRequired = false
	},
	{
		.name           = "led-on",
		.handler        = cmdLedOn,
		.paramsRequired = false
	},
	{
		.name           = "led-off",
		.handler        = cmdLedOff,
		.paramsRequired = false
	},
	{
		.name           = "led-color-red",
		.handler        = cmdLedColorRed,
		.paramsRequired = false
	},
	{
		.name           = "led-color-grn",
		.handler        = cmdLedColorGrn,
		.paramsRequired = false
	},
	{
		.name           = "led-color-blu",
		.handler        = cmdLedColorBlu,
		.paramsRequired = false
	},
	{
		.name           = "outlet-off",
		.handler        = cmdOutletOff,
		.paramsRequired = false
	},
	{
		.name           = "outlet-on",
		.handler        = cmdOutletOn,
		.paramsRequired = false
	},
	{
		.name           = "read-outlet",
		.handler        = cmdReadOutlet,
		.paramsRequired = false
	},
};
static const int	cmdTabSz = sizeof(cmdTab) / sizeof(csSelfTestCmdTab_t);


static void setCmdTab(csSelfTestCfg_t * cfg, void * cbData)
{
	cfg->cmd.tab    = cmdTab;
	cfg->cmd.tabSz  = cmdTabSz;
	cfg->cmd.cbData = cbData;
}
