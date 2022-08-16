/*
 * emtr_drv.c
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#include "driver/gpio.h"
#include "driver/uart.h"

#include "cs_common.h"
#include "cs_control.h"
#include "cs_heap.h"
#include "time_mgr.h"
#include "fw_file_check.h"
#include "xmodem.h"
#include "emtr_fw.h"
#include "emtr_drv.h"
#include "cs_self_test.h" // todo jonw remove for production
#include "mfg_data.h"// todo jonw remove for production

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"emtr_drv"
#include "mod_debug.h"

#if MOD_DEBUG
// Comment out the line below to disable periodic debug reports
//#define ENA_PERIOD_REPORT		(1)
#endif

#ifndef ENA_PERIOD_REPORT
#define ENA_PERIOD_REPORT		(0)
#endif


////////////////////////////////////////////////////////////////////////////////
// Portability macros
////////////////////////////////////////////////////////////////////////////////

// Sleep for a number of milliseconds
#define	EMTR_SLEEP_MS(t)			vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	EMTR_TIME_SEC()				timeMgrGetUptime()

// Acquire and release mutex
#define EMTR_MUTEX_GET(ctrl)		xSemaphoreTake(ctrl->mutex, portMAX_DELAY)
#define EMTR_MUTEX_PUT(ctrl)		xSemaphoreGive(ctrl->mutex)

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

#define EMTR_POLL_CYCLES_PER_SEC		(10)
#define EMTR_POLL_PERIOD_MS				(1000 / EMTR_POLL_CYCLES_PER_SEC)

// EMTR message framing characters
#define MSG_CHAR_SOP					((uint8_t)0x1B)
#define MSG_CHAR_EOP					((uint8_t)0x0A)

// Bit fields of interest in the device status byte
#define DEV_STATUS_FACTORY_RESET		(1 <<  0)

// Bit fields of interest in the socket status flag bytes
#define OSTATUS_SOCKET_ON				(1 <<  2)
#define OSTATUS_PLUG_DETECT				(1 <<  3)

// EMTR command codes
#define EMTR_CMD_GET_STATE				(0x00)
#define EMTR_CMD_GET_STATUS				(0x01)
#define EMTR_CMD_GET_KWH				(0x02)
#define EMTR_CMD_GET_INSTANT_PWR		(0x03)
#define EMTR_CMD_GET_SW_VERSION			(0x05)

#define EMTR_CMD_SET_SOCKET_1_OFF		(0x10)
#define EMTR_CMD_SET_SOCKET_1_ON		(0x11)
#define EMTR_CMD_GET_SOCKET_1_SIG_TS	(0x12)
#define EMTR_CMD_GET_SOCKET_1_SIG_PG	(0x13)

#define EMTR_CMD_SET_SOCKET_2_OFF		(0x20)
#define EMTR_CMD_SET_SOCKET_2_ON		(0x21)
#define EMTR_CMD_GET_SOCKET_2_SIG_TS	(0x22)
#define EMTR_CMD_GET_SOCKET_2_SIG_PG	(0x23)

#define EMTR_CMD_GET_FW_STATUS			(0x31)
#define EMTR_CMD_START_XMODEM			(0x32)
#define EMTR_CMD_REBOOT					(0x33)


////////////////////////////////////////////////////////////////////////////////
// Internal data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
	uint8_t		sockNum;
	emtrCbId_t	callbackId;
	uint8_t		cmdTurnOn;
	uint8_t		cmdTurnOff;
	uint8_t		cmdReadSigTs;
	uint8_t		cmdReadSigPg;
} const sockInfo_t;

/**
 * \brief EMTR run modes
 */
typedef enum {
	emtrRunMode_notRunning = 0,
	emtrRunMode_application,
	emtrRunMode_bootLoader
} emtrRunMode_t;


/**
 * \brief EMTR task message codes
 */
typedef enum {
	emtrtMsgCode_timer,
	emtrMsgCode_socketOn,
	emtrMsgCode_socketOff,
	emtrMsgCode_shutDown,
	emtrMsgCode_pause,
	emtrMsgCode_resume,
} emtrMsgCode_t;


/**
 * \brief EMTR task message data options
 */
typedef union {
	int				socketNum;
} emtrMsgData_t;


/**
 * \brief structure of EMTR message sent via queue
 */
typedef struct {
	emtrMsgCode_t	msgCode;
	emtrMsgData_t	msgData;
} emtrMsg_t;


/**
 * \brief Boolean states
 */
typedef enum {
	boolState_init  = -1,
	boolState_false =  0,
	boolState_true  =  1
} boolState_t;

#define STATE_IS_TRUE(v)		(boolState_true == (v).curState)
#define SOCKET_IS_ON(o)			(STATE_IS_TRUE((o)->relayActive))
#define SOCKET_IS_OFF(o)		(!STATE_IS_TRUE((o)->relayActive))
#define SOCKET_IS_PLUGGED(o)	(STATE_IS_TRUE((o)->isPlugged))
#define SOCKET_IS_ACTIVE(o)		(SOCKET_IS_ON(o) &&  SOCKET_IS_PLUGGED(o))

/**
 * \brief Boolean state change tracking structure
 */
typedef struct {
	boolState_t		curState;
	boolState_t		newState;
} stateChange_t;


typedef struct {
	uint32_t	dVolts;
	uint32_t	mAmps;
	uint32_t	dWatts;
	uint32_t	pf;
	uint64_t	dWattHours;
} oldEnergy_t;


#define EMTR_PRE_SAMPLE_SZ	(4)

typedef struct {
	uint32_t	min;	// Minimum value read
	uint32_t	max;	// Maximum value read
	uint32_t	avg;
	uint64_t	sum;
	uint32_t	sampleCt;
	uint32_t	preSample[EMTR_PRE_SAMPLE_SZ];
	int			preSamplePut;
	int			preSampleGet;
	int			preSampleCt;
} accum_t;

typedef struct {
	bool		initial;
	uint64_t	holdOffMs;
	accum_t		dVolts;			// 0.1 volt units
	accum_t		mAmps;			// 0.001 Amp units
	accum_t		dWatts;			// 0.1 Watt units
	accum_t		powerFactor;	// 0..100
} accEnergy_t;

/**
 * \brief This structure is used internally to track the socket status
 */
typedef struct {
	sockInfo_t *		info;
	cbHandle_t			cbHandle;
	stateChange_t		relayActive;
	uint32_t			cosTimeRelay;
	stateChange_t		isPlugged;
	emtrSocketStatus_t	stat;
	accEnergy_t			eAccChan[EMTR_ACC_NUM_CHANS];
	oldEnergy_t			oldEnergy;
	uint32_t			oldCosTimeRelay;
} socketCtrl_t;


/**
 * \brief States of the EMTR firmware upgrade process
 */
typedef enum {
	fwupgState_idle = 0,
	fwupgState_start,
	fwupgState_inProgress,
	fwupgState_success,
	fwupgState_fail
} fwupgState_t;


/**
 * \brief Task control structure
 */
typedef struct {
	bool				isRunning;
	bool				shutdown;
	bool				pause;
	emtrDrvConf_t		conf;
	emtrRunMode_t		emtrMode;
	uint32_t			emtrResetCount;
	char				blVersion[16];
	char				fwVersion[16];
	SemaphoreHandle_t	mutex;
	QueueHandle_t		queue;
	TimerHandle_t		timer;
	cbHandle_t			cbDevice;
	TaskHandle_t		taskHandle;
	emtrDeviceStatus_t	curDeviceStatus;
	emtrDeviceStatus_t	oldDeviceStatus;
	socketCtrl_t *		socketCtrl;
	uint32_t			curTime;
	uint32_t			commDelayMs;
	bool				holdOffCommand;
	fwupgState_t		fwupgState;
	bool				spyEnable;
	unsigned int		pollSampleCt;
} emtrCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////

static socketCtrl_t * getSocketCtrl(emtrCtrl_t * pCtrl, uint8_t sockNum);

static esp_err_t getCallbackHandle(
	emtrCtrl_t *	pCtrl,
	emtrCbId_t		id,
	cbHandle_t *	ret
);

static esp_err_t uartInit(emtrCtrl_t * pCtrl);

static esp_err_t uartWrite(emtrCtrl_t * pCtrl, const uint8_t * buf, int len);

static esp_err_t uartRead(emtrCtrl_t * pCtrl, uint8_t * buf, int * len);

static esp_err_t resetEmtrBoard(emtrCtrl_t * pCtrl, emtrRunMode_t runMode);

static esp_err_t checkRequest(emtrCtrl_t * pCtrl, int sockNum, bool lock);

static esp_err_t readDeviceState(emtrCtrl_t * pCtrl);

static void checkChangeOfState(emtrCtrl_t * pCtrl);

static void ctrlTask(void * param);

static void timerCallback(TimerHandle_t timerHandle);

static esp_err_t setSocketState(emtrCtrl_t * pCtrl, int sockNum, bool turnOn);

static void clearEmtrAccumulator(accum_t * accum, int value);

static void clearEmtrAccumulators(accEnergy_t * eAccum);

static esp_err_t getAccChan(
	emtrCtrl_t *		pCtrl,
	int					sockNum,
	int					accChan,
	emtrAccEnergy_t *	ret
);

static esp_err_t readSignature(
	emtrCtrl_t *	pCtrl,
	int				sockNum,
	uint32_t *		timestamp,
	uint8_t *		reason,
	uint8_t *		pBuf,
	int				rdLen
);

static esp_err_t emtrRunModeSet(
	emtrCtrl_t *	pCtrl,
	emtrRunMode_t	targetMode,
	uint8_t *		version
);

static esp_err_t emtrRunModeGet(
	emtrCtrl_t *		pCtrl,
	emtrRunMode_t *		mode,
	uint8_t *			version
);

static esp_err_t emtrUpgrade(emtrCtrl_t * pCtrl, const uint8_t * fwFile);

static esp_err_t startXmodemTransfer(emtrCtrl_t * pCtrl);

static void sysEventCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

#if (ENA_PERIOD_REPORT)
static void printDebug(emtrCtrl_t * pCtrl, uint32_t curTime);
#endif


////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////

static uint32_t	uartTxSignalIdx[3] = {
	U0TXD_OUT_IDX,
	U1TXD_OUT_IDX,
	U2TXD_OUT_IDX
};

// The EMTR for the IWO has the concept of socket 1 and 2 flipped from what
// we want the user to see, so map SOCKET-2 EMTR functions for socket 1, etc
static sockInfo_t	sockInfo[] = {
	{
		.sockNum      = 1,
		.callbackId   = emtrCbId_socket1,
		.cmdTurnOn    = EMTR_CMD_SET_SOCKET_2_ON,
		.cmdTurnOff   = EMTR_CMD_SET_SOCKET_2_OFF,
		.cmdReadSigTs = EMTR_CMD_GET_SOCKET_2_SIG_TS,
		.cmdReadSigPg = EMTR_CMD_GET_SOCKET_2_SIG_PG,
	},
	{
		.sockNum      = 2,
		.callbackId   = emtrCbId_socket2,
		.cmdTurnOn    = EMTR_CMD_SET_SOCKET_1_ON,
		.cmdTurnOff   = EMTR_CMD_SET_SOCKET_1_OFF,
		.cmdReadSigTs = EMTR_CMD_GET_SOCKET_1_SIG_TS,
		.cmdReadSigPg = EMTR_CMD_GET_SOCKET_1_SIG_PG,
	}
};
#define sockCmdSz	(sizeof(sockInfo) / sizeof(sockInfo_t))


////////////////////////////////////////////////////////////////////////////////
// Variable data
////////////////////////////////////////////////////////////////////////////////
// Define the message queue
#define MSG_QUEUE_SZ		(20)

// Task control structure
static emtrCtrl_t *	emtrCtrl;


/**
 * \brief Initialize, but not start the EMTR driver
 *
 * Allocate resources used by this driver
 *
 * Must call this before starting any tasks that will register to
 * receive event notifications from this driver
 *
 */
esp_err_t emtrDrvInit(const emtrDrvConf_t * conf)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL != pCtrl) {
		// Already initialized
		return ESP_OK;
	}

	if (NULL == conf) {
		return ESP_ERR_INVALID_ARG;
	}

	if (sockCmdSz < conf->numSockets) {
		gc_err("sockCmd table does not support number of sockets (%d)", conf->numSockets);
		return ESP_ERR_INVALID_SIZE;
	}

	// Allocate the task control structure
	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	// Copy the configuration to the control structure
	pCtrl->conf = *((emtrDrvConf_t *)conf);

	esp_err_t	status;

	// Allocate space to store each socket control structure
	pCtrl->socketCtrl = cs_heap_calloc(conf->numSockets, sizeof(socketCtrl_t));
	if (NULL == pCtrl->socketCtrl) {
		gc_err("Failed to allocate socket status structure");
		status = ESP_ERR_NO_MEM;
		goto exitMem;
	}

	// Set the firmware versions for now until read from EMTR
	strcpy(pCtrl->blVersion, "0.0.0");
	strcpy(pCtrl->fwVersion, "0.0.0");

	if ((status = uartInit(pCtrl)) != ESP_OK) {
		gc_err("uartInit() failed");
		goto exitMem;
	}

	if ((pCtrl->mutex = xSemaphoreCreateMutex()) == NULL) {
		gc_err("Mutex create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	pCtrl->queue = xQueueCreate(MSG_QUEUE_SZ, sizeof(emtrMsg_t));
	if (NULL == pCtrl->queue) {
		gc_err("Queue create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	pCtrl->timer = xTimerCreate(
		"timer-" MOD_NAME,
		pdMS_TO_TICKS(EMTR_POLL_PERIOD_MS),
		pdFALSE,
		(void *)pCtrl,
		timerCallback
	);
	if (NULL == pCtrl->timer) {
		gc_err("Timer create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	if ((status = eventRegisterCreate(&pCtrl->cbDevice)) != ESP_OK) {
		gc_err("eventRegisterCreate failed");
		goto exitMem;
	}

	// Now set up each socket control structure

	socketCtrl_t *	sCtrl = pCtrl->socketCtrl;
	sockInfo_t *	sInfo = sockInfo;

	int	i;
	for (i = 0; i < conf->numSockets; i++, sCtrl++, sInfo++) {
		// Attach the associated info structure
		sCtrl->info = sInfo;

		// Create the event callback
		status = eventRegisterCreate(&sCtrl->cbHandle);
		if (ESP_OK != status) {
			gc_err("eventRegisterCreate(%d) failed", i);
			goto exitMem;
		}

		// Initialize energy accumulator channels
		int	ch;
		for (ch = 0; ch < EMTR_ACC_NUM_CHANS; ch++) {
			clearEmtrAccumulators(&sCtrl->eAccChan[ch]);
		}

		// Initialize the "On" state change trackers
		sCtrl->relayActive.curState = boolState_init;
		sCtrl->relayActive.newState = boolState_init;

		// Initialize the "Plugged" state change trackers
		sCtrl->isPlugged.curState = boolState_init;
		sCtrl->isPlugged.newState = boolState_init;
	}

	// Set IO pin for EMTR reset output, initially inactive (high)
	gpio_set_level((gpio_num_t)conf->gpioEmtrRst, 1);

	// Configure the EMTR reset pin
	gpio_config_t	gpioCfg = {
		.pin_bit_mask = (uint64_t)1 << conf->gpioEmtrRst,
		.mode         = GPIO_MODE_OUTPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en   = GPIO_PULLUP_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE
	};
	gpio_config(&gpioCfg);

	// Start the EMTR boot loader and read its version
	uint8_t	ver[3];
	status = emtrRunModeSet(pCtrl, emtrRunMode_bootLoader, ver);
	if (ESP_OK == status) {
		snprintf(
			pCtrl->blVersion, sizeof(pCtrl->blVersion),
			"%u.%u.%u",
			ver[0], ver[1], ver[2]
		);
	} else {
		gc_err("Failed to start EMTR boot loader");
		goto exitMem;
	}

	// Start the EMTR application and read its version
	status = emtrRunModeSet(pCtrl, emtrRunMode_application, ver);
	if (ESP_OK == status) {
		snprintf(
			pCtrl->fwVersion, sizeof(pCtrl->fwVersion),
			"%u.%u.%u",
			ver[0], ver[1], ver[2]
		);
	} else {
		// Don't abort the initialization at this point, maybe the firmware
		// can be programmed via the boot loader
		gc_err("Failed to start EMTR application");
	}

	emtrCtrl = pCtrl;
	return ESP_OK;

exitMem:
	if (pCtrl->mutex)
		vSemaphoreDelete(pCtrl->mutex);
	if (pCtrl->queue)
		vQueueDelete(pCtrl->queue);
	if (pCtrl->timer)
		xTimerDelete(pCtrl->timer, 10);
	if (pCtrl->socketCtrl)
		cs_heap_free(pCtrl->socketCtrl);
	cs_heap_free(pCtrl);
	return status;
}


/**
 * \brief Start the EMTR driver
 *
 * Must call \ref emtrDrvInit before calling this
 *
 */
esp_err_t emtrDrvStart(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isRunning)
		return ESP_OK;

	BaseType_t	xStatus;

	// Start the control task
	xStatus = xTaskCreate(
		ctrlTask,
		"task-" MOD_NAME,
		3000,
		(void *)pCtrl,
		pCtrl->conf.taskPrio,
		&pCtrl->taskHandle
	);
	if (pdPASS != xStatus) {
		gc_err("Task create failed");
		return ESP_FAIL;
	}

	// Start the poll timer
	xTimerReset(pCtrl->timer, pdMS_TO_TICKS(20));

	if (csControlCallbackRegister(sysEventCb, CS_PTR2ADR(pCtrl)) != ESP_OK)
		return ESP_FAIL;

	// The driver is running
	pCtrl->isRunning = true;

	// Check if there is a firmware update for the EMTR
	if (csFwFileIsValid(emtrFwBin, "emtr")) {
		const csFwHdr_t *	fwHdr = (const csFwHdr_t *)emtrFwBin;

		char	verStr[20];
		snprintf(
			verStr, sizeof(verStr),
			"%u.%u.%u",
			fwHdr->majorVer, fwHdr->minorVer, fwHdr->patchVer
		);

		if (strcmp(verStr, emtrDrvGetFwVersion()) == 0) {
			gc_dbg("EMTR firmware %s is current", verStr);
		} else {
			gc_dbg("Update EMTR firmware to v%s", verStr);

			if (emtrUpgrade(pCtrl, emtrFwBin) == ESP_OK) {
				gc_dbg("EMTR update completed");
			} else {
				gc_err("EMTR update failed");
			}
		}
	}

	return ESP_OK;
}


/**
 * \brief Register a callback function to be notified of driver events
 */
esp_err_t emtrDrvCallbackRegister(emtrCbId_t cbId, eventCbFunc_t cbFunc, uint32_t cbData)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}

	cbHandle_t	cbHandle;
	esp_err_t	status;

	status = getCallbackHandle(pCtrl, cbId, &cbHandle);
	if (ESP_OK != status) {
		return status;
	}

	status = eventRegisterCallback(cbHandle, cbFunc, cbData);
	if (ESP_OK != status) {
		gc_err("eventRegisterCallback failed");
		return status;
	}

	return ESP_OK;
}


/**
 * \brief Unregister a callback function to be notified of driver events
 */
esp_err_t emtrDrvCallbackUnregister(emtrCbId_t cbId, eventCbFunc_t cbFunc)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl) {
		return ESP_FAIL;
	}

	cbHandle_t	cbHandle;
	esp_err_t	status;

	status = getCallbackHandle(pCtrl, cbId, &cbHandle);
	if (ESP_OK != status) {
		return status;
	}

	return eventUnregisterCallback(cbHandle, cbFunc);
}


/**
 * \brief Turn selected socket on or off
 *
 * \param [in] sockNum Select socket
 *
 * \return ESP_OK Message successfully sent to the control task
 * \return ESP_FAIL
 *
 */
esp_err_t emtrDrvSetSocket(int sockNum, bool turnOn)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	int		status;

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, sockNum, false)) != ESP_OK) {
		gc_err("emtrDrvSetSocket error %d", status);
		return status;
	}

	emtrMsg_t	msg;

	msg.msgCode = turnOn ? emtrMsgCode_socketOn : emtrMsgCode_socketOff;
	msg.msgData.socketNum = sockNum;

	status = xQueueSend(pCtrl->queue, &msg, pdMS_TO_TICKS(100));

	return ESP_OK;
}


/**
 * \brief Report on|off state for a socket
 *
 * \param [in] sockNum select socket

 * \return true socket is on
 * \return false The socket is not on or, bad sockNum or,
 * driver was not started
 *
 */
bool emtrDrvIsSocketOn(int sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return false;

	int	status;

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, sockNum, false)) != ESP_OK) {
		//gc_err("emtrDrvIsOutletOn error %d", status);
		return false;
	}

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (sCtrl) {
		return SOCKET_IS_ON(sCtrl);
	}

	return false;
}


/**
 * \brief Report plug insertion state for a socket
  *
  * \param [in] sockNum select socket

 * \return true There is a plug inserted
 * \return false The socket is not on or, bad sockNum or,
 * driver was not started
 *
 */
bool emtrDrvIsPlugInserted(int sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return false;

	int	status;

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, sockNum, false)) != ESP_OK) {
		gc_err("emtrDrvIsPlugInserted error %d", status);
		return false;
	}

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (sCtrl) {
		return SOCKET_IS_PLUGGED(sCtrl);
	}

	return false;
}


/**
 * \brief Test that the socket is on and has a plug inserted
  *
  * \param [in] sockNum select socket
  *
 * \return true The socket is active
 * \return false The socket is not active or, bad sockNum or,
 * driver was not started or, could not acquire mutex
 *
 */
bool emtrDrvIsSocketActive(int sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return false;

	int		status;

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, sockNum, true)) != ESP_OK) {
		gc_err("emtrDrvIsOutletActive error %d", status);
		return false;
	}

	// At this point mutex is locked

	bool	ret = false;

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (sCtrl) {
		ret = SOCKET_IS_ACTIVE(sCtrl);
	}

	EMTR_MUTEX_PUT(pCtrl);
	return ret;
}


/**
 * \brief Read state for selected socket
  *
  * \param [in] sockNum select socket
  * \param [out] ret Pointer to structure to receive the data
  *
 * \return WM_SUCCESS Data was successfully read
 * \return -WM_E_INVAL Bad sockNum or NULL pointer for ret
 * \return -WM_E_UNINIT Driver was not started
 * \return -WM_FAIL Could not acquire mutex
 */
esp_err_t emtrDrvGetSocketStatus(int sockNum, emtrSocketStatus_t * ret)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (NULL == ret)
		return ESP_ERR_INVALID_ARG;

	int		status;

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, sockNum, true)) != ESP_OK) {
		gc_err("emtrDrvGetOutletStatus error %d", status);
		return status;
	}

	// At this point mutex is locked

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (sCtrl) {
		// Copy the status
		*ret = sCtrl->stat;

		ret->relayTime  = EMTR_TIME_SEC() - sCtrl->cosTimeRelay;
	}

	EMTR_MUTEX_PUT(pCtrl);
	return status;
}


/**
 * \brief Read all device-level state information
 *
 * \param [out] ret Pointer to structure to receive device information
 *
 * \return WM_SUCCESS Data was successfully read
 * \return -WM_E_INVAL NULL pointer passed
 * \return -WM_E_UNINIT Driver was not started
 * \return -WM_FAIL Could not acquire mutex
 *
 */
esp_err_t emtrDrvGetDeviceStatus(emtrDeviceStatus_t * ret)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	int		status;

	if (!ret) {
		return ESP_FAIL;
	}

	// Validate driver state and parameters
	if ((status = checkRequest(pCtrl, 1, true)) != ESP_OK) {
		gc_err("emtrDrvGetDeviceStatus error %d", status);
		return status;
	}

	// At this point mutex is locked

	// Copy device information
	*ret = pCtrl->curDeviceStatus;

	EMTR_MUTEX_PUT(pCtrl);
	return status;
}


/**
 * \brief Return EMTR firmware version string
 */
const char * emtrDrvGetFwVersion(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return "N/A";

	return (const char *)pCtrl->fwVersion;
}


/**
 * \brief Return EMTR boot loader version string
 */
const char * emtrDrvGetBlVersion(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return "N/A";

	return (const char *)pCtrl->blVersion;
}


/**
 * \brief Read the power signature for a socket
 */
esp_err_t emtrDrvGetSignature(
	int				sockNum,
	uint32_t *		ts,
	uint8_t *		reason,
	uint8_t *		buf,
	int				bufLen
)
{
	if (!ts || !reason || !buf) {
		gc_err("Bad parameter");
		return ESP_ERR_INVALID_ARG;
	}

	if (bufLen < (PWR_SIGNATURE_NUM_SAMPLES * 4)) {
		gc_err("Buffer size (%d) too small, need %d", bufLen, (PWR_SIGNATURE_NUM_SAMPLES * 4));
		return ESP_ERR_INVALID_ARG;
	}

	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl) {
		gc_err("driver not active");
		return ESP_FAIL;
	}

	if (pCtrl->pause) {
		gc_err("driver is paused");
		return ESP_ERR_INVALID_STATE;
	}

	int		status;

	// Validate driver state and parameters and lock access
	if ((status = checkRequest(pCtrl, sockNum, true)) != ESP_OK) {
		gc_err("emtrDrvGetSignature error %d", status);
		return status;
	}

	status = readSignature(pCtrl, sockNum, ts, reason, buf, bufLen);

	EMTR_MUTEX_PUT(pCtrl);
	return status;
}


const char * emtrDrvEventString(emtrEvtCode_t code)
{
	switch (code)
	{
	case emtrEvtCode_socketOn:
		return "emtrEvtCode_socketOn";
	case emtrEvtCode_socketOff:
		return "emtrEvtCode_socketOff";
	case emtrEvtCode_plugInserted:
		return "emtrEvtCode_plugInserted";
	case emtrEvtCode_plugRemoved:
		return "emtrEvtCode_plugRemoved";
	case emtrEvtCode_commDown:
		return "emtrEvtCode_commDown";
	case emtrEvtCode_commUp:
		return "emtrEvtCode_commUp";
	case emtrEvtCode_reset:
		return "emtrEvtCode_reset";
	case emtrEvtCode_temperature:
		return "emtrEvtCode_temperature";
	case emtrEvtCode_epoch:
		return "emtrEvtCode_epoch";
	case emtrEvtCode_volts:
		return "emtrEvtCode_volts";
	case emtrEvtCode_amps:
		return "emtrEvtCode_amps";
	case emtrEvtCode_watts:
		return "emtrEvtCode_watts";
	case emtrEvtCode_powerFactor:
		return "emtrEvtCode_powerFactor";
	case emtrEvtCode_wattHours:
		return "emtrEvtCode_wattHours";
	case emtrEvtCode_stateTime:
		return "emtrEvtCode_stateTime";
	default:
		return "emtrEvtCode_undefined";
	}
}


esp_err_t emtrDrvGetAccEnergy(int sockNum, int chan, emtrAccEnergy_t * eAcc)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return getAccChan(pCtrl, sockNum, chan, eAcc);
}


/*
********************************************************************************
********************************************************************************
** Local functions
********************************************************************************
********************************************************************************
*/


static socketCtrl_t * getSocketCtrl(emtrCtrl_t * pCtrl, uint8_t sockNum)
{
	if (!pCtrl) {
		return NULL;
	}

	socketCtrl_t *	sCtrl = pCtrl->socketCtrl;
	int				i;
	for (i = 0; i < pCtrl->conf.numSockets; i++, sCtrl++) {
		if (sockNum == sCtrl->info->sockNum) {
			return sCtrl;
		}
	}

	return NULL;
}


static esp_err_t getCallbackHandle(
	emtrCtrl_t *	pCtrl,
	emtrCbId_t		id,
	cbHandle_t *	ret
)
{
	if (emtrCbId_device == id) {
		*ret = pCtrl->cbDevice;
		return ESP_OK;
	}

	socketCtrl_t *	sCtrl = pCtrl->socketCtrl;
	int				i;
	for (i = 0; i < pCtrl->conf.numSockets; i++, sCtrl++) {
		if (id == sCtrl->info->callbackId) {
			*ret = sCtrl->cbHandle;
			return ESP_OK;
		}
	}

	return ESP_ERR_INVALID_ARG;
}


static esp_err_t uartInit(emtrCtrl_t * pCtrl)
{
	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	// Configure the port
	uart_config_t	uCfg = {
		.baud_rate = uartConf->baudRate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	esp_err_t	status;

	if ((status = uart_param_config(uartConf->uart, &uCfg)) != ESP_OK)
		return status;

	status = uart_set_pin(
		uartConf->uart,
		uartConf->gpioUartTx,
		uartConf->gpioUartRx,
		UART_PIN_NO_CHANGE,	// RTS not used,
		UART_PIN_NO_CHANGE	// CTS not used
	);
	if (ESP_OK != status)
		return status;

	return uart_driver_install(uartConf->uart, 128 * 2, 0, 0, NULL, 0);
}


static esp_err_t uartWrite(emtrCtrl_t * pCtrl, const uint8_t * buf, int len)
{
	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	int		wrCount = 0;
	int		wrLen;

	do {
		wrLen = uart_write_bytes(uartConf->uart, (const char *)buf, len);
		if (wrLen < 0)
			return ESP_FAIL;

		if ((wrCount += wrLen) < len) {
			// TX FIFO is full, delay a while so it can drain
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	} while (wrCount < len);

	return ESP_OK;
}


static esp_err_t uartRead(emtrCtrl_t * pCtrl, uint8_t * buf, int * len)
{
	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	int		rdLen;

	rdLen = uart_read_bytes(uartConf->uart, buf, *len, pdMS_TO_TICKS(100));
	if (rdLen < 0)
		return ESP_FAIL;

	*len = rdLen;
	return ESP_OK;
}


/*
 * \brief Reset EMTR board to either normal or boot loader mode
 */
static esp_err_t resetEmtrBoard(emtrCtrl_t * pCtrl, emtrRunMode_t runMode)
{
	uint32_t	pinLevel;

	switch(runMode)
	{
	case emtrRunMode_application:
		pinLevel = 1;
		break;
	case emtrRunMode_bootLoader:
		pinLevel = 0;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}

	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	// Shorthand reference to EMTR mode select pin
	int8_t		gpioNum  = uartConf->gpioUartTx;
	uint32_t	txSigIdx = uartTxSignalIdx[uartConf->uart];

	// Drive active (low) the EMTR reset line
	gpio_set_level(pCtrl->conf.gpioEmtrRst, 0);

	// Drive the pin to select either boot loader mode or regular mode
	gpio_matrix_out(gpioNum, 0x100, 0, 0);
	gpio_reset_pin(gpioNum);
	gpio_config_t	gpioCfg = {
		.pin_bit_mask = (uint64_t)1 << gpioNum,
		.mode         = GPIO_MODE_OUTPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en   = GPIO_PULLUP_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE
	};
	gpio_config(&gpioCfg);
	gpio_set_level(gpioNum, pinLevel);
	gpio_set_direction(gpioNum, GPIO_MODE_OUTPUT);
	// Value 0x100 cancels the assignment of UART TX to this GPIO pin
	EMTR_SLEEP_MS(10);

	// Release the EMTR reset line
	gpio_set_level(pCtrl->conf.gpioEmtrRst, 1);
	EMTR_SLEEP_MS(10);

	// Restore pin to UART TX function
	gpio_set_level(gpioNum, 1);
	gpio_set_direction(gpioNum, GPIO_MODE_DISABLE);
	gpio_matrix_out(gpioNum, txSigIdx, 0, 0);

	// Wait for EMTR to be ready
	EMTR_SLEEP_MS(110);
	return ESP_OK;
}


/**
 * \brief Common validation of parameters and optional mutex lock
 */
static esp_err_t checkRequest(emtrCtrl_t * pCtrl, int sockNum, bool lock)
{
	if (!pCtrl->isRunning) {
		//gc_err("Driver not running");
		return ESP_FAIL;
	}

	if (emtrRunMode_application != pCtrl->emtrMode) {
		gc_err("EMTR application not running");
		return ESP_FAIL;
	}

	if (sockNum < 1 || sockNum > pCtrl->conf.numSockets) {
		gc_err("sockNum (%d) out of range", sockNum);
		return ESP_FAIL;
	}

	if (lock) {
		EMTR_MUTEX_GET(pCtrl);
	}

	return ESP_OK;
}


/**
 * \brief Send notification of event to registered callback functions
 */
static void notify(
	emtrCtrl_t *	pCtrl,
	emtrCbId_t		cbId,
	emtrEvtCode_t	evtCode,
	emtrEvtData_t *	evtData
)
{
	cbHandle_t	cbHandle;

	if (getCallbackHandle(pCtrl, cbId, &cbHandle) == ESP_OK) {
		eventNotify(cbHandle, callCtx_local, (uint32_t)evtCode, CS_PTR2ADR(evtData));
	}
}


/**
 * \brief Perform hardware reset of the EMTR
 */
static void resetEmtr(emtrCtrl_t * pCtrl)
{
	pCtrl->emtrResetCount += 1;

	gc_dbg("");
	gc_dbg(">>>>> Reset EMTR (%u) <<<<<", pCtrl->emtrResetCount);
	gc_dbg("");

	// Reset board for normal operation
	(void)resetEmtrBoard(pCtrl, pCtrl->emtrMode);

	// Tell interested parties the EMTR was reset
	notify(pCtrl, emtrCbId_device, emtrEvtCode_reset, NULL);
}


/**
 * \brief Send a command array over UART to the EMTR
 */
static esp_err_t sendEmtrCommand(emtrCtrl_t * pCtrl, uint8_t cmd, uint8_t * payload)
{
	uint8_t		msg[8];
	int			msgLen = sizeof(msg);
	int			i;

	// Must hold off commands when a socket has been switched on or off
	// because it's sampling the power characteristics when it transitions
	if (pCtrl->holdOffCommand) {
		pCtrl->holdOffCommand = false;
		EMTR_SLEEP_MS(150);
	}

	memset(msg, 0, sizeof(msg));
	msg[0] = MSG_CHAR_SOP;
	msg[1] = cmd;
	if (payload) {
		msg[2] = payload[0];
		msg[3] = payload[1];
		msg[4] = payload[2];
		msg[5] = payload[3];
	}

	// Add checksum of CMD plus payload
	for (i = 1; i < 6; i++)
		msg[6] ^= msg[i];

	// Tack on the EOP
	msg[7] = MSG_CHAR_EOP;

	if (pCtrl->spyEnable) {
		gc_dbg("Write %d bytes to emtr", msgLen);
		gc_hexDump2(NULL, msg, msgLen, true);
	}

	// Send the message
	if (uartWrite(pCtrl, msg, msgLen) != ESP_OK) {
		gc_err("uart_drv_write failed");
		return ESP_FAIL;
	}

	return ESP_OK;
}


/**
 * \brief Read a stream of bytes from EMTR
 */
static esp_err_t readEmtr(emtrCtrl_t * pCtrl, uint8_t * buf, int rdLen)
{
	esp_err_t	status;
	int			retryCt = 0;
	int			rdCount = 0;
	int			ioLen;

	while (rdCount < rdLen)
	{
		ioLen = rdLen - rdCount;
		status = uartRead(pCtrl, buf + rdCount, &ioLen);
		if (ESP_OK != status) {
			gc_err("uartRead failed");
			goto exitError;
		}

		if (ioLen > 0) {
			rdCount += ioLen;
			retryCt = 0;
		} else {
			if (++retryCt == 10) {
				gc_err("Timed out reading from EMTR");
				status = ESP_FAIL;
				goto exitError;
			}

			// Wait 10 ms and read again
			EMTR_SLEEP_MS(10);
		}
	}

	if (pCtrl->spyEnable) {
		gc_dbg("Read %d bytes from emtr", rdCount);
		gc_hexDump2(NULL, buf, rdCount, true);
	}

	return rdCount;

exitError:
	if (rdCount > 0) {
		gc_err("readEmtr failed after reading %d bytes", rdCount);
		gc_hexDump2(NULL, buf, rdCount, true);
	} else {
		gc_err("readEmtr received no bytes");
	}
	return status;
}


/**
 * \brief Read and discard the rest of the EMTR message
 */
static void flushEmtrMsg(emtrCtrl_t * pCtrl, int msgLen)
{
	uint8_t		raw[16];
	int			rdLen;
	int			status;
	int			rdCount = 0;

	gc_dbg("Flush emtr message");
	while (rdCount < msgLen) {
		rdLen = (msgLen - rdCount) > sizeof(raw) ? sizeof(raw) : msgLen - rdCount;
		status = readEmtr(pCtrl, raw, rdLen);
		if (status <= 0)
			break;
		rdCount += status;

#if MOD_DEBUG
		gc_hexDump(raw, status);
#endif
	}
}


/**
 * \brief Read and validate the response from EMTR
 *
 * \param [in] pCtrl Pointer to the task control structure
 * \param [in] cmd The expected value in the 'cmd' field
 * \param [out] ret Buffer to receive the payload
 * \param [in] retSz Size of the payload buffer
 *
 * \return >= 0  Number of bytes read
 * \return < 0 Error status
 *
 */
static esp_err_t readEmtrResponse(emtrCtrl_t * pCtrl, uint8_t cmd, uint8_t * ret, int retSz)
{
	int			payloadLen;
	int			status;
	uint8_t		cksum = 0;
	int			i;
	uint8_t		respHead[3];
	uint8_t		respTail[2];

	// Read SOP, CMD, and payload length
	status = readEmtr(pCtrl, respHead, sizeof(respHead));
	if (status < 0) {
		goto exitError;
	}

	// First byte is expected to SOP
	if (respHead[0] != MSG_CHAR_SOP) {
		gc_err("Expected SOP, got %02X", respHead[0]);
		status = ESP_FAIL;
		goto exitError;
	}

	// Third byte is payload length
	// Need to grab it now before potential branch to exitFlush
	payloadLen = respHead[2];

	// Second byte is the response CMD

	if (ret) {
		// Expecting to read back data
		// Check for expected CMD value
		if (cmd != respHead[1]) {
			gc_err("Expected CMD %02X, got %02X", cmd, respHead[1]);
			status = ESP_FAIL;
			goto exitFlush;
		}
	} else {
		// No ret buffer provided, force zero-length read
		retSz = 0;

		// Not expecting return data, look for generic response
		if (0xF0 != respHead[1]) {
			gc_err("Expected CMD == %02X, got %02X", cmd, respHead[1]);
			status = ESP_FAIL;
			goto exitFlush;
		}
	}

	// Start checksum with CMD and Payload Length
	cksum ^= respHead[1];
	cksum ^= respHead[2];

	if (payloadLen > 0) {
		if (payloadLen > retSz) {
			gc_err("Message length (%d) exceeds space (%d)", payloadLen, retSz);
			status = ESP_FAIL;
			goto exitFlush;
		}

		// Read payload
		status = readEmtr(pCtrl, ret, payloadLen);
		if (status < 0) {
			goto exitError;
		}

		// Add payload to the checksum
		for (i = 0; i < payloadLen; i++)
			cksum ^= ret[i];
	}

	// Read CKSUM and EOP
	status = readEmtr(pCtrl, respTail, sizeof(respTail));
	if (status < 0) {
		goto exitError;
	}

	// Validate checksum
	if (respTail[0] != cksum) {
		gc_err("Checksum failed, expected %02X, got %02X", respTail[0], cksum);
		status = ESP_FAIL;
		goto exitError;
	}

	return payloadLen;

exitFlush:
	flushEmtrMsg(pCtrl, payloadLen + 2);
exitError:
	return status;
}


/**
 * \brief Send command and optionally read back data
 *
 */
static esp_err_t doCommand(
	emtrCtrl_t *	pCtrl,
	uint8_t			cmd,
	uint8_t *		payload,
	uint8_t *		retBuf,
	int *			retLen
)
{
	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	int		status;
	int		recvLen;
	int		bufSz    = *retLen;
	int		maxRetry = 3;
	int		retryCt  = 0;

	*retLen = 0;

	for (retryCt = 0; retryCt < maxRetry; retryCt++) {
		if (retryCt > 0) {
			gc_dbg("doCommand(%02X): retry #%d", cmd, retryCt);

			if (retryCt == (maxRetry - 1)) {
				// Last retry - try a hard reset this time
				resetEmtr(pCtrl);
			}

			// Delay before retry
			EMTR_SLEEP_MS(100);

			// Flush UART receive FIFO
			uart_flush_input(uartConf->uart);
		}

		// Send the command
		status = sendEmtrCommand(pCtrl, cmd, payload);
		if (ESP_OK != status) {
			gc_err("doCommand(%02X): sendEmtrCommand returned %d", cmd, status);
			continue;
		}

		// Read the response
		recvLen = readEmtrResponse(pCtrl, cmd, retBuf, bufSz);
		if (0 > recvLen) {
			gc_err("doCommand(%02X): readEmtrResponse returned %d", cmd, recvLen);
			continue;
		}

		if (!pCtrl->curDeviceStatus.emtrCommUp) {
			// EMTR communication was restored
			pCtrl->curDeviceStatus.emtrCommUp = true;
			notify(pCtrl, emtrCbId_device, emtrEvtCode_commUp, NULL);
		}

		if (NULL == retBuf || 0 >= bufSz) {
			// Not expecting payload data
			recvLen = 0;
		}

		*retLen = recvLen;
		return ESP_OK;
	}

	// If this point is reached retries have been exhausted
	gc_err("doCommand(%02X): exhausted retries", cmd);

	if (pCtrl->curDeviceStatus.emtrCommUp) {
		// EMTR communication was lost
		pCtrl->curDeviceStatus.emtrCommUp = false;
		notify(pCtrl, emtrCbId_device, emtrEvtCode_commDown, NULL);
	}

	return 	ESP_FAIL;
}


/**
 * \brief Send command to set socket on or off
 */
static esp_err_t setSocketState(emtrCtrl_t * pCtrl, int sockNum, bool turnOn)
{
	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (!sCtrl) {
		return ESP_ERR_INVALID_ARG;
	}

	// Reference the command codes for the indexed socket
	sockInfo_t *	sInfo = sCtrl->info;

	uint8_t	cmdCode = turnOn ? sInfo->cmdTurnOn : sInfo->cmdTurnOff;

	//gc_dbg("EMTR %s: turnOn: %d, sockNum %d, cmdCode %02X", __FUNCTION__, turnOn, sockNum, cmdCode);

	// Execute command, no payload, no response expected
	int	ioLen = 0;
	if (doCommand(pCtrl, cmdCode, NULL, NULL, &ioLen) != ESP_OK)
		return ESP_FAIL;

	return ESP_OK;
}


/**
 * \brief Check for change of states that cause notifications
 */
static void checkChangeOfState(emtrCtrl_t * pCtrl)
{
	emtrEvtCode_t	evtCode;
	emtrEvtData_t	evtData;
	emtrCbId_t		cbId    = emtrCbId_device;

	// Check for changes to the overall device

	// Check for change of epoch
	if (pCtrl->oldDeviceStatus.epoch != pCtrl->curDeviceStatus.epoch) {
		pCtrl->oldDeviceStatus.epoch = pCtrl->curDeviceStatus.epoch;

		evtData.epoch.value = pCtrl->curDeviceStatus.epoch;
		notify(pCtrl, cbId, emtrEvtCode_epoch, &evtData);
	}

	// Check for change of temperature
	if (pCtrl->oldDeviceStatus.temperature != pCtrl->curDeviceStatus.temperature) {
		pCtrl->oldDeviceStatus.temperature = pCtrl->curDeviceStatus.temperature;

		evtData.temperature.value = pCtrl->curDeviceStatus.temperature;
		notify(pCtrl, cbId, emtrEvtCode_temperature, &evtData);
	}

	// Check for change of communication state
	if (pCtrl->oldDeviceStatus.emtrCommUp != pCtrl->curDeviceStatus.emtrCommUp) {
		pCtrl->oldDeviceStatus.emtrCommUp = pCtrl->curDeviceStatus.emtrCommUp;

		emtrEvtCode_t	ev =
			pCtrl->curDeviceStatus.emtrCommUp ? emtrEvtCode_commUp : emtrEvtCode_commDown;

		notify(pCtrl, cbId, ev, NULL);
	}

	// Check for changes in each socket state
	int				sIdx;
	socketCtrl_t *	sCtrl = pCtrl->socketCtrl;

	for (sIdx = 0; sIdx < pCtrl->conf.numSockets; sIdx++, sCtrl++) {
		sockInfo_t *	sInfo = sCtrl->info;

		int			sNum = sInfo->sockNum;
		emtrCbId_t	cbId = sInfo->callbackId;

		// Check for change of "on" state
		if (sCtrl->relayActive.curState != sCtrl->relayActive.newState) {
			sCtrl->relayActive.curState = sCtrl->relayActive.newState;

			sCtrl->stat.isOn = SOCKET_IS_ON(sCtrl);

			if (sCtrl->stat.isOn) {
				//gc_dbg("Socket %d is on", sNum);
				evtCode = emtrEvtCode_socketOn;
			} else {
				//gc_dbg("Socket %d is off", sNum);
				evtCode = emtrEvtCode_socketOff;
			}

			evtData.onOff.socketNum = sNum;

			notify(pCtrl, cbId, evtCode, &evtData);

			// When a socket changes state between on and off must delay
			// the next command until the EMTR is done sampling the
			// power characteristics of the attached device
			pCtrl->holdOffCommand = true;
		}

		// Check for change of plug insertion state
		if (sCtrl->isPlugged.curState != sCtrl->isPlugged.newState) {
			sCtrl->isPlugged.curState = sCtrl->isPlugged.newState;

			sCtrl->stat.isPlugged = SOCKET_IS_PLUGGED(sCtrl);

			if (sCtrl->stat.isPlugged) {
				gc_dbg("Socket %d plug inserted", sNum);
				evtCode = emtrEvtCode_plugInserted;
			} else {
				gc_dbg("Socket %d plug removed", sNum);
				evtCode = emtrEvtCode_plugRemoved;
			}

			evtData.plug.socketNum = sNum;

			notify(pCtrl, cbId, evtCode, &evtData);
		}

		// Check for state time change
		uint32_t	diffTime = pCtrl->curTime - sCtrl->cosTimeRelay;
		if (sCtrl->oldCosTimeRelay != diffTime) {
			sCtrl->oldCosTimeRelay = diffTime;

			evtData.stateTime.socketNum = sNum;
			evtData.stateTime.value     = diffTime;

			notify(pCtrl, cbId, emtrEvtCode_stateTime, &evtData);
		}
	}
}


/**
 * \brief Check for change of states that cause notifications
 */
static void checkChangeOfEnergy(emtrCtrl_t * pCtrl)
{
	int				sIdx;
	socketCtrl_t *	sCtrl = pCtrl->socketCtrl;

	for (sIdx = 0; sIdx < pCtrl->conf.numSockets; sIdx++, sCtrl++) {
		sockInfo_t *	sInfo = sCtrl->info;

		emtrInstEnergy_t *	cur = &sCtrl->stat.instEnergy;
		oldEnergy_t *		old = &sCtrl->oldEnergy;
		emtrEvtData_t		evtData;

		evtData.energy.socketNum = sInfo->sockNum;

		emtrCbId_t	cbId = sInfo->callbackId;

		// Check for change in volts
		if (old->dVolts != cur->dVolts) {
			old->dVolts = cur->dVolts;

			evtData.energy.value = cur->dVolts;
			notify(pCtrl, cbId, emtrEvtCode_volts, &evtData);
		}

		// Check for change in amps
		if (old->mAmps != cur->mAmps) {
			old->mAmps = cur->mAmps;

			evtData.energy.value = cur->mAmps;
			notify(pCtrl, cbId, emtrEvtCode_amps, &evtData);
		}

		// Check for change in dWatts
		if (old->dWatts != cur->dWatts) {
			old->dWatts = cur->dWatts;

			evtData.energy.value = cur->dWatts;
			notify(pCtrl, cbId, emtrEvtCode_watts, &evtData);
		}

		// Check for change in power factor
		if (old->pf != cur->powerFactor) {
			old->pf = cur->powerFactor;

			evtData.energy.value = cur->powerFactor;
			notify(pCtrl, cbId, emtrEvtCode_powerFactor, &evtData);
		}

		// Check for change in Watt-Hours
		if (old->dWattHours != sCtrl->stat.dWattHours) {
			old->dWattHours = sCtrl->stat.dWattHours;

			evtData.dWattHours.value = sCtrl->stat.dWattHours;
			notify(pCtrl, cbId, emtrEvtCode_wattHours, &evtData);
		}
	}
}


/**
 * \brief Read the device status
 */
static esp_err_t readDeviceState(emtrCtrl_t * pCtrl)
{
	uint8_t		resp[9];		// Expect 8 or 9 bytes returned
	int			ioLen;
	int			idx;

	// Execute the Get Status command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, EMTR_CMD_GET_STATUS, NULL, resp, &ioLen) != ESP_OK)
		return ESP_FAIL;
	if (ioLen < 8) {
		return ESP_FAIL;
	}

	// Offset  Len  Assignment
	//      0    1  Socket 2 status
	//      1    1  Socket 1 status
	//      2    2  Temperature, degrees C (x10?)
	//      4    4  Epoch (seconds since start)
	//      8    1  Device flags
	idx = 0;

	//gc_hexDump2("EMTR status", resp, 8, true);

	// Unpack the status flag bytes
	// Reference socket number in descending order
	int		oIdx;
	int		sNum = pCtrl->conf.numSockets;
	for (oIdx = 0; oIdx < pCtrl->conf.numSockets; oIdx++, sNum--) {
		socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, sNum);
		if (!sCtrl) {
			gc_err("Failed to get socket %d control structure", sNum);
			continue;
		}

		uint8_t			flags;
		boolState_t		rdState;

		// Read the status flags byte
		flags = resp[idx];
		idx  += 1;

		// Check "isOn" state
		rdState = (flags & OSTATUS_SOCKET_ON) ? boolState_true : boolState_false;

		// Check for change of state
		if (sCtrl->relayActive.newState != rdState) {
			sCtrl->relayActive.newState = rdState;
			sCtrl->cosTimeRelay = pCtrl->curTime;
		}

		// Check "isPlugged" state
		rdState = (flags & OSTATUS_PLUG_DETECT) ? boolState_true : boolState_false;
		// Check for change of state
		if (sCtrl->isPlugged.newState != rdState) {
			sCtrl->isPlugged.newState = rdState;
		}
	}

	// Unpack the device-level information

	// Temperature is presented in degrees C
	// 2 bytes, big-endian
	pCtrl->curDeviceStatus.temperature =
		((uint16_t)resp[idx + 0]) << 8 |
		((uint16_t)resp[idx + 1]) << 0
	;
	idx += 2;

	// Epoch is presented in seconds
	// 4 bytes, big-endian
	pCtrl->curDeviceStatus.epoch =
		((uint32_t)resp[idx + 0]) << 24 |
		((uint32_t)resp[idx + 1]) << 16 |
		((uint32_t)resp[idx + 2]) <<  8 |
		((uint32_t)resp[idx + 3]) <<  0
	;
	idx += 4;

	// If the device status byte is present, extract that
	if (9 == ioLen) {
		pCtrl->curDeviceStatus.flags = resp[idx];
		idx += 1;
	} else {
		pCtrl->curDeviceStatus.flags = 0;
	}

	return ESP_OK;
}


/**
 * \brief Read total Watt-hours for both outlets
 */
static esp_err_t readWattHours(emtrCtrl_t * pCtrl)
{
	uint8_t		resp[8];		// Expect 8 bytes returned
	int			ioLen;
	int			idx;

	// Execute the Get KWH command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, EMTR_CMD_GET_KWH, NULL, resp, &ioLen) != ESP_OK)
		return ESP_FAIL;
	if (ioLen != sizeof(resp)) {
		return ESP_FAIL;
	}

	// Watt-hours are stored as 4-byte big-endian values starting at offset 0
	// Offset  Len  Assignment
	//      0    4  Socket 2 Watt-Hours
	//      4    4  Socket 1 Watt-Hours

	idx = 0;

	// Set up to read sockets in descending order
	int		oIdx;
	int		sNum = pCtrl->conf.numSockets;

	for (oIdx = 0; oIdx < pCtrl->conf.numSockets; oIdx++, sNum--) {

		socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, sNum);
		if (!sCtrl) {
			gc_err("Failed to get socket %d control structure", sNum);
			continue;
		}

		sCtrl->stat.dWattHours =
			((uint32_t)resp[idx + 0]) << 24 |
			((uint32_t)resp[idx + 1]) << 16 |
			((uint32_t)resp[idx + 2]) <<  8 |
			((uint32_t)resp[idx + 3]) <<  0
		;
		idx += 4;
	}

	return ESP_OK;
}


static void initEmtrAccumulator(accum_t * eAvg)
{
	// Reset min/max accumulators
	eAvg->min = 0xffffffff;
	eAvg->max = 0;

	// Initialize the average value accumulator
	eAvg->sampleCt = 0;
	eAvg->sum      = 0;
	eAvg->avg      = 0;

	// Initialize the sample ring buffer
	eAvg->preSampleCt  = 0;
	eAvg->preSamplePut = 0;
	eAvg->preSampleGet = 0;
}


static void updateEmtrAccumulator(accum_t * eAvg, int32_t newSample)
{
	// Wait for the sample ring buffer to be loaded before applying its contents
	if (EMTR_PRE_SAMPLE_SZ == eAvg->preSampleCt) {
		uint32_t	value;

		// Read the oldest value from the ring buffer
		value = eAvg->preSample[eAvg->preSampleGet];
		if (++eAvg->preSampleGet == EMTR_PRE_SAMPLE_SZ) {
			eAvg->preSampleGet = 0;
		}
		eAvg->preSampleCt -= 1;

		// Apply the value to the min/max accumulators
		if (value < eAvg->min)
			eAvg->min = value;
		if (value > eAvg->max)
			eAvg->max = value;

		// Apply the value to the average accumulator
		eAvg->sum += (uint64_t)value;
		eAvg->sampleCt += 1;

		// Update the running average
		eAvg->avg = (uint32_t)(eAvg->sum / (uint64_t)eAvg->sampleCt);
	}

	// Add the newest sample to the ring buffer
	eAvg->preSample[eAvg->preSamplePut] = newSample;
	if (++eAvg->preSamplePut == EMTR_PRE_SAMPLE_SZ) {
		eAvg->preSamplePut = 0;
	}
	eAvg->preSampleCt += 1;
}


static void updateEmtrAccumulators(accEnergy_t * eAcc, emtrInstEnergy_t * inst)
{
	uint64_t	curTimeMs = timeMgrGetUptimeMs();

	if (eAcc->initial) {
		// First pass after accumulators were reset
		eAcc->initial   = false;
		// Ignore first 2.000 seconds of sample data
		eAcc->holdOffMs = 2000 + curTimeMs;

		initEmtrAccumulator(&eAcc->dVolts);
		initEmtrAccumulator(&eAcc->mAmps);
		initEmtrAccumulator(&eAcc->dWatts);
		initEmtrAccumulator(&eAcc->powerFactor);
	} else if (curTimeMs >= eAcc->holdOffMs) {
		updateEmtrAccumulator(&eAcc->dVolts, inst->dVolts);
		updateEmtrAccumulator(&eAcc->mAmps, inst->mAmps);
		updateEmtrAccumulator(&eAcc->dWatts, inst->dWatts);
		updateEmtrAccumulator(&eAcc->powerFactor, inst->powerFactor);
	}
}


static void clearEmtrAccumulator(accum_t * acc, int32_t value)
{
	acc->min = value;
	acc->max = value;
	acc->avg = 0;
}


static void clearEmtrAccumulators(accEnergy_t * eAcc)
{
	clearEmtrAccumulator(&eAcc->dVolts, 0);
	clearEmtrAccumulator(&eAcc->mAmps, 0);
	clearEmtrAccumulator(&eAcc->dWatts, 0);
	clearEmtrAccumulator(&eAcc->powerFactor, 100);

	// Reset average computation
	eAcc->initial = true;
}


static void cpyAccum(emtrAvgEnergy_t * dst, accum_t * src)
{
	dst->min      = src->min;
	dst->max      = src->max;
	dst->avg      = src->avg;
	dst->sampleCt = src->sampleCt;
}


/**
 * \brief Read and clear the selected set of accumulators
 */
static esp_err_t getAccChan(
	emtrCtrl_t *		pCtrl,
	int					sockNum,
	int					accChan,
	emtrAccEnergy_t *	ret
)
{
	if (!pCtrl || !ret || accChan >= EMTR_ACC_NUM_CHANS) {
		return ESP_ERR_INVALID_ARG;
	}

	// Get the control structure for this socket
	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (!sCtrl) {
		return ESP_ERR_INVALID_ARG;
	}

	// Shorthand reference to the selected energy accumulator
	accEnergy_t *	eAcc = &sCtrl->eAccChan[accChan];

	int		status;

	// Validate driver state and parameters and lock the mutex
	if ((status = checkRequest(pCtrl, sockNum, true)) == ESP_OK) {
		// At this point mutex is locked

		// Copy back current accumulator values
		cpyAccum(&ret->dVolts, &eAcc->dVolts);
		cpyAccum(&ret->mAmps, &eAcc->mAmps);
		cpyAccum(&ret->dWatts, &eAcc->dWatts);
		cpyAccum(&ret->powerFactor, &eAcc->powerFactor);

		// Clear the accumulators
		clearEmtrAccumulators(eAcc);

		EMTR_MUTEX_PUT(pCtrl);
	}

	return status;
}


/**
 * \brief Read instant power information for both sockets
 */
static esp_err_t readPower(emtrCtrl_t * pCtrl)
{
	uint8_t			resp[16];		// Expect 16 bytes returned
	int				ioLen;
	int				idx;

	// Execute the Get Instant power info command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, EMTR_CMD_GET_INSTANT_PWR, NULL, resp, &ioLen) != ESP_OK)
		return ESP_FAIL;
	if (ioLen != sizeof(resp)) {
		return ESP_FAIL;
	}

	// Unpack power data
	// Offset  Len  Assignment
	//      0    2  Socket 2 volts x 10
	//      2    2  Socket 2 amps  x 1000 (mAmps)
	//      4    2  Socket 2 watts x 10
	//      6    2  Socket 2 power factor
	//      8    2  Socket 1 volts x 10
	//     10    2  Socket 1 amps  x 1000 (mAmps)
	//     12    2  Socket 1 watts x 10
	//     14    2  Socket 1 power factor
	idx = 0;

	// Read sockets in descending order
	int		oIdx;
	int		sNum = pCtrl->conf.numSockets;
	for (oIdx = 0; oIdx < pCtrl->conf.numSockets; oIdx++, sNum--) {

		socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, sNum);
		if (!sCtrl) {
			gc_err("Failed to get socket %d control structure", sNum);
			continue;
		}

		emtrInstEnergy_t *	eInst = &sCtrl->stat.instEnergy;

		eInst->dVolts =
			((uint16_t)resp[idx + 0]) << 8 |
			((uint16_t)resp[idx + 1]) << 0
		;
		idx += 2;

		eInst->mAmps =
			((uint16_t)resp[idx + 0]) << 8 |
			((uint16_t)resp[idx + 1]) << 0
		;
		idx += 2;

		eInst->dWatts =
			((uint16_t)resp[idx + 0]) << 8 |
			((uint16_t)resp[idx + 1]) << 0
		;
		idx += 2;

		eInst->powerFactor =
			((int)resp[idx + 0]) << 8 |
			((int)resp[idx + 1]) << 0
		;
		idx += 2;

		// Apply instant energy reading to accumulators

		int				chNum;
		accEnergy_t *	eAcc = sCtrl->eAccChan;
		for (chNum = 0; chNum < EMTR_ACC_NUM_CHANS; chNum++, eAcc++) {
			updateEmtrAccumulators(eAcc, eInst);
		}

		// Update the running total of Watts
		sCtrl->stat.dWattsTotal += (uint64_t)eInst->dWatts;
	}

	return ESP_OK;
}


static esp_err_t readPowerSignatureTimestamp(
	emtrCtrl_t *	pCtrl,
	int				sockNum,
	uint32_t *		timestamp,
	uint8_t *		reason
)
{
	if (!timestamp || !reason) {
		return ESP_ERR_INVALID_ARG;
	}

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (!sCtrl) {
		return ESP_ERR_INVALID_ARG;
	}

	sockInfo_t *	sInfo   = sCtrl->info;
	uint8_t			cmdCode = sInfo->cmdReadSigTs;

	int		ioLen;
	uint8_t	resp[5];		// Expect 5 bytes returned

	// Execute the command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, cmdCode, NULL, resp, &ioLen) != ESP_OK) {
		return ESP_FAIL;
	}
	if (ioLen != sizeof(resp)) {
		gc_err("response size (%d) not expected value", ioLen);
		return ESP_FAIL;
	}

	// Unpack the response
	// Bytes 0-3  timestamp
	// Byte  4    reason
	int	idx = 0;

	*timestamp =
		((uint32_t)resp[idx + 0]) << 24 |
		((uint32_t)resp[idx + 1]) << 16 |
		((uint32_t)resp[idx + 2]) <<  8 |
		((uint32_t)resp[idx + 3]) <<  0
	;
	idx += 4;

	*reason = resp[idx];

	return ESP_OK;
}


static esp_err_t readPowerSignaturePage(
	emtrCtrl_t *	pCtrl,
	int				sockNum,
	uint8_t			pageNum,
	uint32_t *		timestamp,
	uint8_t *		rdBuf,
	int				rdSize
)
{
	int			ioLen;
	uint8_t		payload[4];
	uint8_t		resp[133];	// Expecting 133 bytes back
	int			idx = 0;

	if (pageNum > 47 || !timestamp || !rdBuf) {
		gc_err("readPowerSignaturePage: bad parameter");
		return ESP_FAIL;
	}

	socketCtrl_t *	sCtrl = getSocketCtrl(pCtrl, (uint8_t)sockNum);
	if (!sCtrl) {
		return ESP_ERR_INVALID_ARG;
	}

	sockInfo_t *	sInfo   = sCtrl->info;
	uint8_t			cmdCode = sInfo->cmdReadSigPg;

	payload[0] = pageNum;
	payload[1] = 0;
	payload[2] = 0;
	payload[3] = 0;

	// Execute the command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, cmdCode, payload, resp, &ioLen) != ESP_OK) {
		gc_err("readPowerSignaturePage: doCommand failed");
		return ESP_FAIL;
	}
	if (ioLen != sizeof(resp)) {
		gc_err("readPowerSignaturePage: expected to read %d bytes, got %d", sizeof(resp), ioLen);
		return ESP_FAIL;
	}

	// Unpack the timestamp for this page
	*timestamp =
		((uint32_t)resp[idx + 0]) << 24 |
		((uint32_t)resp[idx + 1]) << 16 |
		((uint32_t)resp[idx + 2]) <<  8 |
		((uint32_t)resp[idx + 3]) <<  0
	;
	idx += 4;

	// Verify the returned page number matches the requested page
	if (resp[idx] != pageNum) {
		gc_err("readPowerSignaturePage: expected to read page %d, got %d", pageNum, resp[idx]);
		return ESP_FAIL;
	}
	idx += 1;

	// Copy back the page data
	memcpy(rdBuf, &resp[idx], rdSize);
	return ESP_OK;
}


/**
 * \brief Read power signature information
 */
static esp_err_t readSignature(
	emtrCtrl_t *	pCtrl,
	int				sockNum,
	uint32_t *		timestamp,
	uint8_t *		reason,
	uint8_t *		pBuf,
	int				rdLen
)
{
	uint32_t	ts2 = 0;
	int			status;
	int			rdSize;
	int			rdCount;
	uint8_t		pageNum = 0;

	// Stop compiler warning
	*timestamp = 0;

	// First read the timestamp for when the signature was last updated
	status = readPowerSignatureTimestamp(
		pCtrl,
		sockNum,
		timestamp,
		reason
	);
	if (ESP_OK != status) {
		gc_err("Failed to read signature timestamp");
		return status;
	}

	for (rdCount = 0; rdCount < rdLen; rdCount += rdSize, pageNum++) {
		if (rdLen - rdCount > PWR_SIGNATURE_PAGE_SZ)
			rdSize = PWR_SIGNATURE_PAGE_SZ;
		else
			rdSize = rdLen - rdCount;

		status = readPowerSignaturePage(
			pCtrl,
			sockNum,
			pageNum,
			&ts2,
			(pBuf + rdCount),
			rdSize
		);
		if (ESP_OK != status) {
			gc_err("Failed to read page %u", pageNum);
			return status;
		}

		// Make sure the data did not re-sample during the read
		if (*timestamp != ts2) {
			gc_err("Signature timestamp mismatch: expected %08X, got %08X", *timestamp, ts2);
			return ESP_FAIL;
		}
	}

	return ESP_OK;
}


/*
 * \brief Set and verify EMTR board run mode
 */
static esp_err_t emtrRunModeSet(
	emtrCtrl_t *	pCtrl,
	emtrRunMode_t	targetMode,
	uint8_t *		version
)
{
	int		status;

	if (!pCtrl) {
		return ESP_FAIL;
	}

	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	// Flush the transmit FIFO
	uart_wait_tx_done(uartConf->uart, pdMS_TO_TICKS(200));

	// Reset the board into the desired state
	if ((status = resetEmtrBoard(pCtrl, targetMode)) != ESP_OK) {
		gc_err("resetEmtrBoard error %d", status);
		return status;
	}

	// Flush the receive FIFO
	uart_flush_input(uartConf->uart);

	// Query the EMTR to determine its actual mode
	if ((status = emtrRunModeGet(pCtrl, &pCtrl->emtrMode, version)) != ESP_OK) {
		gc_err("emtrRunModeGet error %d", status);
		return status;
	}

	// Verify EMTR is in the target mode
	if (pCtrl->emtrMode != targetMode) {
		gc_err("EMTR mode (%d) not expected value (%d)", pCtrl->emtrMode, targetMode);
		return ESP_FAIL;
	}

	pCtrl->pollSampleCt = 0;
	return ESP_OK;
}


/**
 * \brief Query the EMTR for its run mode and firmware version
 */
static esp_err_t emtrRunModeGet(
	emtrCtrl_t *		pCtrl,
	emtrRunMode_t *		mode,
	uint8_t *			version
)
{
	if (!mode) {
		return ESP_FAIL;
	}

	uint8_t		cmd = EMTR_CMD_GET_STATE;
	uint8_t		resp[4];		// Expect 4 bytes returned
	int			ioLen;

	// Execute the command
	ioLen = sizeof(resp);
	if (doCommand(pCtrl, cmd, NULL, resp, &ioLen) != ESP_OK)
		return ESP_FAIL;
	if (ioLen != sizeof(resp)) {
		gc_err("EMTR returned %d bytes, expecting %d", ioLen, sizeof(resp));
		return ESP_FAIL;
	}

	if (resp[0] == 'E') {
		*mode = emtrRunMode_application;
	} else if (resp[0] == 'B') {
		*mode = emtrRunMode_bootLoader;
	} else {
		gc_hexDump2("EMTR response to mode query", resp, sizeof(resp), true);
		return ESP_FAIL;
	}

	if (NULL != version) {
		version[0] = resp[1];
		version[1] = resp[2];
		version[2] = resp[3];
	}

	return ESP_OK;
}


/**
 * \brief Start XMODEM transfer
 */
static esp_err_t startXmodemTransfer(emtrCtrl_t * pCtrl)
{
	uint8_t		cmd = EMTR_CMD_START_XMODEM;
	int			ioLen;

	// Execute the command - expect no return
	ioLen = 0;
	if (doCommand(pCtrl, cmd, NULL, NULL, &ioLen) != ESP_OK)
		return ESP_FAIL;

	return ESP_OK;
}


static esp_err_t emtrUpgrade(emtrCtrl_t * pCtrl, const uint8_t * fwFile)
{
	esp_err_t	status;

	// Set up for firmware upgrade operation
	gc_dbg("Start EMTR boot loader");

	EMTR_MUTEX_GET(pCtrl);

	// Place EMTR into boot loader mode
	xTimerStop(pCtrl->timer, pdMS_TO_TICKS(20));
	status = emtrRunModeSet(pCtrl, emtrRunMode_bootLoader, NULL);

	EMTR_MUTEX_PUT(pCtrl);

	if (ESP_OK != status) {
		gc_err("emtrRunModeSet error %d", status);
		goto exitUpdate;
	}

	gc_dbg("Start XModem transfer");

	// Signal the boot loader to start the XMODEM transfer
	if ((status = startXmodemTransfer(pCtrl)) != ESP_OK) {
		gc_err("startXmodemTransfer error failed");
		goto exitBootloader;
	}

	// Configure XMODEM to transfer 128 bytes at a time
	csXmodemCfg_t	xmCfg;

	memset(&xmCfg, 0, sizeof(xmCfg));
	xmCfg.blockSize = 128;

	gc_dbg("Waiting for XModem handshake");

	// Shorthand reference to UART configuration
	emtrUartConf_t *	uartConf = &pCtrl->conf.uartCmd;

	csXmHandle_t	xmHandle;

	if ((status = csXmSendOpen(uartConf->uart, &xmHandle, &xmCfg)) != ESP_OK) {
		gc_err("csXmSendOpen error %d", status);
		goto exitBootloader;
	}

	gc_dbg("XModem transfer in progress");

	// Reference the file header
	const csFwHdr_t *	hdr = (const csFwHdr_t *)fwFile;

	// File data begins after the header
	uint8_t *	fileData  = (uint8_t *)(hdr);
	int			fileSize  = hdr->dataLen + sizeof(*hdr);
	bool		abortFlag = false;
	int			writeCt;
	int			remainder;
	int			xferSz;

	for (writeCt = 0; writeCt < fileSize; writeCt += xferSz) {
		remainder = fileSize - writeCt;
		xferSz = (remainder > xmCfg.blockSize) ? xmCfg.blockSize : remainder;

		status = csXmSendWrite(xmHandle, (fileData + writeCt), xferSz);
		if (ESP_OK != status) {
			gc_err("csXmSendWrite error %d", status);
			gc_err("(%u bytes sent)", writeCt);
			abortFlag = true;
			goto exitXm;
		}
	}

	gc_dbg("%u bytes written to EMTR", writeCt);

exitXm:
	gc_dbg("Close Xmodem session");
	if ((status = csXmSendClose(xmHandle, abortFlag)) != ESP_OK) {
		gc_err("csXmSendClose error %d", status);
	}

exitBootloader:
	gc_dbg("Exit boot loader mode");

	uint8_t	version[3];
	status = emtrRunModeSet(pCtrl, emtrRunMode_application, version);
	if (ESP_OK == status) {
		// Update the EMTR version
		snprintf(
			pCtrl->fwVersion, sizeof(pCtrl->fwVersion),
			"%u.%u.%u",
			version[0], version[1], version[2]
		);
		gc_dbg("EMTR updated to %s", pCtrl->fwVersion);
	}

exitUpdate:
	xTimerReset(pCtrl->timer, pdMS_TO_TICKS(20));
	return status;
}


static esp_err_t handleTimer(emtrCtrl_t * pCtrl, bool * didReadPower, uint8_t * flags)
{
	*didReadPower = false;
	*flags        = 0;

	// If EMTR is in boot loader mode, skip the rest
	if (emtrRunMode_application != pCtrl->emtrMode)
		return ESP_OK;

	esp_err_t			status;

	// Read device and socket status each pass
	if ((status = readDeviceState(pCtrl)) != ESP_OK) {
		gc_err("Error %d from readDeviceState()", status);
		return status;
	}

	*flags = pCtrl->curDeviceStatus.flags;

	// While EMTR is in pause state don't do anything to generate data updates
	if (pCtrl->pause)
		return ESP_OK;

	// Check for changes of state
	checkChangeOfState(pCtrl);

	// Read power information once per second
	if (++pCtrl->pollSampleCt < EMTR_POLL_CYCLES_PER_SEC)
		return ESP_OK;

	// Reset the counter
	pCtrl->pollSampleCt = 0;

	if ((status = readPower(pCtrl)) != ESP_OK) {
		gc_err("Error %d from readPower()", status);
		return status;
	}

	if ((status = readWattHours(pCtrl)) != ESP_OK) {
		gc_err("Error %d from readWattHours()", status);
		return status;
	}

	// Check for changes and send notifications
	checkChangeOfEnergy(pCtrl);

	*didReadPower = true;
	return ESP_OK;
}


/**
 * \brief The control task
 */
static void ctrlTask(void * params)
{
	emtrCtrl_t *		pCtrl = (emtrCtrl_t *)params;
	emtrMsg_t			msg;
	int					oNum;
	uint8_t				devFlags;
	bool				didReadPower;

	//attempt to do a single read before init
	if (readDeviceState(pCtrl) == ESP_OK) {
		socketCtrl_t * sCtrl = pCtrl->socketCtrl;
		int	i;
		for (i = 0; i < pCtrl->conf.numSockets; i++, sCtrl++) {
			sCtrl->relayActive.curState = sCtrl->relayActive.newState;
			sCtrl->isPlugged.curState = sCtrl->isPlugged.newState;
			sCtrl->stat.isOn = SOCKET_IS_ON(sCtrl);
			sCtrl->stat.isPlugged = SOCKET_IS_PLUGGED(sCtrl);
		}
	}
	while (1)
	{
		if (pCtrl->shutdown) {
			pCtrl->isRunning = false;
			vTaskSuspend(NULL);
		}

		xQueueReceive(pCtrl->queue, &msg, portMAX_DELAY);

		pCtrl->curTime = EMTR_TIME_SEC();

		// Lock the mutex
		EMTR_MUTEX_GET(pCtrl);

		switch (msg.msgCode)
		{
		case emtrtMsgCode_timer:
			(void)handleTimer(pCtrl, &didReadPower, &devFlags);

			// Check for factory reset being signaled from the EMTR
			if (DEV_STATUS_FACTORY_RESET & devFlags) {
				// EMTR signaled factory reset requested
				xTimerStop(pCtrl->timer, pdMS_TO_TICKS(10));
				if(mfgDataIsEnabled())// todo jonw remove for production
					mfgDataDisable();// todo jonw remove for production
				csSelfTestEnable();// todo jonw remove for production
				csControlFactoryReset();
			} else {
				// Schedule the next read
				xTimerReset(pCtrl->timer, pdMS_TO_TICKS(10));
			}
			break;

		case emtrMsgCode_socketOn:
			oNum = (int)msg.msgData.socketNum;

			if (oNum < 1 || oNum > pCtrl->conf.numSockets) {
				gc_err("Out of range socket number: %d", oNum);
				break;
			}

			//gc_dbg("Turn on socket %d", oNum);

			// Send command to turn on socket
			//pCtrl->spyEnable = true;
			if (setSocketState(pCtrl, oNum, true) != ESP_OK) {
				gc_err("setSocketState(ON) failed");
			}
			//pCtrl->spyEnable = false;
			break;

		case emtrMsgCode_socketOff:
			oNum = (int)msg.msgData.socketNum;

			if (oNum < 1 || oNum > pCtrl->conf.numSockets) {
				gc_err("Out of range socket number: %d", oNum);
				break;
			}

			//gc_dbg("Turn off socket %d", oNum);

			//pCtrl->spyEnable = true;
			// Send command to turn off socket
			if (setSocketState(pCtrl, oNum, false) != ESP_OK) {
				gc_err("setSocketState(OFF) failed");
			}
			//pCtrl->spyEnable = false;
			break;

		case emtrMsgCode_shutDown:
			pCtrl->shutdown = true;
			break;

		case emtrMsgCode_pause:
			if (!pCtrl->pause) {
				gc_dbg("Pause EMTR operation");
				pCtrl->pause = true;
			}
			break;

		case emtrMsgCode_resume:
			if (pCtrl->pause) {
				gc_dbg("Resume EMTR operation");
				pCtrl->pause = false;
				pCtrl->pollSampleCt = 0;
			}
			break;

		default:
			gc_err("Undefined message id %d", msg.msgCode);
			break;
		}

		EMTR_MUTEX_PUT(pCtrl);

		if (didReadPower) {
			didReadPower = false;
#if (ENA_PERIOD_REPORT)
			printDebug(pCtrl, pCtrl->curTime);
#endif
		}
	}
}


static void sysEventCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	emtrCtrl_t *	pCtrl = CS_ADR2PTR(cbData);
	emtrMsg_t		msg;

	switch ((csCtrlEvtCode_t)evtCode)
	{
	case csCtrlEvtCode_rebootArmed:
	case csCtrlEvtCode_rebooting:
	case csCtrlEvtCode_resetProvision:
	case csCtrlEvtCode_resetFactory:
		msg.msgCode = emtrMsgCode_pause;
		break;

	case csCtrlEvtCode_fwUpgradeStart:
		// Pause some EMTR operations during firmware updates
		msg.msgCode = emtrMsgCode_pause;
		break;

	case csCtrlEvtCode_fwUpgradeFail:
		// Firmware update failed - resume normal operation
		msg.msgCode = emtrMsgCode_resume;
		break;

	default:
		// Ignore other events
		return;
	}

	(void)xQueueSend(pCtrl->queue, &msg, 0);
}


/**
 * \brief Signal to poll the EMTR
 */
static void timerCallback(TimerHandle_t timer)
{
	emtrCtrl_t *	pCtrl = (emtrCtrl_t *)pvTimerGetTimerID(timer);
	emtrMsg_t		msg = {
		.msgCode = emtrtMsgCode_timer
	};

	(void)xQueueSend(pCtrl->queue, &msg, 0);
}


#if (ENA_PERIOD_REPORT)

static void printDeviceStatus(emtrCtrl_t * pCtrl)
{
	emtrDeviceStatus_t	dev = pCtrl->curDeviceStatus;
	char				timeStr[30];

	snprintf(
		timeStr,
		sizeof(timeStr),
		"%u:%02u:%02u",
		(dev.epoch / 3600),
		((dev.epoch % 3600) / 60),
		(dev.epoch % 60)
	);

	gc_dbg("");
	gc_dbg("EMTR status");
	gc_dbg("  Communication  : %s", dev.emtrCommUp ? "Up" : "Down");
	gc_dbg("  Resets         : %u", pCtrl->emtrResetCount);
	gc_dbg("  Epoch          : %u (%s)", dev.epoch, timeStr);
	gc_dbg("  Temperature (C): %u", dev.temperature);
	gc_dbg("");
}


static void printSocketStatus(emtrCtrl_t * pCtrl, int sockNum)
{
	emtrSocketStatus_t	sock;

	if (emtrDrvGetSocketStatus(sockNum, &sock) != ESP_OK) {
		gc_err("Failed to read status of socket %d", sockNum);
	}

	char	timeStr[30];
	snprintf(
		timeStr,
		sizeof(timeStr),
		"%u:%02u:%02u",
		(sock.relayTime / 3600),
		((sock.relayTime % 3600) / 60),
		(sock.relayTime % 60)
	);

	// Shorthand reference to instant energy
	emtrInstEnergy_t *	ie = &sock.instEnergy;

	gc_dbg("Socket %d status", sockNum);
	gc_dbg("  Plug        : %s", sock.isPlugged ? "true" : "false");
	gc_dbg("  On          : %s", sock.isOn ? "true" : "false");
	gc_dbg("  On/Off time : %s", timeStr);
	gc_dbg("  inst volts  : %u.%u", ie->dVolts/10, ie->dVolts%10);
	gc_dbg("  inst amps   : %u.%u", ie->mAmps/1000, ie->mAmps%1000);
	gc_dbg("  inst watts  : %u.%u", ie->dWatts/10, ie->dWatts%10);
	gc_dbg("  inst pf     : %d", ie->powerFactor);
	gc_dbg("  dWatt-hours : %u", sock.dWattHours);
	gc_dbg("  Total dWatts: %u", sock.dWattsTotal);
	gc_dbg("");
}


static void printDebug(emtrCtrl_t * pCtrl, uint32_t curTime)
{
	static	uint32_t	lastTime = 0;

	// Don't print more frequently than 10-second intervals
	if (curTime - lastTime >= 10) {
		lastTime = curTime;

		printDeviceStatus(pCtrl);

		int	i;
		for (i = 0; i < pCtrl->conf.numSockets; i++) {
			printSocketStatus(pCtrl, 1 + i);
		}
	}
}

#endif

