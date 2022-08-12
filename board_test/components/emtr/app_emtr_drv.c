/*
 * emtr_drv.c
 *
 *  Created on: Jan 18, 2019
 *      Author: wesd
 */

#include "sdkconfig.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>

#include "pinout.h"
#include "cs_common.h"
#include "cs_packer.h"
#include "app_emtr_drv.h"

static const char TAG[] = {"app_emtr"};

////////////////////////////////////////////////////////////////////////////////
// Convenience macros
////////////////////////////////////////////////////////////////////////////////

// Acquire and release mutex
#define MUTEX_GET(ctrl)		xSemaphoreTake((ctrl)->mutex, portMAX_DELAY)
#define MUTEX_PUT(ctrl)		xSemaphoreGive((ctrl)->mutex)

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

// EMTR message framing characters
#define MSG_CHAR_SOP		((uint8_t)0x1B)
#define MSG_CHAR_EOP		((uint8_t)0x0A)

// General command codes
#define CMD_GET_FW_ID		(0x00)
#define CMD_GET_STATUS		(0x01)

// Reading data
#define CMD_GET_TOTALS		(0x02)
#define CMD_GET_INST_VALUES	(0x03)

// Set relay state
#define CMD_SET_RELAY		(0x05)

// Test commands
//#define CMD_SELF_TEST_START	(0x11)
//#define CMD_SET_TEST_MODE	(0x12)

// Calibration
//#define CMD_CAL_GET				(0x0D)
//#define CMD_CAL_SET				(0x0E)
//#define CMD_SET_U_GAIN			(0x1A)
//#define CMD_SET_I_GAIN			(0x1B)
//#define CMD_READ_I_LEAK			(0x40)
//#define CMD_SET_0P1_MA_LEAK		(0x41)
//#define CMD_SET_1P0_MA_LEAK		(0x42)


////////////////////////////////////////////////////////////////////////////////
// Internal data types
////////////////////////////////////////////////////////////////////////////////

/**
 * \brief Task control structure
 */
typedef struct {
	bool				isRunning;
	uint32_t			curTime;
	uint64_t			curTimeMs;
	csEmtrDrvConf_t		emtrConf;
	SemaphoreHandle_t	mutex;
	appEmtrStatus_t		curDeviceStatus;
	appEmtrStatus_t		preDeviceStatus;
	appEmtrInstant_t	preInstEnergy;
	appEmtrInstant_t	curInstEnergy;
	bool				emtrPaused;
} drvCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// Local functions
////////////////////////////////////////////////////////////////////////////////

static esp_err_t enterApi(drvCtrl_t ** pCtrl);

static esp_err_t emtrCbDeviceStatus(
	csEmtrDrvCbData_t *		cbData,			//!< Pointer to callback data structure
	uint32_t *				devFlags,		//!< Device-level flags
	uint32_t *				sockFlags,		//!< Array of flag words, one for each socket
	csEmtrDeviceStatus_t *	devStatus
);

static esp_err_t readStatus(drvCtrl_t * pCtrl, appEmtrStatus_t * ret);

static esp_err_t readTotals(drvCtrl_t * pCtrl, appEmtrTotals_t * ret);

static esp_err_t readInstValues(drvCtrl_t * pCtrl, appEmtrInstant_t * ret);

static esp_err_t setRelay(drvCtrl_t * pCtrl, bool on);

static void emtrTask(void* arg);

////////////////////////////////////////////////////////////////////////////////
// Constant data
////////////////////////////////////////////////////////////////////////////////

extern const unsigned char	emtrFwBin[];
//extern const unsigned int	emtrFwBinLen;

/**
 * \brief define EMTR driver use for this product
 *
 */
static const csEmtrDrvConf_t	emtrDrvConfInit = {
	.appTag             = 'P',	// 'P'owder Watts
	.numSockets         = 0,
	.sockInfo			= NULL,
	.numAccChan			= 0,	// In addition to the one built into the driver
	.features = {
		.fwUpdate    = 1,	// Enable check for firmware update
		.plugDetect  = 0,
		.loadDetect  = 0,
		.hcci        = 0,
		.calData     = 0,
	},
	.resetDelayMs    = 1000,
	.uartConf			= {
		.port         = UART_NUM_1,
		.gpioUartTx   = UART1_TX_GPIO,
		.gpioUartRx   = UART1_RX_GPIO,
		.baudRate     = 921600UL
	},
	.gpioEmtrRst		= EMTR_RST_GPIO,
	.taskPrio			= (tskIDLE_PRIORITY + 7),
	.fwImage            = emtrFwBin,
	.cbPrivate			= NULL,		// Must be set at run-time
	.cmd = {
		.stateGet		= 0x00,
		.statusGet		= 0x01,
		.xmodemStart	= 0x32,
	},
	.devFlag = {
		.factoryReset   = 0
	},
	.sockFlag = {
		.relayActive = 0,
		.loadActive  = 0,
		.overload    = 0,
	},
	.cbFunc = {
		.devStatus		= emtrCbDeviceStatus,
		.sockStatus		= NULL,
		.loadDetectGet  = NULL,
		.loadDetectSet  = NULL,
		.hcciGet        = NULL,
		.hcciSet        = NULL,
	},
};


////////////////////////////////////////////////////////////////////////////////
// Variable data
////////////////////////////////////////////////////////////////////////////////
// Define the message queue
#define MSG_QUEUE_SZ		(20)

// Task control structure
static drvCtrl_t *	drvCtrl;

/**
 * \brief Initialize, but not start the EMTR driver
 *
 * Allocate resources used by this driver
 *
 * Must call this before starting any tasks that will register to
 * receive event notifications from this driver
 *
 */
esp_err_t appEmtrDrvInit(void)
{
	drvCtrl_t *	pCtrl = drvCtrl;
	if (pCtrl) {
		// Already initialized
		return ESP_OK;
	}

	if ((pCtrl = calloc(1, sizeof(*pCtrl))) == NULL) {
		ESP_LOGE(TAG, "Failed to allocate memory");
		return ESP_ERR_NO_MEM;
	}

	esp_err_t	status;

	if ((pCtrl->mutex = xSemaphoreCreateRecursiveMutex()) == NULL) {
		ESP_LOGE(TAG, "Mutex create failed");
		status = ESP_FAIL;
		goto exitMem;
	}

	// Set up the EMTR driver configuration
	pCtrl->emtrConf = emtrDrvConfInit;

	// Attach the control structure
	pCtrl->emtrConf.cbPrivate = (void *)pCtrl;

	status = csEmtrDrvInit(&pCtrl->emtrConf);
	if (ESP_OK == status) {
		ESP_LOGI(TAG, "EMTR driver initialized");
		ESP_LOGI(TAG, "  BL version   : %s", csEmtrDrvBlVersion());
		ESP_LOGI(TAG, "  FW version   : %s", csEmtrDrvFwVersion());
	} else {
		ESP_LOGE(TAG, "EMTR driver failed to initialize");
		goto exitMem;
	}

	drvCtrl = pCtrl;
	return ESP_OK;

exitMem:
	// Release allocate resources
	free(pCtrl);

	return status;
}


/**
 * \brief Start the driver
 *
 * Must call \ref emtrDrvInit before calling this
 *
 */
esp_err_t appEmtrDrvStart()
{
	drvCtrl_t *	pCtrl = drvCtrl;
	if (!pCtrl) {
		// Not initialized
		return ESP_ERR_INVALID_STATE;
	}

	if (pCtrl->isRunning) {
		// Already started
		return ESP_OK;
	}

	esp_err_t	status;

	// If an EMTR firmware update is required, this function may take
	// a couple of minutes to return
	status = csEmtrDrvStart();
	if (ESP_OK == status) {
		ESP_LOGI(TAG, "EMTR driver started");
	} else {
		ESP_LOGI(TAG, "EMTR driver failed to start, error = %d", status);
		return status;
	}

	BaseType_t	xStatus;
	xStatus = xTaskCreate(
		emtrTask,
		"app_emtr",
		4000,
		(void*)pCtrl,
		5,
		NULL
	);
	if (pdPASS != xStatus) {
		ESP_LOGE(TAG, "Task create failed");
		return ESP_FAIL;
	}

	// The driver is running
	pCtrl->isRunning = true;
	return ESP_OK;
}


/**
 * \brief Read state
  *
  * \param [out] ret Pointer to structure to receive the data
  *
 */
esp_err_t appEmtrDrvGetStatus(appEmtrStatus_t * ret)
{
	if (NULL == ret) {
		return ESP_ERR_INVALID_ARG;
	}

	drvCtrl_t *	pCtrl;
	esp_err_t	status;

	if ((status = enterApi(&pCtrl)) != ESP_OK) {
		return status;
	}

	MUTEX_GET(pCtrl);
	status = readStatus(pCtrl, ret);
	MUTEX_PUT(pCtrl);

	return status;
}


/**
 * \brief Read instant data
  *
  * \param [out] ret Pointer to structure to receive the data
  *
 */
esp_err_t appEmtrDrvGetInstant(appEmtrInstant_t * ret)
{
	if (NULL == ret) {
		return ESP_ERR_INVALID_ARG;
	}

	drvCtrl_t *	pCtrl;
	esp_err_t	status;

	if ((status = enterApi(&pCtrl)) != ESP_OK) {
		return status;
	}

	MUTEX_GET(pCtrl);
	status = readInstValues(pCtrl, ret);
	MUTEX_PUT(pCtrl);

	return status;
}


/**
 * \brief Read cumulative data
  *
  * \param [out] ret Pointer to structure to receive the data
  *
 */
esp_err_t appEmtrDrvGetTotals(appEmtrTotals_t * ret)
{
	if (NULL == ret) {
		return ESP_ERR_INVALID_ARG;
	}

	drvCtrl_t *	pCtrl;
	esp_err_t	status;

	if ((status = enterApi(&pCtrl)) != ESP_OK) {
		return status;
	}

	MUTEX_GET(pCtrl);
	status = readTotals(pCtrl, ret);
	MUTEX_PUT(pCtrl);

	return status;
}


esp_err_t appEmtrDrvSetRelay(bool on)
{
	drvCtrl_t *	pCtrl;
	esp_err_t	status;

	if ((status = enterApi(&pCtrl)) != ESP_OK) {
		return status;
	}

	MUTEX_GET(pCtrl);
	status = setRelay(pCtrl, on);
	MUTEX_PUT(pCtrl);

	return status;
}


#if 0
esp_err_t pwEmtrDrvFactoryTest(appEmtrTestId_t testId, uint8_t duration)
{
	if (0 == duration) {
		return ESP_ERR_INVALID_ARG;
	}

	drvCtrl_t *	pCtrl;
	esp_err_t	status;

	if ((status = enterApi(&pCtrl)) != ESP_OK) {
		return status;
	}

	uint8_t	payload[4] = {duration, (uint8_t)testId, 0, 0};

	// Expecting 4 bytes returned (will be discarded)
	uint8_t	resp[4];
	int		ioLen = sizeof(resp);

	return csEmtrDrvCommand(CMD_SET_TEST_MODE, payload, resp, &ioLen);
}
#endif


const char * appEmtrDrvStateStr(appEmtrState_t value)
{
	switch(value)
	{
	case appEmtrState_off:
		return "off";
	case appEmtrState_on:
		return "on";
	default:
		return "?";
	}
}


/**
 * \brief Build a cJSON array of names of active alarm conditions
 */
cJSON * appEmtrDrvAlarmListJson(appEmtrAlarm_t alarm)
{
	const char *	sList[8];
	int				sCount = 0;

#if 0	// ToDo define alarm flag bits
	if (alarm.item.acLine) {
		sList[sCount++] = "acLine";
	}
	if (alarm.item.highTemp) {
		sList[sCount++] = "highTemp";
	}
	if (alarm.item.overload) {
		sList[sCount++] = "overload";
	}
	if (alarm.item.underload) {
		sList[sCount++] = "underload";
	}
#endif

	return cJSON_CreateStringArray(sList, sCount);
}


/*
********************************************************************************
********************************************************************************
** Local functions
********************************************************************************
********************************************************************************
*/

static void notify(drvCtrl_t * pCtrl, appEmtrEvtCode_t evtCode, appEmtrEvtData_t * evtData)
{
	//eventNotify(pCtrl->cbHandle, callCtx_local, (uint32_t)evtCode, CS_PTR2ADR(evtData));
}

static esp_err_t enterApi(drvCtrl_t ** pCtrl)
{
	*pCtrl = drvCtrl;
	if (NULL == *pCtrl) {
		ESP_LOGE(TAG, "Driver not initialized");
		return ESP_ERR_INVALID_STATE;
	}
	if (!(*pCtrl)->isRunning) {
		ESP_LOGE(TAG, "Driver not started");
		return ESP_ERR_INVALID_STATE;
	}

	return ESP_OK;
}


/**
 * \brief Read the device status
 */
static esp_err_t readStatus(drvCtrl_t* pCtrl, appEmtrStatus_t* ret)
{
	esp_err_t	status;
	uint8_t		resp[4];	// Expect 4 bytes returned
	int			ioLen = sizeof(resp);

	status = csEmtrDrvCommand(CMD_GET_STATUS, NULL, resp, &ioLen);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "csEmtrDrvCommand(CMD_GET_STATUS) error %d", status);
		return status;
	}

	// Unpack response
	// Byte  Content
	//    0  Relay status
	//    1  Output status
	//    2  Alarm bit mask
	//       Bit   Function
	//         7   Reserved
	//         6   Reserved
	//         5   Reserved
	//         4   Reserved
	//         3   Reserved
	//         2   Reserved
	//         1   Reserved
	//         0   Reserved
	//    3  Temperature (degrees C)

	// Decode relay state
	if (resp[0] & 0x01) {
		ret->relayStatus.value = appEmtrState_on;
	} else {
		ret->relayStatus.value = appEmtrState_off;
	}
	ret->relayStatus.str = appEmtrDrvStateStr(ret->relayStatus.value);

	// Decode output state
	if (resp[1] & 0x01) {
		ret->outputStatus.value = appEmtrState_on;
	} else {
		ret->outputStatus.value = appEmtrState_off;
	}
	ret->outputStatus.str = appEmtrDrvStateStr(ret->outputStatus.value);

	// Unpack the flags byte
	uint8_t	alarms = resp[2];

	// Map the alarm bit mask to the flags structure
	appEmtrAlarm_t*	rFlag = &ret->alarm.value;

	rFlag->item.resvd_7  = (alarms >> 7) & 1;
	rFlag->item.resvd_6  = (alarms >> 6) & 1;
	rFlag->item.resvd_5  = (alarms >> 5) & 1;
	rFlag->item.resvd_4  = (alarms >> 4) & 1;
	rFlag->item.resvd_3  = (alarms >> 3) & 1;
	rFlag->item.resvd_2  = (alarms >> 2) & 1;
	rFlag->item.resvd_1  = (alarms >> 1) & 1;
	rFlag->item.resvd_0  = (alarms >> 0) & 1;

	// Unpack the temperature
	ret->tempC = resp[3];

	// Enable this block to print changes in flag bits
#if 0
	static uint8_t	oldFlags = 0xff;

	if (oldFlags != flags) {
		oldFlags = flags;

		const char *	flagList[8];
		int				flagCt = 0;

		if (rFlag->item.resvd_7)
			flagList[flagCt++] = "rsvd-7";
		if (rFlag->item.resvd_6)
			flagList[flagCt++] = "rsvd-6";
		if (rFlag->item.resvd_5)
			flagList[flagCt++] = "rsvd-5";
		if (rFlag->item.resvd_4)
			flagList[flagCt++] = "rsvd-4";
		if (rFlag->item.resvd_3)
			flagList[flagCt++] = "rsvd-3";
		if (rFlag->item.resvd_2)
			flagList[flagCt++] = "rsvd-2";
		if (rFlag->item.resvd_1)
			flagList[flagCt++] = "rsvd-1";
		if (rFlag->item.resvd_0)
			flagList[flagCt++] = "rsvd-0";

		char	flagStr[60] = {'\0'};
		int		i;

		for (i = 0; i < flagCt; i++) {
			if (i > 0) {
				strcat(flagStr, ", ");
			}
			strcat(flagStr, flagList[i]);
		}

		ESP_LOGD(TAG, "Alarm flags: %02X [%s]", flags, flagStr);
	}
#endif

	return ESP_OK;
}


static esp_err_t readTotals(drvCtrl_t * pCtrl, appEmtrTotals_t * ret)
{
	uint8_t		resp[16];	// Expect 16 bytes returned
	int			ioLen;
	esp_err_t	status;

	ioLen = sizeof(resp);
	status = csEmtrDrvCommand(CMD_GET_TOTALS, NULL, resp, &ioLen);
	if (ESP_OK != status) {
		return status;
	}

	// Unpack response
	// Offset  Len  Content
	//      0    4  Epoch
	//      4    4  On Duration
	//      8    4  Relay Cycles
	//     12    4  Watt-Hours
	csPacker_t	unpack;

	csPackInit(&unpack, resp, sizeof(resp));
	csUnpackBEU32(&unpack, &ret->epoch);
	csUnpackBEU32(&unpack, &ret->onDuration);
	csUnpackBEU32(&unpack, &ret->relayCycles);
	csUnpackBEU32(&unpack, &ret->dWattH);

	return ESP_OK;
}


static esp_err_t readInstValues(drvCtrl_t * pCtrl, appEmtrInstant_t * ret)
{
	uint8_t		resp[16];	// Expect 16 bytes returned
	int			ioLen;
	esp_err_t	status;

	ioLen = sizeof(resp);
	status = csEmtrDrvCommand(CMD_GET_INST_VALUES, NULL, resp, &ioLen);
	if (ESP_OK != status) {
		return status;
	}

	// Unpack response
	// Offset  Len  Content
	//      0    2  dVolts
	//      2    2  mAmps
	//      4    2  dWatts
	//      6    2  pFactor
	//      8    4  seconds power has been on
	//     12    4  seconds relay has been on
	csPacker_t	unpack;

	csPackInit(&unpack, resp, sizeof(resp));
	csUnpackBEU16(&unpack, &ret->dVolts);
	csUnpackBEU16(&unpack, &ret->mAmps);
	csUnpackBEU16(&unpack, &ret->dWatts);
	csUnpackBEU16(&unpack, &ret->pFactor);
	csUnpackBEU32(&unpack, &ret->uptime);
	csUnpackBEU32(&unpack, &ret->relayOnSecs);

	return ESP_OK;
}


static esp_err_t setRelay(drvCtrl_t * pCtrl, bool on)
{
	uint8_t		payload[4] = {0, 0, 0, 0};

	payload[0] = on ? 1 : 0;
	payload[1] = 0;
	payload[2] = 0;
	payload[3] = 0;

	return csEmtrDrvCommand(CMD_SET_RELAY, payload, NULL, NULL);
}


/**
 * This will be called regularly from the EMTR driver timer
 */
static esp_err_t emtrCbDeviceStatus(
	csEmtrDrvCbData_t *		cbData,			//!< Pointer to callback data structure
	uint32_t *				devFlags,		//!< Device-level flags
	uint32_t *				sockFlags,		//!< Array of flag words, one for each socket
	csEmtrDeviceStatus_t *	devStatus
)
{
	//drvCtrl_t *	pCtrl = (drvCtrl_t *)cbData->privData;

	(void)cbData;
	*devFlags = 0;
	(void)sockFlags;
	(void)devStatus;

	return ESP_OK;
}


static void checkEmtr(drvCtrl_t* pCtrl)
{
	esp_err_t	status;

	// Read current status each pass
	appEmtrStatus_t	curStatus;

	MUTEX_GET(pCtrl);
	status = readStatus(pCtrl, &curStatus);
	MUTEX_PUT(pCtrl);

	if (ESP_OK != status) {
		return;
	}

	// If EMTR is in pause state do nothing to generate events
	if (pCtrl->emtrPaused) {
		return;
	}

	// Shorthand references to status structures
	appEmtrStatus_t*	cur = &pCtrl->curDeviceStatus;
	appEmtrStatus_t*	pre = &pCtrl->preDeviceStatus;

	appEmtrEvtData_t	evtData;

	// Copy current status to the control structure
	*cur = curStatus;

	// Check for changes and notify registered clients

	if (pre->tempC != cur->tempC) {
		pre->tempC = cur->tempC;

		evtData.temperature.value = cur->tempC;
		notify(pCtrl, appEmtrEvtCode_temperature, &evtData);
	}

	if (pre->relayStatus.value != cur->relayStatus.value) {
		pre->relayStatus.value = cur->relayStatus.value;

		evtData.state.value = cur->relayStatus.value;
		evtData.state.str   = cur->relayStatus.str;

		notify(pCtrl, appEmtrEvtCode_relayState, &evtData);
	}

	/*
	 * outputStatus should follow relayStatus
	 *
	 * There may be up to 20 ms lag between relayStatus change
	 * and outputStatus changing to match
	 *
	 * outputStatus not matching relayStatus indicates relay failure
	 */
	if (pre->outputStatus.value != cur->outputStatus.value) {
		pre->outputStatus.value = cur->outputStatus.value;

		evtData.state.value = cur->outputStatus.value;
		evtData.state.str   = cur->outputStatus.str;

		notify(pCtrl, appEmtrEvtCode_outputState, &evtData);
	}

	if (pre->alarm.value.mask != cur->alarm.value.mask) {
		pre->alarm.value = cur->alarm.value;

		evtData.alarms.flags  = cur->alarm.value;
		notify(pCtrl, appEmtrEvtCode_alarms, &evtData);
	}

	appEmtrInstant_t	inst;
	if (readInstValues(pCtrl, &inst) == ESP_OK) {
		// Shorthand references to status structures
		appEmtrInstant_t*	cur = &pCtrl->curInstEnergy;
		appEmtrInstant_t*	pre = &pCtrl->preInstEnergy;

		// Copy current status to the control structure
		*cur = inst;

		appEmtrEvtData_t	evtData;

		// Check for changes and notify registered clients
		if (pre->dVolts != cur->dVolts) {
			pre->dVolts = cur->dVolts;

			evtData.dVolts.value = cur->dVolts;
			notify(pCtrl, appEmtrEvtCode_dVolts, &evtData);
		}
	}
}


static void emtrTask(void* arg)
{
	drvCtrl_t*	pCtrl = arg;

	while (1)
	{
		CS_SLEEP_MS(1000);
		checkEmtr(pCtrl);
	}
}
