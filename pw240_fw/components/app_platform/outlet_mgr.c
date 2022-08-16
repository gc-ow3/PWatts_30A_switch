/*
 * outlet_mgr.c
 *
 *  Created on: Feb 18, 2019
 *      Author: wesd
 */

#include "cs_common.h"
#include "cs_platform.h"
#include "cs_heap.h"
#include "app_led_mgr.h"
#include "outlet_mgr.h"
#include "emtr_drv.h"
#include "app_params.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"outlet_mgr"
#include "mod_debug.h"

typedef struct {
	csLedNum_t		ledNum;
	emtrCbId_t		cbId;
	eventCbFunc_t	cbFunc;
	const char *	paramKey_on;
	const char *	paramKey_type;
	const char *	paramKey_name;
} const info_t;


typedef struct {
	info_t *		pInfo;
	bool			isOn;
	appDeltaSrc_t	delta;
} socket_t;


typedef struct {
	cbHandle_t		cbHandle;
	bool			isStarted;
	socket_t		socket[NUM_SOCKETS];
} control_t;


static void notify(
	control_t *				pCtrl,
	callCtx_t				ctx,
	outletMgrEvtCode_t		eCode,
	outletMgrEvtData_t *	eData
);

static void emtrSocket1EvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

static void emtrSocket2EvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

// Static information about each socket
static info_t	socketInfo[NUM_SOCKETS] = {
	{
		.ledNum        = csLedNum_socket1,
		.cbId          = emtrCbId_socket1,
		.cbFunc        = emtrSocket1EvtCb,
		.paramKey_on   = paramKey_socket1On,
		.paramKey_type = paramKey_socket1Type,
		.paramKey_name = paramKey_socket1Name
	},
	{
		.ledNum        = csLedNum_socket2,
		.cbId          = emtrCbId_socket2,
		.cbFunc        = emtrSocket2EvtCb,
		.paramKey_on   = paramKey_socket2On,
		.paramKey_type = paramKey_socket2Type,
		.paramKey_name = paramKey_socket2Name
	}
};


static control_t *	control;


esp_err_t outletMgrInit(void)
{
	control_t *		pCtrl = control;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	if (eventRegisterCreate(&pCtrl->cbHandle) != ESP_OK)
		return ESP_FAIL;

	// Attach static info to each socket control block
	int		i;
	for (i = 0; i < NUM_SOCKETS; i++) {
		pCtrl->socket[i].pInfo = &socketInfo[i];
	}

	control = pCtrl;

	socket_t *	socket = pCtrl->socket;

	for (int oIdx = 0; oIdx < NUM_SOCKETS; oIdx++, socket++) {
		int			oNum  = 1 + oIdx;
		info_t *	pInfo = socket->pInfo;
		if (appParams.socket[oIdx].on) {
			emtrDrvSetSocket(oNum, true);
			ledMgrTurnLedOn(pInfo->ledNum);
		} else {
			emtrDrvSetSocket(oNum, false);
			ledMgrTurnLedOff(pInfo->ledNum);
		}
		socket->delta = appDeltaSrc_internal;
	}
	return ESP_OK;
}


esp_err_t outletMgrStart(void)
{
	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (pCtrl->isStarted)
		return ESP_OK;

	int		oIdx;

	// Set each socket "on" state and its LED per the socket's stored value
	socket_t *	socket = pCtrl->socket;
	uint32_t	cbData = CS_PTR2ADR(pCtrl);

	for (oIdx = 0; oIdx < NUM_SOCKETS; oIdx++, socket++) {
		info_t *	pInfo = socket->pInfo;

		// Register the callback function for this socket
		emtrDrvCallbackRegister(pInfo->cbId, pInfo->cbFunc, cbData);
	}

	pCtrl->isStarted = true;
	return ESP_OK;
}


esp_err_t outletMgrCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData)
{
	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventRegisterCallback(pCtrl->cbHandle, cbFunc, cbData);
}


esp_err_t outletMgrSetSocket(int sockNum, bool value, callCtx_t ctx, appDeltaSrc_t delta)
{
	if (sockNum < 1 || sockNum > NUM_SOCKETS) {
		return ESP_ERR_INVALID_ARG;
	}

	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isStarted)
		return ESP_FAIL;

	int	oIdx = sockNum - 1;

	socket_t *	sock = &pCtrl->socket[oIdx];

	paramMgrSetBool(sock->pInfo->paramKey_on, value);
	emtrDrvSetSocket(sockNum, value);

	// Notify interested parties of the event
	outletMgrEvtData_t	evtData;

	evtData.socketNum = sockNum;
	evtData.data.on   = value;
	notify(pCtrl, ctx, outletMgrEvt_setTargetState, &evtData);

	if (appDeltaSrc_null != delta) {
		sock->delta = delta;
		evtData.data.name = outletMgrDeltaSrcToStr(delta);
		notify(pCtrl, ctx, outletMgrEvt_setDeltaSource, &evtData);
	}

	return ESP_OK;
}


esp_err_t outletMgrSetType(int sockNum, uint32_t value, callCtx_t ctx)
{
	if (sockNum < 1 || sockNum > NUM_SOCKETS) {
		return ESP_ERR_INVALID_ARG;
	}

	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isStarted)
		return ESP_FAIL;

	int	oIdx = sockNum - 1;

	socket_t *	sock = &pCtrl->socket[oIdx];

	paramMgrSetU32(sock->pInfo->paramKey_type, value);

	// Notify interested parties of the event
	outletMgrEvtData_t	evtData;

	evtData.socketNum = sockNum;
	evtData.data.type = value;

	notify(pCtrl, ctx, outletMgrEvt_setType, &evtData);

	return ESP_OK;
}


esp_err_t outletMgrSetName(int sockNum, const char * value, callCtx_t ctx)
{
	if (sockNum < 1 || sockNum > NUM_SOCKETS) {
		return ESP_ERR_INVALID_ARG;
	}

	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isStarted)
		return ESP_FAIL;

	int	oIdx = sockNum - 1;

	socket_t *	sock = &pCtrl->socket[oIdx];

	paramMgrSetStr(sock->pInfo->paramKey_name, value);

	// Notify interested parties of the event
	outletMgrEvtData_t	evtData;

	evtData.socketNum = sockNum;
	evtData.data.name = value;

	notify(pCtrl, ctx, outletMgrEvt_setName, &evtData);

	return ESP_OK;
}


esp_err_t outletMgrSetSource(int sockNum, appDeltaSrc_t value, callCtx_t ctx)
{
	if (sockNum < 1 || sockNum > NUM_SOCKETS) {
		return ESP_ERR_INVALID_ARG;
	}

	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isStarted)
		return ESP_FAIL;

	// Ignore the null value
	if (appDeltaSrc_null == value)
		return ESP_OK;

	int	oIdx = sockNum - 1;

	// Store the value
	pCtrl->socket[oIdx].delta = value;

	// Notify interested parties of the event
	outletMgrEvtData_t	evtData;

	evtData.socketNum = sockNum;
	evtData.data.name = outletMgrDeltaSrcToStr(value);

	notify(pCtrl, ctx, outletMgrEvt_setDeltaSource, &evtData);
	return ESP_OK;
}



esp_err_t outletMgrGetSource(int sockNum, appDeltaSrc_t * value)
{
	if (sockNum < 1 || sockNum > NUM_SOCKETS) {
		return ESP_ERR_INVALID_ARG;
	}

	control_t *		pCtrl = control;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (!pCtrl->isStarted)
		return ESP_FAIL;

	int	oIdx = sockNum - 1;

	*value = pCtrl->socket[oIdx].delta;
	return ESP_OK;
}



const char * outletMgrDeltaSrcToStr(appDeltaSrc_t src)
{
	switch (src)
	{
	case appDeltaSrc_null:
		return "--";
	case appDeltaSrc_voice:
		return "VI";
	case appDeltaSrc_app:
		return "AI";
	case appDeltaSrc_user:
	case appDeltaSrc_internal:
		return "PI";
	default:
		return "??";
	}
}


const char * outletMgrGetSourceStr(int sockNum)
{
	appDeltaSrc_t	value;

	if (outletMgrGetSource(sockNum, &value) != ESP_OK)
		return "--";

	return outletMgrDeltaSrcToStr(value);
}


static void notify(
	control_t *				pCtrl,
	callCtx_t				ctx,
	outletMgrEvtCode_t		eCode,
	outletMgrEvtData_t *	eData
)
{
	eventNotify(pCtrl->cbHandle, ctx, (uint32_t)eCode, CS_PTR2ADR(eData));
}


static void emtrSocket1EvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	(void)cbData;
	(void)ctx;
	(void)evtData;

	// Switch the LED to match the socket state

	switch ((emtrEvtCode_t)evtCode)
	{
	case emtrEvtCode_socketOn:
		ledMgrTurnLedOn(csLedNum_socket1);
		break;

	case emtrEvtCode_socketOff:
		ledMgrTurnLedOff(csLedNum_socket1);
		break;

	default:
		// Ignore other EMTR events
		return;
	}
}


static void emtrSocket2EvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	(void)cbData;
	(void)ctx;
	(void)evtData;

	// Switch the LED to match the socket state

	switch ((emtrEvtCode_t)evtCode)
	{
	case emtrEvtCode_socketOn:
		ledMgrTurnLedOn(csLedNum_socket2);
		break;

	case emtrEvtCode_socketOff:
		ledMgrTurnLedOff(csLedNum_socket2);
		break;

	default:
		// Ignore other EMTR events
		return;
	}
}
