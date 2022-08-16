/*
 * cap1298_handler.c
 *
 *  Created on: Jan 24, 2019
 *      Author: wesd
 */
#include "app_led_mgr.h"
#include "cs_common.h"
#include "cs_platform.h"
#include "cs_heap.h"
#include "cap1298_handler.h"
#include "emtr_drv.h"
#include "outlet_mgr.h"
#include "time_mgr.h"
#include "cs_control.h"
#include "app_led_mgr.h"
#include "app_params.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"cap_handler"
#include "mod_debug.h"

#define NUM_BUTTONS		(3)


#define EXPIRE_TIME_IDLE			( 1500)
#define HOLD_TIME_REBOOT_MS			( 5000)
#define HOLD_TIME_RESET_FACT_MS		(15000)


typedef struct {
	capDrvSource_t	src;
	const char *	name;
} const buttonInfo_t;


typedef enum {
	msgCode_null = 0,
	msgCode_press,
	msgCode_release
} msgCode_t;


typedef union {
	struct {
		capDrvSource_t	id;
	} source;
} msgData_t;


typedef struct {
	msgCode_t		code;
	msgData_t		data;
} taskMsg_t;

typedef enum {
	resetState_idle = 0,
	resetState_armed,
	resetState_reboot,
	resetState_factory
} resetState_t;

typedef struct {
	bool			isStarted;
	TaskHandle_t	taskHandle;
	QueueHandle_t	queue;
	uint32_t		curTime;
	uint32_t		pressTime;
	bool			isPressed;
	int				tapCount;
	buttonInfo_t *	activeButton;
	resetState_t	resetState;
} taskCtrl_t;


static void capControlTask(void * param);

static void capCallback(uint32_t cbData, capDrvEvt_t evtCode, capDrvSource_t src);

static void emtrCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	eventData
);

static buttonInfo_t buttonInfo[NUM_BUTTONS] = {
	{
		.src   = capDrvSource_A,
		.name  = "A",
	},
	{
		.src   = capDrvSource_B,
		.name  = "B",
	},
	{
		.src   = capDrvSource_center,
		.name  = "Center",
	},
};

static taskCtrl_t *	pTaskCtrl;


esp_err_t cap1298HandlerInit(const cap1298DrvConf_t * conf)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	if ((pCtrl->queue = xQueueCreate(8, sizeof(taskMsg_t))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	esp_err_t	status;

	// CAP1298 driver
	if ((status = cap1298DrvInit(conf)) != ESP_OK) {
		gc_err("cap1298DrvInit error %d", status);
		return status;
	}

	pTaskCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t cap1298HandlerStart(void)
{
	taskCtrl_t *	pCtrl = pTaskCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isStarted)
		return ESP_OK;

	esp_err_t	status;

	// Start the CAP1298 driver, attaching the callback function
	if ((status = cap1298DrvStart(capCallback, CS_PTR2ADR(pCtrl))) != ESP_OK) {
		gc_err("cap1298DrvStart error %d", status);
		return status;
	}

	UBaseType_t		ret;

	ret = xTaskCreate(
		capControlTask,
		"capControlTask",
		2048,
		(void *)pCtrl,
		TASK_PRIO_CAP1298_HNDLR,
		&pCtrl->taskHandle
	);
	if (pdTRUE != ret) {
		return ESP_FAIL;
	}

	// Register to receive Outlet Mgr events
	emtrDrvCallbackRegister(emtrCbId_socket1, emtrCallback, CS_PTR2ADR(pCtrl));
	emtrDrvCallbackRegister(emtrCbId_socket2, emtrCallback, CS_PTR2ADR(pCtrl));

	pCtrl->isStarted = true;
	return ESP_OK;
}


static buttonInfo_t * getButtonInfo(taskCtrl_t * pCtrl, capDrvSource_t btnId)
{
	int				idx;
	buttonInfo_t *	info;

	for (idx = 0, info = buttonInfo; idx < NUM_BUTTONS; idx++, info++) {
		if (info->src == btnId)
			return info;
	}

	// Not found
	gc_err("Button id (%d) not supported", btnId);
	return NULL;
}


static void handlePress(taskCtrl_t * pCtrl, capDrvSource_t btnId)
{
	buttonInfo_t *	info;

	//gc_dbg("handlePress (%d)", btnId);

	if ((info = getButtonInfo(pCtrl, btnId)) == NULL) {
		return;
	}

	if (!pCtrl->isPressed) {
		pCtrl->isPressed = true;
		ledMgrTurnLedOn(csLedNum_status);
	}

	if (pCtrl->activeButton != info) {
		// Switch to a different button
		pCtrl->activeButton   = info;
		pCtrl->tapCount       = 0;
		pCtrl->resetState     = resetState_idle;
	}

	pCtrl->pressTime = pCtrl->curTime;
}


static void handleRelease(taskCtrl_t * pCtrl, capDrvSource_t btnId)
{
	buttonInfo_t *	info;

	if ((info = getButtonInfo(pCtrl, btnId)) == NULL) {
		return;
	}

	if (pCtrl->isPressed) {
		pCtrl->isPressed = false;
		ledMgrTurnLedOff(csLedNum_status);
	}

	if (pCtrl->activeButton != info) {
		// Spurious release
		pCtrl->activeButton = NULL;
		return;
	}

	pCtrl->tapCount += 1;

	switch (btnId)
	{
	case capDrvSource_A:
		if (pCtrl->tapCount == 2) {
			//gc_dbg("Toggle socket A");
			outletMgrSetSocket(
				1,
				!emtrDrvIsSocketOn(1),
				callCtx_local,
				appDeltaSrc_user
			);
			pCtrl->activeButton = NULL;
		}
		break;

	case capDrvSource_B:
		if (pCtrl->tapCount == 2) {
			//gc_dbg("Toggle socket B");
			outletMgrSetSocket(
				2,
				!emtrDrvIsSocketOn(2),
				callCtx_local,
				appDeltaSrc_user
			);
			pCtrl->activeButton = NULL;
		}
		break;

	case capDrvSource_center:
		switch (pCtrl->resetState)
		{
		case resetState_idle:
			if (3 == pCtrl->tapCount) {
				//gc_dbg("Reset function armed");
				pCtrl->resetState = resetState_armed;
			}
			break;

		case resetState_armed:
			gc_dbg("Reset function canceled");
			pCtrl->activeButton = NULL;
			break;

		case resetState_reboot:
			pCtrl->activeButton = NULL;
			//gc_dbg("Reboot");

			// If the button input is disabled, re-enable
			if (!appParams.buttonsEnabled)
				paramMgrSetBool(paramKey_buttonEnable, true);

			csControlReboot(200, csRebootReason_user);
			break;

		case resetState_factory:
			//gc_dbg("Reset to factory settings");
			pCtrl->activeButton = NULL;
			csControlSignal(csCtrlSignal_resetFactory);
			break;

		default:
			gc_err("Reset state (%d) not handled", pCtrl->resetState);
			pCtrl->activeButton = NULL;
			break;
		}
		break;

	default:
		return;
	}
}


static void handleMessage(taskCtrl_t * pCtrl, taskMsg_t * msg)
{
	switch (msg->code)
	{
	case msgCode_press:
		handlePress(pCtrl, msg->data.source.id);
		break;

	case msgCode_release:
		handleRelease(pCtrl, msg->data.source.id);
		break;

	case msgCode_null:
		// Ignore
		return;
	}
}


static void setLedFlashStatus(taskCtrl_t * pCtrl, uint32_t count)
{
	// Put a one-second off period between changing LED flash patterns
	ledMgrTurnLedOff(csLedNum_status);
	vTaskDelay(pdMS_TO_TICKS(1000));
	pCtrl->pressTime += 1000;
	ledMgrFlashLed(csLedNum_status, 200, 200, count, 1000);
}


static void setLedFlashWifi(taskCtrl_t * pCtrl, csLedColor_t color)
{
	ledMgrTurnLedOff(csLedNum_wifi);
	ledMgrSetLedColor(csLedNum_wifi, color);
	ledMgrFlashLed(csLedNum_wifi, 1000, 0, 0, 0);
}


static void checkTimers(taskCtrl_t * pCtrl)
{
	uint32_t	diff = pCtrl->curTime - pCtrl->pressTime;

	if (pCtrl->isPressed) {
		// Check for center button
		if (capDrvSource_center == pCtrl->activeButton->src) {
			switch (pCtrl->resetState)
			{
			case resetState_idle:
				// Ignore this state
				break;

			case resetState_armed:
				if (diff >= HOLD_TIME_REBOOT_MS) {
					gc_dbg("Reboot armed");
					csControlSignal(csCtrlSignal_rebootArmed);
					pCtrl->resetState = resetState_reboot;
					setLedFlashWifi(pCtrl, csLedColor_green);
					setLedFlashStatus(pCtrl, 1);
				}
				break;

			case resetState_reboot:
				if (diff >= HOLD_TIME_RESET_FACT_MS) {
					gc_dbg("Provisioning reset armed");
					pCtrl->resetState = resetState_factory;
					setLedFlashWifi(pCtrl, csLedColor_yellow);
					setLedFlashStatus(pCtrl, 2);
				}
				break;

			case resetState_factory:
				// Stay in this state until the button is released
				break;

			default:
				gc_err("resetState (%d) not handled", pCtrl->resetState);
				pCtrl->activeButton = NULL;
			}
		}
	} else if (diff >= EXPIRE_TIME_IDLE) {
		if (NULL != pCtrl->activeButton) {
			gc_dbg("Clear the active button (%d)", pCtrl->activeButton->src);
			pCtrl->activeButton = NULL;
		}
	}
}


static void capControlTask(void * param)
{
	taskCtrl_t *	pCtrl = (taskCtrl_t *)param;
	taskMsg_t		msg;
	BaseType_t		eStatus;

	// Start with no active buttons
	pCtrl->activeButton = NULL;

	while (1)
	{
		eStatus = xQueueReceive(pCtrl->queue, &msg, pdMS_TO_TICKS(50));

		// Get snapshot of the current time and check timers
		pCtrl->curTime = (uint32_t)timeMgrGetUptimeMs();
		checkTimers(pCtrl);

		if (pdTRUE == eStatus) {
			handleMessage(pCtrl, &msg);
		}
	}
}


/**
 * \brief Handle CAP1298 driver events
 */
static void capCallback(uint32_t cbData, capDrvEvt_t evtCode, capDrvSource_t src)
{
	taskCtrl_t *	pCtrl = CS_ADR2PTR(cbData);
	taskMsg_t		msg;

	//gc_dbg("capCallback");

	// If the outlet buttons are disabled,
	// allow only center button events through
	if (!appParams.buttonsEnabled && capDrvSource_center != src)
		return;

	switch (evtCode)
	{
	case capDrvEvt_press:
		msg.code = msgCode_press;
		msg.data.source.id = src;
		break;

	case capDrvEvt_release:
		msg.code = msgCode_release;
		msg.data.source.id = src;
		break;

	default:
		gc_err("Undefined CAP driver event code (%d)", evtCode);
		return;
	}

	if (xQueueSend(pCtrl->queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
		gc_err("Failed to send message to task");
	}
}


/**
 * \brief Handle outlet on/off plug in/out events
 */
static void emtrCallback(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	switch ((emtrEvtCode_t)evtCode)
	{
	case emtrEvtCode_socketOn:
	case emtrEvtCode_socketOff:
	case emtrEvtCode_plugInserted:
	case emtrEvtCode_plugRemoved:
		// re-calibrate the touch sensor
		cap1298DrvCalibrate();
		break;

	default:
		// Ignore other events
		return;
	}
}
