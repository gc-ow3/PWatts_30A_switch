/** \file cs_emtr_drv_main.c
 * Main body of the core EMTR driver
 *
 *  Created on: Apr 22, 2020
 *      Author: wesd
 */

#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include "cs_common.h"
#include "cs_emtr_drv.h"
#include "xmodem.h"
#include "fw_file_check.h"

static const char TAG[] = {"cs_emtr_drv"};

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

#define EMTR_POLL_CYCLES_PER_SEC		(10)
#define EMTR_POLL_PERIOD_MS				(1000 / EMTR_POLL_CYCLES_PER_SEC)

// EMTR message framing characters
#define MSG_CHAR_SOP					((uint8_t)0x1B)
#define MSG_CHAR_EOP					((uint8_t)0x0A)


////////////////////////////////////////////////////////////////////////////////
// Macros
////////////////////////////////////////////////////////////////////////////////

// Sleep for a number of milliseconds
#define	EMTR_SLEEP_MS(t)			vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	EMTR_TIME_SEC()				(esp_timer_get_time()/1000000)
#define	EMTR_TIME_MS()				(esp_timer_get_time()/1000)

// Acquire and release mutex
#define MUTEX_GET(ctrl)		xSemaphoreTakeRecursive(ctrl->mutex, portMAX_DELAY)
#define MUTEX_PUT(ctrl)		xSemaphoreGiveRecursive(ctrl->mutex)

// Test for feature
#define EMTR_HAS_FEATURE(ctrl, test) ((ctrl)->features & (test))
#define EMTR_HAS_PLUG_DETECT(ctrl)	(EMTR_HAS_FEATURE((ctrl), csEmtrFeature_plugDetect))
#define EMTR_HAS_LOAD_DETECT(ctrl)	(EMTR_HAS_FEATURE((ctrl), csEmtrFeature_loadDetect))
#define EMTR_HAS_HCCI(ctrl)			(EMTR_HAS_FEATURE((ctrl), csEmtrFeature_hcci))


////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

/**
 * \brief EMTR run modes
 */
typedef enum {
	emtrRunMode_notRunning = 0,
	emtrRunMode_application,
	emtrRunMode_bootLoader
} emtrRunMode_t;


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
 * \brief Boolean states
 */
typedef enum {
	boolState_init  = -1,
	boolState_false =  0,
	boolState_true  =  1
} boolState_t;

#define STATE_IS_TRUE(v)		(boolState_true == (v).curState)

/**
 * \brief Boolean state change tracking structure
 */
typedef struct {
	boolState_t		curState;
	boolState_t		newState;
} stateChange_t;


#define EMTR_PRE_SAMPLE_SZ	(4)

typedef struct {
	uint32_t	min;	// Minimum value read
	uint32_t	max;	// Maximum value read
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
	accum_t		dVolts;		// 0.1 volt units
	accum_t		mAmps;		// 0.001 Amp units
	accum_t		dWatts;		// 0.1 Watt units
	accum_t		pFactor;	// 0..100
} accEnergy_t;


/**
 * \brief Track the status for each socket
 */
typedef struct {
	csEmtrSockInfo_t *		info;
	uint32_t *				flags;
	csEmtrSockStatus_t *	status;
	csEmtrSockStatus_t		prvStatus;
	uint32_t				cosTimeRelay;
	uint32_t				cosTimeLoad;
	accEnergy_t *			eAccChan;
	stateChange_t			plugDetect;
	stateChange_t			relayActive;
	stateChange_t			loadActive;
	stateChange_t			overload;
	uint8_t					calData[128];
} sockCtrl_t;


/**
 * \brief Task control structure
 */
typedef struct {
	bool					isRunning;
	bool					isGood;
	bool					pause;
	csEmtrDrvConf_t			conf;
	int						numAccChan;
	emtrRunMode_t			emtrMode;
	uint32_t				emtrResetCount;
	char					blVersion[16];
	char					fwVersion[16];
	SemaphoreHandle_t		mutex;
	QueueHandle_t			queue;
	TimerHandle_t			timer;
	TaskHandle_t			taskHandle;
	csEmtrDeviceStatus_t	curDeviceStatus;
	csEmtrDeviceStatus_t	prevDeviceStatus;
	uint32_t *				sockFlags;
	csEmtrSockStatus_t *	sockStatus;
	sockCtrl_t *			sockCtrl;
	uint32_t				curTime;
	uint32_t				commDelayMs;
	bool					holdOffCommand;
	fwupgState_t			fwupgState;
	bool					spyEnable;
	unsigned int			pollSampleCt;
	csEmtrDrvCmdOpt_t		cmdOpt;
} emtrCtrl_t;


/**
 * \brief EMTR task message codes
 */
typedef enum {
	emtrtMsgCode_timer,
	emtrMsgCode_pause,
	emtrMsgCode_resume
} emtrMsgCode_t;


/**
 * \brief EMTR task message data options
 */
typedef union {
	union {
		struct {
			csEmtrSockNum_t	sockNum;
			uint32_t		id;
			uint16_t		samples;
			uint8_t			resolution;
		} sockState;
		struct {
			csEmtrSockNum_t	sockNum;
			uint8_t			value;
		} hiAmpLevel;
		struct {
			csEmtrSockNum_t	sockNum;
			uint16_t		hiVal;
			uint16_t		loVal;
		} loadDetect;
	};
} emtrMsgData_t;


/**
 * \brief structure of EMTR message sent via queue
 */
typedef struct {
	emtrMsgCode_t	msgCode;
	emtrMsgData_t	msgData;
} emtrMsg_t;


////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

static esp_err_t sockInit(emtrCtrl_t * sock);

static void sockTerm(emtrCtrl_t * pCtrl);

static sockCtrl_t * getSocketCtrl(emtrCtrl_t * pCtrl, csEmtrSockNum_t sockNum);

static esp_err_t uartInit(csEmtrUartConf_t * conf);

static esp_err_t uartWrite(uart_port_t uart, const uint8_t * buf, int len);

static esp_err_t uartRead(uart_port_t uart, uint8_t * buf, int * len);

static esp_err_t doCommand(
	emtrCtrl_t *	pCtrl,
	uint8_t			cmd,
	uint8_t *		payload,
	uint8_t *		retBuf,
	int *			retLen
);

static esp_err_t resetEmtrBoard(emtrCtrl_t * pCtrl, emtrRunMode_t runMode);

static esp_err_t emtrRunModeSet(
	emtrCtrl_t *	pCtrl,
	emtrRunMode_t	targetMode,
	uint8_t *		version
);

static esp_err_t checkRequest(emtrCtrl_t * pCtrl, csEmtrSockNum_t sockNum);

static void resetEmtrAccumulator(accum_t * eAvg, int value);

static void resetEmtrAccumulators(accEnergy_t * eAccum);


static void ctrlTask(void * params);

static esp_err_t emtrUpgrade(emtrCtrl_t * pCtrl, const uint8_t * fwFile);

static void timerCallback(TimerHandle_t timerHandle);

////////////////////////////////////////////////////////////////////////////////
// Variable data
////////////////////////////////////////////////////////////////////////////////
// Define the message queue
#define MSG_QUEUE_SZ		(20)

// Task control structure
static emtrCtrl_t *	emtrCtrl;


/*
********************************************************************************
********************************************************************************
* Public functions
********************************************************************************
********************************************************************************
*/


esp_err_t csEmtrDrvInit(const csEmtrDrvConf_t * conf)
{
	if (NULL == conf) {
		return ESP_ERR_INVALID_ARG;
	}

	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL != pCtrl) {
		// Already initialized
		return ESP_OK;
	}

	// Allocate the task control structure
	if ((pCtrl = calloc(1, sizeof(*pCtrl))) == NULL) {
		return ESP_ERR_NO_MEM;
	}

	// Copy the configuration to the control structure
	pCtrl->conf = *((csEmtrDrvConf_t *)conf);

	// Set starting command options
	pCtrl->cmdOpt.flags.noResp = 0;
	pCtrl->cmdOpt.flags.spy    = 0;
	pCtrl->cmdOpt.timeoutMs    = 5000;

	// Set the firmware versions for now until read from EMTR
	strcpy(pCtrl->blVersion, "0.0.0");
	strcpy(pCtrl->fwVersion, "0.0.0");

	esp_err_t	status;

	if (conf->numSockets > 0) {
		// Set the number of accumulator channels per socket
		// 1 (channel 0) for internal use for load tracking
		// Any additional for application use
		pCtrl->numAccChan = 1 + conf->numAccChan;

		// Allocate the array of socket flag words
		pCtrl->sockFlags = calloc(conf->numSockets, sizeof(*pCtrl->sockFlags));
		if (NULL == pCtrl->sockFlags) {
			status = ESP_ERR_NO_MEM;
			goto exitMem;
		}

		// Allocate the array of socket status structures
		pCtrl->sockStatus = calloc(conf->numSockets, sizeof(*pCtrl->sockStatus));
		if (NULL == pCtrl->sockFlags) {
			status = ESP_ERR_NO_MEM;
			goto exitMem;
		}

		// Allocate the array of socket control structures
		pCtrl->sockCtrl = calloc(conf->numSockets, sizeof(sockCtrl_t));
		if (NULL == pCtrl->sockCtrl) {
			status = ESP_ERR_NO_MEM;
			goto exitMem;
		}

		// Initialize socket control
		sockInit(pCtrl);
	}

	// Set up the command UART
	if ((status = uartInit(&pCtrl->conf.uartConf)) != ESP_OK) {
		ESP_LOGE(TAG, "uartInit(CMD) failed");
		goto exitMem;
	}

	if ((pCtrl->mutex = xSemaphoreCreateRecursiveMutex()) == NULL) {
		ESP_LOGE(TAG, "Mutex create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	pCtrl->queue = xQueueCreate(MSG_QUEUE_SZ, sizeof(emtrMsg_t));
	if (NULL == pCtrl->queue) {
		ESP_LOGE(TAG, "Queue create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	pCtrl->timer = xTimerCreate(
		"emtr_drv",
		pdMS_TO_TICKS(EMTR_POLL_PERIOD_MS),
		pdFALSE,
		(void *)pCtrl,
		timerCallback
	);
	if (NULL == pCtrl->timer) {
		ESP_LOGE(TAG, "Timer create failed");
		status = ESP_FAIL;
		goto exitMem;
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
	uint8_t	blVer[3];
	status = emtrRunModeSet(pCtrl, emtrRunMode_bootLoader, blVer);
	if (ESP_OK == status) {
		snprintf(
			pCtrl->blVersion, sizeof(pCtrl->blVersion),
			"%u.%u.%u",
			blVer[0], blVer[1], blVer[2]
		);
	} else {
		ESP_LOGE(TAG, "Failed to start EMTR boot loader");
		goto exitMem;
	}

	// Start the EMTR application and read its version
	uint8_t	fwVer[3];
	status = emtrRunModeSet(pCtrl, emtrRunMode_application, fwVer);
	if (ESP_OK == status) {
		snprintf(
			pCtrl->fwVersion, sizeof(pCtrl->fwVersion),
			"%u.%u.%u",
			fwVer[0], fwVer[1], fwVer[2]
		);
	} else {
		// Don't abort the initialization at this point, maybe the firmware
		// can be programmed via the boot loader
		ESP_LOGE(TAG, "Failed to start EMTR application");
	}

	// Check if there is a firmware update for the EMTR
	const uint8_t *	fwImg = pCtrl->conf.fwImage;

	if (csFwFileIsValid(fwImg, "emtr")) {
		const csFwHdr_t *	fwHdr = (const csFwHdr_t *)fwImg;

		if (strcmp(pCtrl->fwVersion, "0.0.0") == 0) {
			ESP_LOGI(TAG,
				"Update EMTR firmware to v%u.%u.%u",
				fwHdr->majorVer, fwHdr->minorVer, fwHdr->patchVer
			);

			if (emtrUpgrade(pCtrl, fwImg) == ESP_OK) {
				ESP_LOGI(TAG, "EMTR update completed");
			} else {
				ESP_LOGE(TAG, "EMTR update failed");
			}
		}
	}

	pCtrl->isGood = true;
	emtrCtrl = pCtrl;
	return ESP_OK;

exitMem:
	if (pCtrl->mutex) {
		vSemaphoreDelete(pCtrl->mutex);
	}
	if (pCtrl->queue) {
		vQueueDelete(pCtrl->queue);
	}
	if (pCtrl->timer) {
		xTimerDelete(pCtrl->timer, 10);
	}
	if (pCtrl->sockFlags) {
		free(pCtrl->sockFlags);
	}
	if (pCtrl->sockStatus) {
		free(pCtrl->sockStatus);
	}
	sockTerm(pCtrl);
	free(pCtrl);
	return status;
}


esp_err_t csEmtrDrvStart(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (pCtrl->isRunning)
		return ESP_OK;

	if (!pCtrl->isGood) {
		return ESP_FAIL;
	}

	BaseType_t	xStatus;

	// Start the control task
	xStatus = xTaskCreate(
		ctrlTask,
		"emtr_drv",
		3000,
		(void *)pCtrl,
		pCtrl->conf.taskPrio,
		&pCtrl->taskHandle
	);
	if (pdPASS == xStatus) {
		//ESP_LOGI(TAG, "EMTR ctrlTask created");
	} else {
		ESP_LOGE(TAG, "Task create failed");
		return ESP_FAIL;
	}

	// Start the poll timer
	xTimerStart(pCtrl->timer, pdMS_TO_TICKS(20));

	// The driver is running
	pCtrl->isRunning = true;

	return ESP_OK;
}


esp_err_t csEmtrDrvStop(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (!pCtrl->isRunning)
		return ESP_FAIL;

	xTimerDelete(pCtrl->timer, pdMS_TO_TICKS(20));
	vTaskDelete(pCtrl->taskHandle);

	pCtrl->isRunning = false;

	return ESP_OK;
}


/**
 * \brief Return running state of EMTR driver
 */
bool csEmtrDrvIsRunning(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return false;

	return pCtrl->isRunning && pCtrl->isGood;
}


/**
 * \brief Return EMTR boot loader version string
 */
const char * csEmtrDrvBlVersion(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return "N/A";

	return (const char *)pCtrl->blVersion;
}


/**
 * \brief Return EMTR firmware version string
 */
const char * csEmtrDrvFwVersion(void)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	if (NULL == pCtrl)
		return "N/A";

	return (const char *)pCtrl->fwVersion;
}


esp_err_t csEmtrDrvDeviceStatus(csEmtrDeviceStatus_t * ret)
{
	if (!ret) {
		return ESP_ERR_INVALID_ARG;
	}

	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	if ((status = checkRequest(pCtrl, 1)) != ESP_OK) {
		return status;
	}

	MUTEX_GET(pCtrl);
	*ret = pCtrl->curDeviceStatus;
	MUTEX_PUT(pCtrl);

	return ESP_OK;
}


esp_err_t csEmtrDrvSockStatus(csEmtrSockNum_t sockNum, csEmtrSockStatus_t * ret)
{
	if (!ret) {
		return ESP_ERR_INVALID_ARG;
	}

	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}

	uint32_t	curTime = EMTR_TIME_SEC();

	MUTEX_GET(pCtrl);
	*ret = *sock->status;
	ret->relayTime = curTime - sock->cosTimeRelay;
	ret->loadTime  = curTime - sock->cosTimeLoad;
	MUTEX_PUT(pCtrl);

	return ESP_OK;
}


static void cpyAccum(csEmtrAvgEnergy_t * dst, accum_t * src)
{
	dst->min = src->min;
	dst->max = src->max;
	if (src->sampleCt > 0) {
		dst->avg = (uint32_t)(src->sum / (uint64_t)src->sampleCt);
	} else {
		dst->avg = 0;
	}
}


esp_err_t csEmtrDrvReadAccumulator(
	csEmtrSockNum_t		sockNum,
	uint8_t				chan,
	csEmtrAccEnergy_t *	dst,
	bool				reset
)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	if (chan > pCtrl->numAccChan || NULL == dst) {
		return ESP_ERR_INVALID_ARG;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}
	accEnergy_t *	src = &sock->eAccChan[chan];

	MUTEX_GET(pCtrl);

	cpyAccum(&dst->dVolts, &src->dVolts);
	cpyAccum(&dst->mAmps, &src->mAmps);
	cpyAccum(&dst->dWatts, &src->dWatts);
	cpyAccum(&dst->pFactor, &src->pFactor);

	if (reset) {
		resetEmtrAccumulators(src);
	}

	MUTEX_PUT(pCtrl);

	return ESP_OK;
}


esp_err_t csEmtrDrvResetAccumulator(csEmtrSockNum_t sockNum, uint8_t chan)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	if (chan > pCtrl->numAccChan) {
		return ESP_ERR_INVALID_ARG;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}

	MUTEX_GET(pCtrl);
	resetEmtrAccumulators(&sock->eAccChan[chan]);
	MUTEX_PUT(pCtrl);

	return ESP_OK;
}


esp_err_t csEmtrDrvSetRelay(csEmtrSockNum_t sockNum, bool active)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}

	// Reference the command codes for the indexed socket
	uint8_t	cmdCode = active ? sock->info->cmd.turnOn : sock->info->cmd.turnOff;

	// Execute command, no payload, no response expected
	MUTEX_GET(pCtrl);
	status = doCommand(pCtrl, cmdCode, NULL, NULL, NULL);
	MUTEX_PUT(pCtrl);

	return status;
}


bool csEmtrDrvRelayIsActive(csEmtrSockNum_t sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return false;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return false;
	}

	return sock->status->relayActive;
}


bool csEmtrDrvLoadIsActive(csEmtrSockNum_t sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return false;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return false;
	}

	return sock->status->loadActive;
}


esp_err_t csEmtrDrvGetLoadDetect(csEmtrSockNum_t sockNum, uint16_t * hiVal, uint16_t * loVal)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	csEmtrDrvCbGetLoadDetect_t	cbFunc = pCtrl->conf.cbFunc.loadDetectGet;

	if (!cbFunc) {
		ESP_LOGE(TAG, "%s not implemented", __FUNCTION__);
		return ESP_FAIL;
	}

	csEmtrDrvCbData_t	cbData = {
		.drvHandle = (csEmtrDrvHandle_t)pCtrl,
		.curTime   = pCtrl->curTime,
		.privData  = pCtrl->conf.cbPrivate
	};

	MUTEX_GET(pCtrl);
	status = cbFunc(&cbData, sockNum, hiVal, loVal);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvSetLoadDetect(csEmtrSockNum_t sockNum, uint16_t hiVal, uint16_t loVal)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	csEmtrDrvCbSetLoadDetect_t	cbFunc = pCtrl->conf.cbFunc.loadDetectSet;

	if (!cbFunc) {
		ESP_LOGE(TAG, "%s not implemented", __FUNCTION__);
		return ESP_FAIL;
	}

	csEmtrDrvCbData_t	cbData = {
		.drvHandle = (csEmtrDrvHandle_t)pCtrl,
		.curTime   = pCtrl->curTime,
		.privData  = pCtrl->conf.cbPrivate
	};

	MUTEX_GET(pCtrl);
	status = cbFunc(&cbData, sockNum, hiVal, loVal);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvGetHcci(csEmtrSockNum_t sockNum, csEmtrHcci_t * ret)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	csEmtrDrvCbGetHcci_t	cbFunc = pCtrl->conf.cbFunc.hcciGet;

	if (!cbFunc) {
		ESP_LOGE(TAG, "%s not implemented", __FUNCTION__);
		return ESP_FAIL;
	}

	csEmtrDrvCbData_t	cbData = {
		.drvHandle = (csEmtrDrvHandle_t)pCtrl,
		.curTime   = pCtrl->curTime,
		.privData  = pCtrl->conf.cbPrivate
	};

	MUTEX_GET(pCtrl);
	status = cbFunc(&cbData, sockNum, ret);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvSetHcci(csEmtrSockNum_t sockNum, csEmtrHcci_t threshold)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	csEmtrDrvCbSetHcci_t	cbFunc = pCtrl->conf.cbFunc.hcciSet;

	if (!cbFunc) {
		ESP_LOGE(TAG, "%s not implemented", __FUNCTION__);
		return ESP_FAIL;
	}

	csEmtrDrvCbData_t	cbData = {
		.drvHandle = (csEmtrDrvHandle_t)pCtrl,
		.curTime   = pCtrl->curTime,
		.privData  = pCtrl->conf.cbPrivate
	};

	MUTEX_GET(pCtrl);
	status = cbFunc(&cbData, sockNum, threshold);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvGetCalData(csEmtrSockNum_t sockNum, uint8_t * buf, int * bufSz)
{
	if (!buf || !bufSz || *bufSz < 128) {
		return ESP_ERR_INVALID_ARG;
	}

	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}

	// Reference the command codes for the indexed socket
	uint8_t	cmdCode = sock->info->cmd.calGet;

	// Execute command, no payload, copy back data
	MUTEX_GET(pCtrl);
	status = doCommand(pCtrl, cmdCode, NULL, buf, bufSz);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvSaveCalData(csEmtrSockNum_t sockNum)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, sockNum)) != ESP_OK) {
		return status;
	}

	sockCtrl_t *	sock = getSocketCtrl(pCtrl, sockNum);
	if (!sock) {
		return ESP_FAIL;
	}

	// Reference the command codes for the indexed socket
	uint8_t	cmdCode = sock->info->cmd.calSet;

	// Execute command, no payload, read and discard returned data
	int	ioLen = sizeof(sock->calData);
	MUTEX_GET(pCtrl);
	status = doCommand(pCtrl, cmdCode, NULL, sock->calData, &ioLen);
	MUTEX_PUT(pCtrl);

	return status;
}


static esp_err_t _command(
	uint8_t				cmd,
	uint8_t *			payload,
	uint8_t *			retBuf,
	int *				retLen,
	csEmtrDrvCmdOpt_t *	opt
)
{
	emtrCtrl_t *	pCtrl = emtrCtrl;
	esp_err_t		status;

	// Validate parameters
	if ((status = checkRequest(pCtrl, 1)) != ESP_OK) {
		ESP_LOGE(TAG, "%s: error %d", __func__, status);
		return status;
	}

	csEmtrDrvCmdOpt_t	saveOpt;

	MUTEX_GET(pCtrl);
	saveOpt = pCtrl->cmdOpt;
	pCtrl->cmdOpt = *opt;
	status = doCommand(pCtrl, cmd, payload, retBuf, retLen);
	pCtrl->cmdOpt = saveOpt;
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t csEmtrDrvCommand(
	uint8_t		cmd,
	uint8_t *	payload,
	uint8_t *	retBuf,
	int *		retLen
)
{
	csEmtrDrvCmdOpt_t	opt = csEmtrDrvCmdOptDefault();

	return _command(cmd, payload, retBuf, retLen, &opt);
}


esp_err_t csEmtrDrvCommandOpt(
	uint8_t				cmd,
	uint8_t *			payload,
	uint8_t *			retBuf,
	int *				retLen,
	csEmtrDrvCmdOpt_t *	opt
)
{
	return _command(cmd, payload, retBuf, retLen, opt);
}


esp_err_t csEmtrDrvCmdFromCb(
	csEmtrDrvHandle_t	handle,
	uint8_t				cmd,
	uint8_t *			payload,
	uint8_t *			retBuf,
	int *				retLen
)
{
	emtrCtrl_t *	pCtrl = (emtrCtrl_t *)handle;

	return doCommand(pCtrl, cmd, payload, retBuf, retLen);
}


void csEmtrDrvEventFromCb(
	csEmtrDrvHandle_t	handle,
	csEmtrEvtCode_t		evtCode,
	csEmtrEvtData_t *	evtData
)
{
//	emtrCtrl_t *	pCtrl = (emtrCtrl_t *)handle;

}


const char * csEmtrDrvEventStr(csEmtrEvtCode_t evtCode)
{
	switch (evtCode)
	{
	case csEmtrEvtCode_commDown:
		return "commDown";
	case csEmtrEvtCode_commUp:
		return "commUp";
	case csEmtrEvtCode_reset:
		return "reset";
	case csEmtrEvtCode_temperature:
		return "temperature";
	case csEmtrEvtCode_epoch:
		return "epoch";
	case csEmtrEvtCode_overload:
		return "overload";
	case csEmtrEvtCode_volts:
		return "volts";
	case csEmtrEvtCode_amps:
		return "amps";
	case csEmtrEvtCode_watts:
		return "watts";
	case csEmtrEvtCode_pFactor:
		return "powerFactor";
	case csEmtrEvtCode_wattHours:
		return "wattHours";
	case csEmtrEvtCode_relayState:
		return "relayState";
	case csEmtrEvtCode_relayCycles:
		return "relayCycles";
	case csEmtrEvtCode_loadState:
		return "loadState";
	case csEmtrEvtCode_loadCycles:
		return "loadCycles";
	case csEmtrEvtCode_plugState:
		return "plugState";
	default:
		return "undefined";
	}
}


/*
********************************************************************************
********************************************************************************
* Local functions
********************************************************************************
********************************************************************************
*/


static esp_err_t sockInit(emtrCtrl_t * pCtrl)
{
	int						i;
	sockCtrl_t *			sock       = pCtrl->sockCtrl;
	csEmtrSockInfo_t *		info       = pCtrl->conf.sockInfo;
	uint32_t *				flags      = pCtrl->sockFlags;
	csEmtrSockStatus_t *	sockStatus = pCtrl->sockStatus;

	for (i = 0; i < pCtrl->conf.numSockets; i++, sock++) {
		// Attach the info structure, status structure, and flags word to the socket
		sock->info   = info++;
		sock->flags  = flags++;
		sock->status = sockStatus++;

		// Allocate space for the accumulator channel(s)
		accEnergy_t *	acc = calloc(pCtrl->numAccChan, sizeof(*acc));
		if (NULL == acc) {
			return ESP_ERR_NO_MEM;
		}
		sock->eAccChan = acc;

		int	ch;
		for (ch = 0; ch < pCtrl->numAccChan; ch++) {
			resetEmtrAccumulators(acc + ch);
		}

		// Initialize the "Plugged" state change trackers
		sock->plugDetect.curState = boolState_init;
		sock->plugDetect.newState = boolState_init;

		// Initialize the "On" state change trackers
		sock->relayActive.curState = boolState_init;
		sock->relayActive.newState = boolState_init;

		// Initialize the "Load" state change trackers
		sock->loadActive.curState = boolState_init;
		sock->loadActive.newState = boolState_init;

		// Initialize the "Overload" state change trackers
		sock->overload.curState = boolState_init;
		sock->overload.newState = boolState_init;
	}

	return ESP_OK;
}


static void sockTerm(emtrCtrl_t * pCtrl)
{
	if (NULL == pCtrl->sockCtrl) {
		return;
	}

	int				i;
	sockCtrl_t *	sock = pCtrl->sockCtrl;

	for (i = 0; i < pCtrl->conf.numSockets; i++, sock++) {
		// Deallocate resources used for the socket
		if (sock->eAccChan) {
			free(sock->eAccChan);
			sock->eAccChan = NULL;
		}
	}

	free(pCtrl->sockCtrl);
	pCtrl->sockCtrl = NULL;
}


static sockCtrl_t * getSocketCtrl(emtrCtrl_t * pCtrl, csEmtrSockNum_t sockNum)
{
	if (!pCtrl) {
		return NULL;
	}

	sockCtrl_t *	sock = pCtrl->sockCtrl;
	int				i;
	for (i = 0; i < pCtrl->conf.numSockets; i++, sock++) {
		if (sockNum == sock->info->sockNum) {
			return sock;
		}
	}

	return NULL;
}


static esp_err_t uartInit(csEmtrUartConf_t * uartConf)
{
	// Configure the port
	uart_config_t	uCfg = {
		.baud_rate = uartConf->baudRate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	esp_err_t	status;

	if ((status = uart_param_config(uartConf->port, &uCfg)) != ESP_OK)
		return status;

	status = uart_set_pin(
		uartConf->port,
		uartConf->gpioUartTx,
		uartConf->gpioUartRx,
		UART_PIN_NO_CHANGE,	// RTS not used,
		UART_PIN_NO_CHANGE	// CTS not used
	);
	if (ESP_OK != status)
		return status;

	return uart_driver_install(uartConf->port, 128 * 2, 0, 0, NULL, 0);
}


static esp_err_t uartWrite(uart_port_t uart, const uint8_t * buf, int len)
{
	int		wrCount = 0;
	int		wrLen;

	do {
		wrLen = uart_write_bytes(uart, (const char *)buf, len);
		if (wrLen < 0)
			return ESP_FAIL;

		if ((wrCount += wrLen) < len) {
			// TX FIFO is full, delay a while so it can drain
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	} while (wrCount < len);

	return ESP_OK;
}


static esp_err_t uartRead(uart_port_t uart, uint8_t * buf, int * len)
{
	int		rdLen;

	rdLen = uart_read_bytes(uart, buf, *len, pdMS_TO_TICKS(100));
	if (rdLen < 0)
		return ESP_FAIL;

	*len = rdLen;
	return ESP_OK;
}


/**
 * \brief Perform hardware reset of the EMTR
 */
static void resetEmtr(emtrCtrl_t * pCtrl)
{
	pCtrl->emtrResetCount += 1;

	// Reset board for normal operation
	(void)resetEmtrBoard(pCtrl, pCtrl->emtrMode);
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
		printf("Write %d bytes to emtr\r\n", msgLen);
		printf(
			"%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
			msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7]
		);
	}

	// Send the message
	if (uartWrite(pCtrl->conf.uartConf.port, msg, msgLen) != ESP_OK) {
		ESP_LOGE(TAG, "uart_drv_write failed");
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
	int			rdCount = 0;
	int			ioLen;
	uint64_t	timeout = EMTR_TIME_MS() + pCtrl->cmdOpt.timeoutMs;

	while (rdCount < rdLen)
	{
		if (EMTR_TIME_MS() >= timeout) {
			ESP_LOGE(TAG, "readEmtr timed out (%llu ms)", pCtrl->cmdOpt.timeoutMs);
			status = ESP_ERR_TIMEOUT;
			goto exitError;
		}

		ioLen = rdLen - rdCount;
		status = uartRead(pCtrl->conf.uartConf.port, buf + rdCount, &ioLen);
		if (ESP_OK != status) {
			ESP_LOGE(TAG, "uartRead failed");
			goto exitError;
		}

		if (ioLen > 0) {
			rdCount += ioLen;
		} else {
			// Wait 10 ms and read again
			EMTR_SLEEP_MS(10);
		}
	}

	if (pCtrl->cmdOpt.flags.spy) {
		ESP_LOGD(TAG, "Read %d bytes from emtr", rdCount);
		ESP_LOG_BUFFER_HEXDUMP(TAG, buf, rdCount, ESP_LOG_DEBUG);
	}

	return rdCount;

exitError:
	if (rdCount > 0) {
		ESP_LOGE(TAG, "readEmtr failed after reading %d bytes", rdCount);
		ESP_LOG_BUFFER_HEXDUMP(TAG, buf, rdCount, ESP_LOG_DEBUG);
	} else {
		ESP_LOGE(TAG, "readEmtr received no bytes");
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

	while (rdCount < msgLen) {
		rdLen = (msgLen - rdCount) > sizeof(raw) ? sizeof(raw) : msgLen - rdCount;
		status = readEmtr(pCtrl, raw, rdLen);
		if (status <= 0)
			break;
		rdCount += status;
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
		ESP_LOGE(TAG, "Expected SOP, got %02X", respHead[0]);
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
			ESP_LOGE(TAG, "Expected CMD %02X, got %02X", cmd, respHead[1]);
			status = ESP_FAIL;
			goto exitFlush;
		}
	} else {
		// No ret buffer provided, force zero-length read
		retSz = 0;

		// Not expecting return data, look for generic response
		if (0xF0 != respHead[1]) {
			ESP_LOGE(TAG, "Expected CMD == F0, got %02X", respHead[1]);
			status = ESP_FAIL;
			goto exitFlush;
		}
	}

	// Start checksum with CMD and Payload Length
	cksum ^= respHead[1];
	cksum ^= respHead[2];

	if (payloadLen > 0) {
		if (payloadLen > retSz) {
			ESP_LOGE(TAG, "Message length (%d) exceeds space (%d)", payloadLen, retSz);
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
		ESP_LOGE(TAG, "Checksum failed, expected %02X, got %02X", respTail[0], cksum);
		status = ESP_FAIL;
		goto exitError;
	}

	return payloadLen;

exitFlush:
	flushEmtrMsg(pCtrl, payloadLen + 2);
exitError:
	return status;
}


static esp_err_t doCommand(
	emtrCtrl_t *	pCtrl,
	uint8_t			cmd,
	uint8_t *		payload,
	uint8_t *		retBuf,
	int *			retLen
)
{
	if (!pCtrl) {
		return ESP_ERR_INVALID_ARG;
	}

	// Shorthand reference to UART configuration
	csEmtrUartConf_t *	uartConf = &pCtrl->conf.uartConf;

	int		status;
	int		recvLen;
	int		bufSz;
	int		maxRetry = 3;
	int		retryCt  = 0;

	if (retLen) {
		bufSz   = *retLen;
		*retLen = 0;
	} else {
		bufSz = 0;
	}

	for (retryCt = 0; retryCt < maxRetry; retryCt++) {
		if (retryCt > 0) {
			ESP_LOGD(TAG, "%s(%02X): retry #%d", __FUNCTION__, cmd, retryCt);

			if (retryCt == (maxRetry - 1)) {
				// Last retry - try a hard reset this time
				resetEmtr(pCtrl);
			}

			// Delay before retry
			EMTR_SLEEP_MS(100);

			// Flush UART receive FIFO
			uart_flush_input(uartConf->port);
		}

		// Send the command
		status = sendEmtrCommand(pCtrl, cmd, payload);
		if (ESP_OK != status) {
			ESP_LOGE(TAG, "%s(%02X): sendEmtrCommand returned %d", __FUNCTION__, cmd, status);
			continue;
		}

		if (pCtrl->cmdOpt.flags.noResp) {
			recvLen = 0;
		} else {
			// Read the response
			recvLen = readEmtrResponse(pCtrl, cmd, retBuf, bufSz);
			if (0 > recvLen) {
				ESP_LOGE(TAG, "%s(%02X): readEmtrResponse returned %d", __FUNCTION__, cmd, recvLen);
				continue;
			}

			if (!pCtrl->curDeviceStatus.commUp) {
				// EMTR communication was restored
				pCtrl->curDeviceStatus.commUp = true;
				//notify(pCtrl, csEmtrEvtCode_commUp, NULL);
			}

			if (NULL == retBuf || 0 >= bufSz) {
				// Not expecting payload data
				recvLen = 0;
			}
		}

		if (retLen) {
			*retLen = recvLen;
		}

		return ESP_OK;
	}

	// If this point is reached retries have been exhausted
	ESP_LOGE(TAG, "%s(%02X): exhausted retries", __FUNCTION__, cmd);

	if (pCtrl->curDeviceStatus.commUp) {
		// EMTR communication was lost
		pCtrl->curDeviceStatus.commUp = false;
		//notify(pCtrl, csEmtrEvtCode_commDown, NULL);
	}

	return 	ESP_FAIL;
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
	csEmtrUartConf_t *	uartConf = &pCtrl->conf.uartConf;

	// Shorthand reference to EMTR mode select pin
	int8_t		gpioNum  = uartConf->gpioUartTx;

	// Drive active (low) the EMTR reset line
	gpio_set_level(pCtrl->conf.gpioEmtrRst, 0);

	// Drive the pin to select either boot loader mode or regular mode
	gpio_set_level(gpioNum, pinLevel);
	gpio_set_direction(gpioNum, GPIO_MODE_OUTPUT);
	// Value 0x100 cancels the assignment of UART TX to this GPIO pin
	gpio_matrix_out(gpioNum, 0x100, 0, 0);
	EMTR_SLEEP_MS(10);

	// Release the EMTR reset line
	gpio_set_level(pCtrl->conf.gpioEmtrRst, 1);
	EMTR_SLEEP_MS(10);

	// Restore pin to UART TX function
	uart_set_pin(
		uartConf->port,
		uartConf->gpioUartTx,
		UART_PIN_NO_CHANGE,
		UART_PIN_NO_CHANGE,
		UART_PIN_NO_CHANGE
	);

	// Wait for EMTR to be ready
	EMTR_SLEEP_MS(pCtrl->conf.resetDelayMs);
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
	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	// mode is required, version is optional
	if (!mode) {
		return ESP_ERR_INVALID_ARG;
	}

	uint8_t		cmd = pCtrl->conf.cmd.stateGet;
	uint8_t		resp[4];		// Expect 4 bytes returned
	int			ioLen;
	esp_err_t	status;

	// Execute the command
	ioLen = sizeof(resp);
	status = doCommand(pCtrl, cmd, NULL, resp, &ioLen);
	if (ESP_OK != status) {
		return status;
	}
	if (ioLen != sizeof(resp)) {
		ESP_LOGE(TAG, "EMTR returned %d bytes, expecting %d", ioLen, sizeof(resp));
		return ESP_ERR_INVALID_RESPONSE;
	}

	// Unpack response
	// Offset  Length  Content
	//      0       1  Run mode: 'B' == boot loader, appTag == application,
	//      1       1  Major version
	//      2       1  Minor version
	//      3       1  Patch version

	// Return the mode
	if (resp[0] == pCtrl->conf.appTag) {
		*mode = emtrRunMode_application;
	} else if (resp[0] == 'B') {
		*mode = emtrRunMode_bootLoader;
	} else {
		ESP_LOGE(TAG, "EMTR mode returned %c", resp[0]);
		return ESP_ERR_INVALID_RESPONSE;
	}

	// Optionally copy back the version
	if (NULL != version) {
		version[0] = resp[1];
		version[1] = resp[2];
		version[2] = resp[3];
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
	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	// Shorthand reference to UART configuration
	csEmtrUartConf_t *	uartConf = &pCtrl->conf.uartConf;

	// Flush the transmit FIFO
	uart_wait_tx_done(uartConf->port, pdMS_TO_TICKS(200));

	esp_err_t	status;

	// Reset the board into the desired state
	if ((status = resetEmtrBoard(pCtrl, targetMode)) != ESP_OK) {
		ESP_LOGE(TAG, "resetEmtrBoard error %d", status);
		return status;
	}

	// Flush the receive FIFO
	uart_flush_input(uartConf->port);

	// Query the EMTR to determine its actual mode
	if ((status = emtrRunModeGet(pCtrl, &pCtrl->emtrMode, version)) != ESP_OK) {
		ESP_LOGE(TAG, "emtrRunModeGet error %d", status);
		return status;
	}

	// Verify EMTR is in the target mode
	if (pCtrl->emtrMode != targetMode) {
		ESP_LOGE(TAG, "EMTR mode (%d) not expected value (%d)", pCtrl->emtrMode, targetMode);
		return ESP_ERR_INVALID_RESPONSE;
	}

	pCtrl->pollSampleCt = 0;
	return ESP_OK;
}


static void initEmtrAccumulator(accum_t *eAvg)
{
	// Reset min/max accumulators
	eAvg->min = 0x7fffffff;
	eAvg->max = 0;

	// Initialize the average value accumulator
	eAvg->sampleCt = 0;
	eAvg->sum      = 0;

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
	}

	// Add the newest sample to the ring buffer
	eAvg->preSample[eAvg->preSamplePut] = newSample;
	if (++eAvg->preSamplePut == EMTR_PRE_SAMPLE_SZ) {
		eAvg->preSamplePut = 0;
	}
	eAvg->preSampleCt += 1;
}


static void updateEmtrAccumulators(accEnergy_t * eAcc, csEmtrInstEnergy_t * inst)
{
	uint64_t	curTimeMs = EMTR_TIME_MS();

	if (eAcc->initial) {
		// First pass after accumulators were reset
		eAcc->initial   = false;
		// Ignore first 2.000 seconds of sample data
		eAcc->holdOffMs = 2000 + curTimeMs;

		initEmtrAccumulator(&eAcc->dVolts);
		initEmtrAccumulator(&eAcc->mAmps);
		initEmtrAccumulator(&eAcc->dWatts);
		initEmtrAccumulator(&eAcc->pFactor);
	} else if (curTimeMs >= eAcc->holdOffMs) {
		updateEmtrAccumulator(&eAcc->dVolts, inst->dVolts);
		updateEmtrAccumulator(&eAcc->mAmps, inst->mAmps);
		updateEmtrAccumulator(&eAcc->dWatts, inst->dWatts);
		updateEmtrAccumulator(&eAcc->pFactor, inst->pFactor);
	}
}


static void resetEmtrAccumulator(accum_t * eAvg, int32_t value)
{
	eAvg->min = value;
	eAvg->max = value;
}


static void resetEmtrAccumulators(accEnergy_t * eAcc)
{
	resetEmtrAccumulator(&eAcc->dVolts, 0);
	resetEmtrAccumulator(&eAcc->mAmps, 0);
	resetEmtrAccumulator(&eAcc->dWatts, 0);
	resetEmtrAccumulator(&eAcc->pFactor, 100);

	// Reset average computation
	eAcc->initial = true;
}


/**
 * \brief Common validation of parameters and optional mutex lock
 */
static esp_err_t checkRequest(emtrCtrl_t * pCtrl, csEmtrSockNum_t sockNum)
{
	if (NULL == pCtrl) {
		ESP_LOGE(TAG, "Driver not initialized");
		return ESP_ERR_INVALID_STATE;
	}

	if (!pCtrl->isRunning) {
		ESP_LOGE(TAG, "Driver not running");
		return ESP_ERR_INVALID_STATE;
	}

	if (!pCtrl->isGood) {
		ESP_LOGE(TAG, "Driver in fault");
		return ESP_ERR_INVALID_STATE;
	}

	/*
	if (emtrRunMode_application != pCtrl->emtrMode) {
		ESP_LOGE(TAG, "EMTR application not running");
		return ESP_ERR_INVALID_STATE;
	}
	*/

	if (sockNum < 1 || (pCtrl->conf.numSockets > 0 && sockNum > pCtrl->conf.numSockets)) {
		ESP_LOGE(TAG, "sockNum (%d) out of range", sockNum);
		return ESP_ERR_INVALID_ARG;
	}

	return ESP_OK;
}


static void handleSockets(emtrCtrl_t * pCtrl, csEmtrDrvCbData_t * cbData)
{
	csEmtrDrvConf_t *	conf = &pCtrl->conf;

	csEmtrDrvCbSockStatus_t	cbFunc = conf->cbFunc.sockStatus;
	if (!cbFunc) {
		//ESP_LOGE(TAG, "Socket status callback not implemented");
		return;
	}

	esp_err_t		status;

	// Read information for all sockets
	status = cbFunc(cbData, pCtrl->sockStatus, conf->numSockets);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Socket status read error %d", status);
		return;
	}

	int					i;
	csEmtrSockNum_t		sockNum = 1;
	sockCtrl_t *		sCtrl   = pCtrl->sockCtrl;

	for (i = 0; i < conf->numSockets; i++, sockNum++, sCtrl++) {
		csEmtrSockStatus_t *	curStat = sCtrl->status;

		// If not an active load, force current power factor reading
		// to 100. This should eventually be handled internally by the EMTR
		if (!curStat->loadActive) {
			curStat->instEnergy.pFactor = 100;
		}

		csEmtrInstEnergy_t *	eInst = &curStat->instEnergy;

		// Update energy accumulator(s)
		int	chNum;
		accEnergy_t *	eAcc = sCtrl->eAccChan;

		for (chNum = 0; chNum < pCtrl->numAccChan; chNum++, eAcc++) {
			// Update load accumulator (channel 0) only when load is active
			if (0 == chNum && !curStat->loadActive) {
				continue;
			}

			updateEmtrAccumulators(eAcc, eInst);
		}
	}
}


#define CHECK_FLAG(bm, flg)	(((flg) & (bm)) ? boolState_true : boolState_false)

static void handleDevice(emtrCtrl_t * pCtrl, csEmtrDrvCbData_t * cbData)
{
	csEmtrDrvConf_t *	conf = &pCtrl->conf;

	csEmtrDrvCbDevStatus_t	cbFunc = conf->cbFunc.devStatus;
	if (!cbFunc) {
		ESP_LOGE(TAG, "Device status callback not implemented");
		return;
	}

	esp_err_t	status;
	uint32_t	devFlags = 0;

	status = cbFunc(
		cbData,
		&devFlags,
		pCtrl->sockFlags,
		&pCtrl->curDeviceStatus
	);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Device status read error %d", status);
		return;
	}
}

static esp_err_t handleTimer(emtrCtrl_t * pCtrl)
{
	// Prepare the callback info structure
	csEmtrDrvCbData_t	cbData = {
		.drvHandle = (csEmtrDrvHandle_t)pCtrl,
		.curTime   = pCtrl->curTime,
		.privData  = pCtrl->conf.cbPrivate
	};

	if (emtrRunMode_bootLoader == pCtrl->emtrMode) {
		emtrRunMode_t	runMode;

		emtrRunModeGet(pCtrl, &runMode, NULL);
	} else if (emtrRunMode_application == pCtrl->emtrMode) {
		// Read device status on each pass
		// To keep the EMTR watch-dog timer happy
		handleDevice(pCtrl, &cbData);

		// Read socket-level information one per second
		if (++pCtrl->pollSampleCt < EMTR_POLL_CYCLES_PER_SEC)
			return ESP_OK;
		pCtrl->pollSampleCt = 0;

		// While EMTR is in pause state don't do anything to generate data updates
		if (pCtrl->pause)
			return ESP_OK;

		handleSockets(pCtrl, &cbData);
	} else {

	}

	return ESP_OK;
}


/**
 * \brief The control task
 */
static void ctrlTask(void * params)
{
	emtrCtrl_t *	pCtrl = (emtrCtrl_t *)params;
	emtrMsg_t		msg;

	while (1)
	{
		xQueueReceive(pCtrl->queue, &msg, portMAX_DELAY);

		pCtrl->curTime = EMTR_TIME_SEC();

		// Lock the mutex
		MUTEX_GET(pCtrl);

		switch (msg.msgCode)
		{
		case emtrtMsgCode_timer:
			(void)handleTimer(pCtrl);

			// Schedule the next read
			xTimerReset(pCtrl->timer, pdMS_TO_TICKS(10));
			break;

		case emtrMsgCode_pause:
			if (!pCtrl->pause) {
				pCtrl->pause = true;
			}
			break;

		case emtrMsgCode_resume:
			if (pCtrl->pause) {
				pCtrl->pause = false;
				pCtrl->pollSampleCt = 0;
			}
			break;

		default:
			// Ignore other messages
			break;
		}

		MUTEX_PUT(pCtrl);
	}
}


static esp_err_t startXmodemTransfer(emtrCtrl_t * pCtrl)
{
	uint8_t		cmd = pCtrl->conf.cmd.xmodemStart;

	// Execute the command - expect no return
	if (doCommand(pCtrl, cmd, NULL, NULL, NULL) != ESP_OK)
		return ESP_FAIL;

	return ESP_OK;
}

static esp_err_t emtrUpgrade(emtrCtrl_t * pCtrl, const uint8_t * fwFile)
{
	esp_err_t	status;

	// Set up for firmware upgrade operation
	ESP_LOGI(TAG, "Start EMTR boot loader");

	MUTEX_GET(pCtrl);

	// Place EMTR into boot loader mode
	xTimerStop(pCtrl->timer, pdMS_TO_TICKS(20));
	status = emtrRunModeSet(pCtrl, emtrRunMode_bootLoader, NULL);

	MUTEX_PUT(pCtrl);

	if (ESP_OK != status) {
		ESP_LOGE(TAG, "emtrRunModeSet error %d", status);
		goto exitUpdate;
	}

	ESP_LOGI(TAG, "Start XModem transfer");

	// Signal the boot loader to start the XMODEM transfer
	if ((status = startXmodemTransfer(pCtrl)) != ESP_OK) {
		ESP_LOGE(TAG, "startXmodemTransfer error failed");
		goto exitBootloader;
	}

	// Configure XMODEM to transfer 128 bytes at a time
	csXmodemCfg_t	xmCfg;

	memset(&xmCfg, 0, sizeof(xmCfg));
	xmCfg.blockSize = 128;

	ESP_LOGI(TAG, "Waiting for XModem handshake");

	// Shorthand reference to UART configuration
	csEmtrUartConf_t *	uartConf = &pCtrl->conf.uartConf;

	csXmHandle_t	xmHandle;

	if ((status = csXmSendOpen(uartConf->port, &xmHandle, &xmCfg)) != ESP_OK) {
		ESP_LOGE(TAG, "csXmSendOpen error %d", status);
		goto exitBootloader;
	}

	ESP_LOGI(TAG, "XModem transfer in progress");

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
			ESP_LOGE(TAG, "csXmSendWrite error %d, bytes sent: %u", status, writeCt);
			abortFlag = true;
			goto exitXm;
		}
	}

	ESP_LOGI(TAG, "%u bytes written to EMTR", writeCt);

exitXm:
	ESP_LOGI(TAG, "Close Xmodem session");
	if ((status = csXmSendClose(xmHandle, abortFlag)) != ESP_OK) {
		ESP_LOGE(TAG, "csXmSendClose error %d", status);
	}

exitBootloader:
	ESP_LOGI(TAG, "Exit boot loader mode");

	uint8_t	version[3];
	status = emtrRunModeSet(pCtrl, emtrRunMode_application, version);
	if (ESP_OK == status) {
		// Update the EMTR version
		snprintf(
			pCtrl->fwVersion, sizeof(pCtrl->fwVersion),
			"%u.%u.%u",
			version[0], version[1], version[2]
		);
		ESP_LOGI(TAG, "EMTR updated to %s", pCtrl->fwVersion);
	}

exitUpdate:
	xTimerReset(pCtrl->timer, pdMS_TO_TICKS(20));
	return status;
}


static void timerCallback(TimerHandle_t timer)
{
	emtrCtrl_t *	pCtrl = (emtrCtrl_t *)pvTimerGetTimerID(timer);
	emtrMsg_t		msg = {
		.msgCode = emtrtMsgCode_timer
	};

	(void)xQueueSend(pCtrl->queue, &msg, 0);
}
