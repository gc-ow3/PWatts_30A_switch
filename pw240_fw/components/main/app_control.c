/*
 * app_master_task.c
 *
 *  Created on: Feb 7, 2019
 *      Author: wesd
 */

#include "cs_platform.h"
#include "cs_control.h"
#include "cs_framework.h"
#include "emtr_drv.h"
#include "app_control.h"
#include "app_self_test.h"
#include "event_callback.h"
#include "app_led_mgr.h"
#include "app_self_test.h"
#include "time_mgr.h"
#include "app_params.h"
#include "cs_local_ota.h"
#include "cs_ota_rollback.h"
#include "cs_prov_support.h"
#include "app_pw_api.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"app_ctrl"
#include "mod_debug.h"


#define HEAP_REPORT		(0)

////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

typedef enum {
	msgCode_null = 0,
	msgCode_signal
} msgCode_t;


typedef union {
	struct {
		csCtrlSignal_t	value;
	} signal;
} msgData_t;


typedef struct {
	msgCode_t		code;
	msgData_t		data;
} taskMsg_t;


typedef struct {
	bool			isStarted;
	TaskHandle_t	taskHandle;
	cbHandle_t		cbHandle;
	QueueHandle_t	queue;
	uint32_t		curTime;
} taskCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Forward references
////////////////////////////////////////////////////////////////////////////////

static void sysCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
);

static void frwkCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
);

static void notify(
	taskCtrl_t *		pCtrl,
	appCtrlEvtCode_t	evtCode,
	appCtrlEvtData_t *	evtData
);

static void appControlTask(void * param);

static esp_err_t sendMsg(taskMsg_t * msg);


////////////////////////////////////////////////////////////////////////////////
// Local data
////////////////////////////////////////////////////////////////////////////////

static taskCtrl_t *	taskCtrl;


////////////////////////////////////////////////////////////////////////////////
// Public Functions
////////////////////////////////////////////////////////////////////////////////


esp_err_t appControlInit(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	esp_err_t		status;

	if ((status = eventRegisterCreate(&pCtrl->cbHandle)) != ESP_OK) {
		return status;
	}

	if ((pCtrl->queue = xQueueCreate(8, sizeof(taskMsg_t))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	taskCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t appControlStart(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	UBaseType_t		ret;

	if (pCtrl->isStarted)
		return ESP_OK;

	ret = xTaskCreate(
		appControlTask,
		"appControl",
		4096,
		(void *)pCtrl,
		CS_TASK_PRIO_CONTROL,
		&pCtrl->taskHandle
	);
	if (pdTRUE != ret) {
		return ESP_FAIL;
	}

	// Attach a callback for system-level events
	csControlCallbackRegister(sysCallback, CS_PTR2ADR(pCtrl));

	// Attach a callback for framework events
	csFrameworkCallbackRegister(frwkCallback, CS_PTR2ADR(pCtrl));

	pCtrl->isStarted = true;
	return ESP_OK;
}


esp_err_t appControlCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventRegisterCallback(pCtrl->cbHandle, cbFunc, cbData);
}


esp_err_t appControlCallbackUnregister(eventCbFunc_t cbFunc)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventUnregisterCallback(pCtrl->cbHandle, cbFunc);
}



static esp_err_t _enable(
	const char *		paramKey,
	bool				value,
	appCtrlEvtCode_t	evtCode,
	callCtx_t			ctx
)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	appCtrlEvtData_t	eData;

	// Store the parameter
	paramMgrSetBool(paramKey, value);

	// Notify interested parties of the change
	eData.enabled = value;

	eventNotify(
		pCtrl->cbHandle,
		ctx,
		(uint32_t)evtCode,
		CS_PTR2ADR(&eData)
	);

	return ESP_OK;
}

esp_err_t appControlReportOnline(callCtx_t ctx)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	eventNotify(
		pCtrl->cbHandle,
		ctx,
		(uint32_t)appCtrlEvtCode_sendOnline,
		0
	);

	return ESP_OK;
}

esp_err_t appControlButtonEnable(bool value, callCtx_t ctx)
{
	return _enable(paramKey_buttonEnable, value, appCtrlEvtCode_buttonEnable, ctx);
}


esp_err_t appControlSignal(appCtrlSignal_t signal)
{
	taskMsg_t	msg = {
		.code = msgCode_signal,
		.data.signal.value = signal
	};

	return sendMsg(&msg);
}


////////////////////////////////////////////////////////////////////////////////
// Private Functions
////////////////////////////////////////////////////////////////////////////////

static void sysCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	//taskCtrl_t *		pCtrl      = CS_ADR2PTR(cbData);
	//csCtrlEvtData_t *	sysEvtData = CS_ADR2PTR(evtData);
	csLedColor_t		ledColor;

	switch ((csCtrlEvtCode_t)evtCode)
	{
	case csCtrlEvtCode_start:
		break;

	case csCtrlEvtCode_rebooting:
		ledMgrTurnLedOff(csLedNum_wifi);
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_green);
		ledMgrTurnLedOn(csLedNum_wifi);
		vTaskDelay(pdMS_TO_TICKS(1000));
		ledMgrTurnLedOff(csLedNum_wifi);
		break;

	case csCtrlEvtCode_resetProvision:
		// Application-level provision reset (if any)
		break;

	case csCtrlEvtCode_resetFactory:
		ledMgrTurnLedOff(csLedNum_wifi);
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_red);
		ledMgrTurnLedOn(csLedNum_wifi);
		vTaskDelay(pdMS_TO_TICKS(1000));
		ledMgrTurnLedOff(csLedNum_wifi);
		break;

	case csCtrlEvtCode_identify:
		// Blink the identification LED pattern
		ledColor = ledMgrGetLedColor(csLedNum_wifi);

		ledMgrSetLedColor(csLedNum_wifi, csLedColor_blue);
		ledMgrFlashLed(csLedNum_wifi, 250, 250, 10, 0);

		vTaskDelay(pdMS_TO_TICKS(5100));

		ledMgrSetLedColor(csLedNum_wifi, ledColor);
		break;

	default:
		return;
	}
}


static void frwkCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
)
{
	frmwkEvtCode_t	evtCode = (frmwkEvtCode_t)event;

	//taskCtrl_t *	pCtrl = CS_ADR2PTR(cbData);

	gc_dbg("Framework event: %s", csFrameworkEventStr(evtCode));

	switch (evtCode)
	{
	case frmwkEvtCode_staConnecting:
		// Fast yellow blink to indicate Wi-Fi connection in progress
		// If Wi-Fi connection succeeds, the event next expected will be
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_yellow);
		ledMgrFlashLed(csLedNum_wifi, 50, 50, 0, 0);
		break;

	case frmwkEvtCode_staConnected:
		// Do nothing - should proceed next to _staGotIp
		break;

	case frmwkEvtCode_staGotIp:
		if (csProvStateGet() == csProvState_provisioned) {
			ledMgrTurnLedOff(csLedNum_wifi);
			appPWApiStart();
		}
		break;

	case frmwkEvtCode_staDisconnect:
	case frmwkEvtCode_staLostIp:
		// Fast red blink to indicate Wi-Fi connection failed
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_red);
		ledMgrFlashLed(csLedNum_wifi, 50, 50, 0, 0);
		if (csProvStateGet() == csProvState_provisioned) {
			appPWApiStop();
		}
		break;

	case frmwkEvtCode_provStart:
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_yellow);
		ledMgrFlashLed(csLedNum_wifi, 500, 500, 0, 0);
		break;

	case frmwkEvtCode_provDone:
		ledMgrTurnLedOff(csLedNum_wifi);
		appPWApiStart();
		break;

	case frmwkEvtCode_provFail:
		ledMgrSetLedColor(csLedNum_wifi, csLedColor_yellow);
		ledMgrTurnLedOn(csLedNum_wifi);
		break;

	case frmwkEvtCode_sapStarted:
		if (csSelfTestIsEnabled()) {
			appSelfTestCfg_t	cfg = {
				.httpHandle = csFrameworkHttpdHandle()
			};
			appSelfTestInit(&cfg);

			// Enable local Over-The-Air firmware update
			if (csLocalOtaInit(csFrameworkHttpdHandle()) == ESP_OK) {
				csOtaType_t	otaType;
				if (csOtaUpdateIsPresent(&otaType)) {
					if (csOtaType_local == otaType || csOtaType_null == otaType) {
						// Received a local OTA update - mark it as valid
						csOtaUpdateAccept();
					}
				}
			}
		}
		break;

	default:
		// Ignore other events
		return;
	}
}

/**
 * \brief Send notification of an event to registered clients
 */
static void notify(taskCtrl_t * pCtrl, appCtrlEvtCode_t evtCode, appCtrlEvtData_t * evtData)
{
	eventNotify(
		pCtrl->cbHandle,
		callCtx_local,
		(uint32_t)evtCode,
		CS_PTR2ADR(evtData)
	);
}


static void handleSignal(taskCtrl_t * pCtrl, csCtrlSignal_t signal)
{

	switch (signal)
	{
	default:
		break;
	}
}


/**
 * \brief Application control task
 */
static void appControlTask(void * param)
{
	taskCtrl_t *	pCtrl = (taskCtrl_t *)param;
	taskMsg_t		msg;
	BaseType_t		xStatus;

#if CONFIG_IOT8020_DEBUG && HEAP_REPORT
	uint32_t	ramCheckTime = 0;
	int			intRamFree = 0;
	int			spiRamFree = 0;
#endif

	notify(pCtrl, appCtrlEvtCode_start, NULL);

	while (1)
	{
		xStatus = xQueueReceive(pCtrl->queue, &msg, pdMS_TO_TICKS(200));
		pCtrl->curTime = timeMgrGetUptime();

		if (pdTRUE != xStatus) {
#if CONFIG_IOT8020_DEBUG && HEAP_REPORT
			if (pCtrl->curTime >= ramCheckTime) {
				ramCheckTime = pCtrl->curTime + 30;

				int		curFree;

				curFree = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
				if (curFree != intRamFree) {
					gc_dbg("Free Internal SRAM: %9u (%5d)", curFree, intRamFree - curFree);
					intRamFree = curFree;
				}

				curFree = (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
				if (curFree != spiRamFree) {
					gc_dbg("Free SPI SRAM     : %9u (%5d)", curFree, spiRamFree - curFree);
					spiRamFree = curFree;
				}
			}
#endif

			continue;
		}

		// Handle the received message
		switch (msg.code)
		{
		case msgCode_signal:
			handleSignal(pCtrl, msg.data.signal.value);
			break;

		case msgCode_null:
			// Ignore
			break;

		default:
			gc_err("Event code (%d) not handled", msg.code);
			break;
		}
	}
}


/**
 * \brief Common internal logic for sending a message to the task
 */
static esp_err_t sendMsg(taskMsg_t * msg)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (!pCtrl->isStarted)
		return ESP_FAIL;

	if (xQueueSend(pCtrl->queue, msg, pdMS_TO_TICKS(100)) != pdTRUE)
		return ESP_FAIL;

	return ESP_OK;
}
