/*
 * app_master_task.c
 *
 *  Created on: Feb 7, 2019
 *      Author: wesd
 */

#include <esp_partition.h>
#include <esp_ota_ops.h>

#include "cs_common.h"
#include "cs_heap.h"
#include "mfg_data.h"
#include "param_mgr.h"
#include "time_mgr.h"
#include "cs_framework.h"
#include "cs_control.h"
#include "fw_update.h"
#include "cs_ota_rollback.h"
#include "app_pw_api.h"

#include "cs_prov_support.h"


// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cs_ctrl"
#include "mod_debug.h"

////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

typedef enum {
	msgCode_null = 0,
	msgCode_reboot,
	msgCode_signal,
	msgCode_setTz,
	msgCode_wifiUp,
	msgCode_wifiLost,
} msgCode_t;


typedef union {
	struct {
		uint32_t			delayMs;
		csRebootReason_t	reason;
	} reboot;
	struct {
		csCtrlSignal_t	value;
	} signal;
	struct {
		callCtx_t		ctx;
		const char *	name;
	} timeZone;
} msgData_t;


typedef struct {
	msgCode_t		code;
	msgData_t		data;
} taskMsg_t;

typedef enum {
	wifiState_init = 0,
	wifiState_connUp,
	wifiState_connLost
} wifiState_t;

typedef struct {
	bool				isStarted;
	TaskHandle_t		taskHandle;
	QueueHandle_t		queue;
	cbHandle_t			cbHandle;
	uint32_t			curTime;
	struct {
		bool		failed;
		uint32_t	timer;
	} fwUpdate;
	struct {
		uint32_t			delayMs;
		csRebootReason_t	reason;
	} reboot;
	struct {
		wifiState_t	state;
		uint32_t	timeOut;
		uint32_t	wifiTimeoutMs;//amount of time to reboot firmware if WiFi disconnected
	} wifi;
	multi_heap_info_t	prvHeap;
	multi_heap_info_t	curHeap;
} taskCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Forward references
////////////////////////////////////////////////////////////////////////////////

static void controlTask(void * param);

static esp_err_t sendMsg(taskMsg_t * msg);

static void frwkCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
);


////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////
static const char	nvsKey_rebootReason[]     = {"reboot_reason"};


////////////////////////////////////////////////////////////////////////////////
// Local data
////////////////////////////////////////////////////////////////////////////////

static taskCtrl_t *	taskCtrl;


////////////////////////////////////////////////////////////////////////////////
// Public Functions
////////////////////////////////////////////////////////////////////////////////


esp_err_t csControlInit(csControlConf_t * conf)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	esp_err_t		status;

	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	if ((status = eventRegisterCreate(&pCtrl->cbHandle)) != ESP_OK) {
		return status;
	}

	if ((pCtrl->queue = xQueueCreate(8, sizeof(taskMsg_t))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	if (0 == conf->wifiTimeoutMs) {
		// Default to 60 seconds
		pCtrl->wifi.wifiTimeoutMs = 60000;
	}
	else{
		pCtrl->wifi.wifiTimeoutMs = conf->wifiTimeoutMs;
	}

	pCtrl->fwUpdate.timer = 0xffffffff;

	taskCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t csControlStart(void)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isStarted)
		return ESP_OK;

	UBaseType_t		ret;

	ret = xTaskCreate(
		controlTask,
		"controlTask",
		8192,
		(void *)pCtrl,
		CS_TASK_PRIO_CONTROL,
		&pCtrl->taskHandle
	);
	if (pdTRUE != ret) {
		return ESP_FAIL;
	}

	// Attach a callback for framework events
	csFrameworkCallbackRegister(frwkCallback, CS_PTR2ADR(pCtrl));

	pCtrl->isStarted = true;
	return ESP_OK;
}


esp_err_t csControlCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventRegisterCallback(pCtrl->cbHandle, cbFunc, cbData);
}


esp_err_t csControlCallbackUnregister(eventCbFunc_t cbFunc)
{
	taskCtrl_t *	pCtrl = taskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventUnregisterCallback(pCtrl->cbHandle, cbFunc);
}


esp_err_t csControlReboot(uint32_t delayMs, csRebootReason_t reason)
{
	taskMsg_t	msg = {
		.code = msgCode_reboot,
		.data.reboot.delayMs = delayMs,
		.data.reboot.reason  = reason
	};

	return sendMsg(&msg);
}

esp_err_t csControlFactoryReset(void)
{
	taskMsg_t	msg = {
		.code = msgCode_signal,
		.data.signal.value = csCtrlSignal_resetFactory
	};

	return sendMsg(&msg);
}


esp_err_t csControlIdentify(void)
{
	taskMsg_t	msg = {
		.code = msgCode_signal,
		.data.signal.value = csCtrlSignal_identify
	};

	return sendMsg(&msg);
}


esp_err_t csControlSignal(csCtrlSignal_t signal)
{
	taskMsg_t	msg = {
		.code = msgCode_signal,
		.data.signal.value = signal
	};

	return sendMsg(&msg);
}


esp_err_t csControlSetTimeZone(const char * value, callCtx_t ctx)
{
	taskMsg_t	msg = {
		.code = msgCode_setTz,
		.data.timeZone.ctx  = ctx,
		.data.timeZone.name = value
	};

	return sendMsg(&msg);
}


const char * csBootReasonStr(csRebootReason_t code)
{
	switch (code)
	{
	case csRebootReason_null:
		return "null";
	case csRebootReason_command:
		return "command";
	case csRebootReason_user:
		return "user";
	case csRebootReason_fwUpdate:
		return "firmware_update";
	case csRebootReason_resetFactory:
		return "reset_factory";
	case csRebootReason_recovery:
		return "recovery";
	case csRebootReason_localApi:
		return "local_api";
	case csRebootReason_crash:
		return "crash_or_power_cycle";
	case csRebootReason_firstBoot:
		return "first_boot";
	default:
		return "undefined";
	}
}


static const char nvsSpace_bootLog[] = {"boot_log"};
static csRebootReason_t	lastBootReason;
static bool	lastBootReasonLoaded;


void csBootReasonSet(csRebootReason_t reason)
{
	lastBootReason = reason;

	nvs_handle	nvs;
	if (nvs_open(nvsSpace_bootLog, NVS_READWRITE, &nvs) == ESP_OK) {
		nvs_set_i16(nvs, nvsKey_rebootReason, (int16_t)reason);
		nvs_commit(nvs);
		nvs_close(nvs);
	}
}


const char * csBootReasonGet(void)
{
	if (!lastBootReasonLoaded) {
		lastBootReason = csRebootReason_null;

		nvs_handle	nvs;
		if (nvs_open(nvsSpace_bootLog, NVS_READWRITE, &nvs) == ESP_OK) {
			int16_t	value;
			if (nvs_get_i16(nvs, nvsKey_rebootReason, &value) == ESP_OK) {
				lastBootReason = (csRebootReason_t)value;
			} else {
				lastBootReason = csRebootReason_firstBoot;
			}

			// Write to NVS last boot reason of 'crash' because if the device
			// reboots for a non-scheduled purpose, it was because of
			// either power loss or crash
			nvs_set_i16(nvs, nvsKey_rebootReason, (int16_t)csRebootReason_crash);
			nvs_commit(nvs);
			nvs_close(nvs);

			lastBootReasonLoaded = true;
		}
	}

	return (const char *)csBootReasonStr(lastBootReason);
}


////////////////////////////////////////////////////////////////////////////////
// Private Functions
////////////////////////////////////////////////////////////////////////////////

/**
 * \brief Send notification of an event to registered clients
 */
static void notify(taskCtrl_t * pCtrl, csCtrlEvtCode_t evtCode, csCtrlEvtData_t * evtData)
{
	eventNotify(
		pCtrl->cbHandle,
		callCtx_local,
		(uint32_t)evtCode,
		CS_PTR2ADR(evtData)
	);
}



static void resetFactory(taskCtrl_t * pCtrl)
{
	gc_dbg("");
	gc_dbg(">>>>>>>> BLAMMO <<<<<<<<");
	gc_dbg("");

	csRebootReason_t	reason  = csRebootReason_resetFactory;
	uint32_t			delayMs = 250;

	// Reset local provisioning
	(void)csProvConfErase();

	gc_dbg("Reset parameters");

	// First reset the parameters
	(void)paramMgrReset();

	pCtrl->reboot.reason        = reason;

	// Notify interested parties
	csCtrlEvtData_t	eData = {
		.reboot = {
			.reason = reason
		}
	};

	notify(pCtrl, csCtrlEvtCode_rebooting, &eData);
	vTaskDelay(pdMS_TO_TICKS(delayMs));
}


static void checkForFwUpdateMcu(taskCtrl_t * pCtrl)
{

	if (pCtrl->fwUpdate.failed)
		return;

	if (pCtrl->fwUpdate.timer > pCtrl->curTime)
		return;

	// Notify interested parties of the change
	notify(pCtrl, csCtrlEvtCode_fwUpgradeStart, NULL);

	vTaskDelay(pdMS_TO_TICKS(200));

	esp_err_t	status = appPWApiOTA();

	if (ESP_OK == status) {
		gc_dbg("MCU firmware upgrade successful");

		notify(pCtrl, csCtrlEvtCode_fwUpgradeSuccess, NULL);
		vTaskDelay(pdMS_TO_TICKS(200));

		// Disable further update attempts
		pCtrl->fwUpdate.timer = 0xffffffff;

		// Schedule reboot
		csControlReboot(300, csRebootReason_fwUpdate);
	} else {
		gc_err("MCU firmware upgrade failed");
		pCtrl->fwUpdate.failed = true;

		csCtrlEvtData_t		eData;

		eData.fwUpgradeFail.reason = csFwUpdateFailReason(status);
		notify(pCtrl, csCtrlEvtCode_fwUpgradeFail, &eData);
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}

static void handleSignal(taskCtrl_t * pCtrl, csCtrlSignal_t signal)
{

	switch (signal)
	{
	case csCtrlSignal_identify:
		gc_dbg("Identify");
		notify(pCtrl, csCtrlEvtCode_identify, NULL);
		break;

	case csCtrlSignal_rebootArmed:
		// User is initiating some form of reboot/reset provision/reset factory
		// Tell interested parties
		notify(pCtrl, csCtrlEvtCode_rebootArmed, NULL);
		break;

	case csCtrlSignal_resetFactory:
		gc_dbg("Reset to factory defaults");

		// Notify interested parties of the pending reset/reboot
		notify(pCtrl, csCtrlEvtCode_resetFactory, NULL);

		// Delay to allow other processes that were notified to do cleanup
		vTaskDelay(pdMS_TO_TICKS(250));

		// Do the deed
		resetFactory(pCtrl);

		// Schedule the reboot
		csControlReboot(300, csRebootReason_resetFactory);
		break;

	case csCtrlSignal_fwUpgrade:
		// Check for update 1 second from now
		pCtrl->fwUpdate.failed  = false;
		pCtrl->fwUpdate.timer   = pCtrl->curTime + 1;
		break;

	default:
		break;
	}
}


static void handleMsg(taskCtrl_t * pCtrl, taskMsg_t * msg)
{
	csCtrlEvtData_t	evtData;

	switch (msg->code)
	{
	case msgCode_reboot:
		gc_dbg("Reboot scheduled");
		pCtrl->reboot.delayMs       = msg->data.reboot.delayMs;
		pCtrl->reboot.reason        = msg->data.reboot.reason;
		break;

	case msgCode_signal:
		handleSignal(pCtrl, msg->data.signal.value);
		break;

	case msgCode_setTz:
		// Notify interested parties of the change
		evtData.setTz.str = msg->data.timeZone.name;
		notify(pCtrl, csCtrlEvtCode_tzChange, &evtData);
		break;

	case msgCode_wifiUp:
		pCtrl->wifi.state   = wifiState_connUp;
		pCtrl->wifi.timeOut = 0;
		break;

	case msgCode_wifiLost:
		if (wifiState_connUp == pCtrl->wifi.state) {
			// Set timer to reboot in 60 seconds if connection is not re-established
			pCtrl->wifi.state   = wifiState_connLost;
			pCtrl->wifi.timeOut = timeMgrGetUptime() + (pCtrl->wifi.wifiTimeoutMs/1000);
		}
		break;

	case msgCode_null:
		// Ignore
		return;

	default:
		gc_err("Event code (%d) not handled", msg->code);
		return;
	}
}


static void checkHeap(taskCtrl_t * pCtrl)
{
#if MOD_DEBUG
	static uint32_t	checkTime = 0;

	uint32_t	heapFlags = CS_HEAP_FLAGS;
//	uint32_t	heapFlags = MALLOC_CAP_INTERNAL;

	if (0 == checkTime) {
		// First time through - synchronize
		heap_caps_get_info(&pCtrl->prvHeap, heapFlags);

		// Schedule first check
		checkTime = pCtrl->curTime + 10;
	}

	if (pCtrl->curTime < checkTime) {
		// Not yet time
		return;
	}

	// Schedule the next check time
	checkTime = pCtrl->curTime + 10;

	// Shorthand references to heap info
	multi_heap_info_t *	cur = &pCtrl->curHeap;
	multi_heap_info_t *	prv = &pCtrl->prvHeap;

	// Read current heap state
	heap_caps_get_info(cur, heapFlags);

	// Check for differences
	if (prv->total_free_bytes != cur->total_free_bytes) {
		int32_t	diff = (int32_t)cur->total_free_bytes - (int32_t)prv->total_free_bytes;

		gc_dbg("");
		gc_dbg("Heap report");
		gc_dbg("  Current free bytes : %8d", cur->total_free_bytes);
		gc_dbg("  Previous free bytes: %8d", prv->total_free_bytes);
		gc_dbg("  Difference         : %8d", diff);
		gc_dbg("  Minimum free bytes : %8d", cur->minimum_free_bytes);
		gc_dbg("");
	}

	// Synchronize
	*prv = *cur;

#endif
}


/**
 * \brief Application control task
 */
static void controlTask(void * param)
{
	taskCtrl_t *	pCtrl = (taskCtrl_t *)param;

	pCtrl->reboot.delayMs = 0;
	notify(pCtrl, csCtrlEvtCode_start, NULL);

	while (1)
	{
		if (pCtrl->reboot.delayMs > 0) {
			csRebootReason_t	reason = pCtrl->reboot.reason;

			gc_dbg("Rebooting because: %s", csBootReasonStr(reason));

			// Store the reason for the reboot
			csBootReasonSet(reason);

			gc_dbg("Reboot in %u MS", pCtrl->reboot.delayMs);

			// Notify interested parties
			csCtrlEvtData_t	eData = {
				.reboot = {
					.reason = reason
				}
			};
			notify(pCtrl, csCtrlEvtCode_rebooting, &eData);

			vTaskDelay(pdMS_TO_TICKS(pCtrl->reboot.delayMs));

			esp_restart();


			vTaskSuspend(NULL);
		}

		taskMsg_t	msg;
		BaseType_t	isMsg;

		vTaskDelay(pdMS_TO_TICKS(200));

		isMsg = xQueueReceive(pCtrl->queue, &msg, 0);

		// Get current up time (seconds counter)
		pCtrl->curTime = timeMgrGetUptime();

		if (isMsg) {
			// Handle the message
			handleMsg(pCtrl, &msg);
		}

		// Do periodic checks

		checkHeap(pCtrl);

		if (wifiState_connLost == pCtrl->wifi.state) {
			// Lost Wi-Fi connection, waiting to see if it will recover
			if (pCtrl->curTime >= pCtrl->wifi.timeOut) {
				// It didn't recover - force a reboot
				msg.code                = msgCode_reboot;
				msg.data.reboot.delayMs = 100;
				msg.data.reboot.reason  = csRebootReason_recovery;
				sendMsg(&msg);
			}
		}
		checkForFwUpdateMcu(pCtrl);
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

	if (xQueueSend(pCtrl->queue, msg, pdMS_TO_TICKS(10)) != pdTRUE)
		return ESP_FAIL;

	return ESP_OK;
}


static void frwkCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
)
{
	//taskCtrl_t *	pCtrl = CS_ADR2PTR(cbData);
	//frmwkEvtData_t *	evtData = CS_ADR2PTR(eventData);

	taskMsg_t		msg;

	switch ((frmwkEvtCode_t)event)
	{
	case frmwkEvtCode_staConnecting:
		break;

	case frmwkEvtCode_staConnected:
		break;

	case frmwkEvtCode_staDisconnect:
	case frmwkEvtCode_staLostIp:
		msg.code = msgCode_wifiLost;
		sendMsg(&msg);
		break;

	case frmwkEvtCode_staGotIp:
		if (csProvStateGet() == csProvState_provisioned) {
			msg.code = msgCode_wifiUp;
			sendMsg(&msg);
		}
		break;

	case frmwkEvtCode_provStart:
		break;

	case frmwkEvtCode_provDone:
		// Fully connected with IP address and fully provisioned
		msg.code = msgCode_wifiUp;
		sendMsg(&msg);
		break;

	case frmwkEvtCode_provFail:
		break;

	default:
		// Ignore other events
		return;
	}
}
