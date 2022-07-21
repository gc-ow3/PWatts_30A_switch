/*
 * pump_status.c
 *
 *  Created on: Nov 4, 2020
 *      Author: wesd
 */

#include <cJSON.h>

#include "cs_platform.h"
#include "cs_aws_jobs.h"
#include "ww_pump_drv.h"
#include "app_led_drv.h"
#include "app_control.h"
#include "app_mfg_data.h"
#include "app_params.h"
#include "app_aws_client.h"
#include "time_mgr.h"
#include "pump_status.h"

// Comment out the module name to disable debug messages from this file
#define MOD_NAME	"pump_status"
#include "mod_debug.h"

////////////////////////////////////////////////////////////////////////////////
// Convenience macros
////////////////////////////////////////////////////////////////////////////////

// Sleep for a number of milliseconds
#define	SLEEP_MS(t)			vTaskDelay(pdMS_TO_TICKS(t))

// Read number of seconds since boot
#define	TIME_SEC()			timeMgrGetUptime()

// Read number of milliseconds since boot
#define	TIME_MS()			timeMgrGetUptimeMs()

// Acquire and release mutex
#define MUTEX_GET(ctrl)		xSemaphoreTake((ctrl)->mutex, portMAX_DELAY)
#define MUTEX_PUT(ctrl)		xSemaphoreGive((ctrl)->mutex)

typedef struct {
	int16_t	dVolts;
	int16_t	mAmps;
} const hiAmpTab_t;

#define MAX_AMP_STEPS		(3)

typedef struct {
	uint16_t	dVoltsLow;
	uint16_t	dVoltsHigh;
#if 0
	int			hiAmpTabSz;
	hiAmpTab_t	hiAmpTab[MAX_AMP_STEPS];
	int			exAmpTabSz;
	hiAmpTab_t	exAmpTab[MAX_AMP_STEPS];
#endif
	struct {
		uint16_t	highMa;
		uint32_t	holdMs;
	} lockedRotor;
	struct {
		uint16_t	highMa;
		uint16_t	lowMa;
	} waterNotDrop;
	uint64_t	waterDropTimeLimitMs;
	uint8_t		pwrFactor;
	uint32_t	runTime;
	struct {
		uint16_t	lowMa;
		uint32_t	holdMs;
	} thermalTrip;
} const wwStatusLedThreshold_t;


typedef enum {
	condState_inactive = 0,
	condState_inactivePending,
	condState_active,
	condState_activePending,
} condState_t;

typedef struct {
	condState_t	state;
	uint32_t	stateCount;	// Number of sequential times in current state
	uint64_t	chgTimeMs;
	union {
		bool		b;
		uint8_t		u8;
		uint16_t	u16;
		uint32_t	u32;
		float		f;
	} value;
} conditionItem_t;

typedef struct {
	struct {
		int				activeCt;
		//conditionItem_t	hiCurrentExceeded;
		conditionItem_t	rotorLocked;
		conditionItem_t	powerFactor;
		conditionItem_t	currentLeak;
		conditionItem_t	pumpCycleLen;
	} crit;
	struct {
		int				activeCt;
		//conditionItem_t	hiCurrent;
		conditionItem_t	wndDischargeLeak;
		conditionItem_t	wndInflow;
		conditionItem_t	wndObstruction;
		conditionItem_t	hiVoltage;
		conditionItem_t	loVoltage;
		conditionItem_t	waterLevel;
		conditionItem_t	acLine;
		//conditionItem_t	loCurrent;	// future item?
		conditionItem_t	runTime;
		conditionItem_t thermalTrip;
	} warn;
} condition_t;


typedef enum {
	pollState_off = 0,
	pollState_start,
	pollState_run,
	pollState_end
} pollState_t;

typedef struct {
	bool		updated;
	uint32_t	cycleLength;
	uint8_t		pFactor;
	//uint32_t	inrushMa;	// Future
} pumpCycle_t;

typedef struct {
	bool		updated;
	bool		wetTest;
	uint32_t	currentLeak;
	uint8_t		pFactor;
} sysTest_t;

typedef struct {
	wwPumpInstant_t	instant;
	wwPumpTotals_t	total;
	wwPumpStatus_t	status;
	pumpCycle_t		cycle;
	sysTest_t		sysTest;
} pumpData_t;


typedef struct {
	SemaphoreHandle_t			mutex;
	uint64_t					curTimeMs;
	uint64_t					runTimeMs;
	uint64_t					nextCheckTimeMs;
	csLedColor_t				curColor;
	bool						btnOverride;
	wwStatusLedThreshold_t *	threshold;
	struct {
		bool		on;
		uint64_t	onTimeMs;
		uint8_t		waterLevel;
	} 							pumpOn;
	float						hiAmpSlope[MAX_AMP_STEPS-1];
	float						exAmpSlope[MAX_AMP_STEPS-1];
	condition_t					curCond;
	struct {
		uint8_t				level;
		uint64_t			recTimeMs;
		bool				notDropping;
	} waterLevel;
	pollState_t					pollState;
	pumpData_t					curData;
	pumpData_t					preData;
} control_t;

static void csAwsClientCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
);

static void pumpDrvEvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	event,
	uint32_t	evtData
);

static esp_err_t buildAlertsJson(condition_t * cond, cJSON ** jAlerts, cJSON ** jValues);

#if 0
static void	calcAmpSlope(hiAmpTab_t * tab, int tabSz, float * slope);

static bool	isHighAmp(
	float *			slope,
	hiAmpTab_t *	tab,
	int				tabSz,
	uint16_t		mV,		// measured voltage (units of 0.1 V)
	uint16_t		mA		// measured amps    (units of 0.001 A)
);
#endif

static esp_err_t getStatusConditions(control_t * pCtrl);

#define LOCKED_ROTOR_MA			(15000)
#define LOCKED_ROTOR_HOLD_MS	(2000)

#define THERMAL_TRIP_MA			(100)
#define THERMAL_TRIP_HOLD_MS	(5000)

static wwStatusLedThreshold_t	thresholdWayne = {
	.dVoltsLow  = 1040,		// Per WW specification
	.dVoltsHigh = 1280,		// Per WW specification
#if 0
	.hiAmpTabSz = 3,
	.hiAmpTab = {
		{.dVolts = 1270, .mAmps = 7200},
		{.dVolts = 1150, .mAmps = 7200},
		{.dVolts = 1040, .mAmps = 7800}
	},
	.exAmpTabSz = 3,
	.exAmpTab = {
		{.dVolts = 1270, .mAmps = 7700},
		{.dVolts = 1150, .mAmps = 7700},
		{.dVolts = 1040, .mAmps = 8300}
	},
#endif
	.lockedRotor = {
		.highMa = LOCKED_ROTOR_MA,
		.holdMs = LOCKED_ROTOR_HOLD_MS
	},
	.waterNotDrop = {
		.highMa = 6900,
		.lowMa  = 5000
	},
	.waterDropTimeLimitMs = 10000,
	.pwrFactor            = 93,
	.runTime              = (5 * 60),	// 5 minutes
	.thermalTrip = {
		.lowMa    = THERMAL_TRIP_MA,
		.holdMs   = THERMAL_TRIP_HOLD_MS
	}
};


static wwStatusLedThreshold_t	thresholdRigid = {
	.dVoltsLow  = 1040,		// Per WW specification
	.dVoltsHigh = 1270,		// Per WW specification
#if 0
	.hiAmpTabSz = 2,
	.hiAmpTab = {
		{.dVolts = 1280, .mAmps = 8500},
		{.dVolts = 1040, .mAmps = 9500}
	},
	.exAmpTabSz = 2,
	.exAmpTab = {
		{.dVolts = 1270, .mAmps =  9000},
		{.dVolts = 1040, .mAmps = 10000}
	},
#endif
	.lockedRotor = {
		.highMa = LOCKED_ROTOR_MA,
		.holdMs = LOCKED_ROTOR_HOLD_MS
	},
	.waterNotDrop = {
		.highMa = 8500,
		.lowMa  = 5000
	},
	.waterDropTimeLimitMs = 10000,
	.pwrFactor            = 93,
	.runTime              = (5 * 60),	// 5 minutes
	.thermalTrip = {
		.lowMa    = THERMAL_TRIP_MA,
		.holdMs   = THERMAL_TRIP_HOLD_MS
	}
};

static control_t *	control;


esp_err_t appPumpStatusInit(void)
{
	control_t *	pCtrl = control;
	if (pCtrl) {
		return ESP_OK;
	}

	// Create and initialize the control structure
	pCtrl = cs_heap_calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	pCtrl->curColor = csLedColor_green;

	// Install the thresholds per brand of pump
	switch (appInfo.brand)
	{
	case appBrand_rigid:
		pCtrl->threshold = &thresholdRigid;
		break;

	case appBrand_wayne:
		pCtrl->threshold = &thresholdWayne;
		break;

	default:
		gc_err("Brand (%d) not supported, default to \"wayne\"", appInfo.brand);
		pCtrl->threshold = &thresholdWayne;
		break;
	}

#if 0
	// Compute the high amp and exceeded high amp slopes
	calcAmpSlope(pCtrl->threshold->hiAmpTab, pCtrl->threshold->hiAmpTabSz, pCtrl->hiAmpSlope);
	calcAmpSlope(pCtrl->threshold->exAmpTab, pCtrl->threshold->exAmpTabSz, pCtrl->exAmpSlope);
#endif

	if ((pCtrl->mutex = xSemaphoreCreateMutex()) == NULL) {
		gc_err("Mutex create failed");
		return ESP_FAIL;
	}

	appLedDrvSetColor(csLedNum_status, pCtrl->curColor);
	appLedDrvTurnOn(csLedNum_status);

	control = pCtrl;
	return ESP_OK;
}


esp_err_t appPumpStatusStart(void)
{
	control_t *	pCtrl = control;
	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	// Register to be notified of pump driver events
	wwPumpDrvCallbackRegister(pumpDrvEvtCb, CS_PTR2ADR(pCtrl));

	// Register to be notified of AWS client events
	csAwsClientCallbackRegister(csAwsClientCb, CS_PTR2ADR(pCtrl));

	pCtrl->curData.cycle.pFactor = 100;
	pCtrl->curData.sysTest.currentLeak = 0;

	return ESP_OK;
}


void appPumpStatusCheck(void)
{
	control_t *	pCtrl = control;
	if (!pCtrl) {
		return;
	}

	pCtrl->curTimeMs = timeMgrGetUptimeMs();

	if (pCtrl->nextCheckTimeMs > pCtrl->curTimeMs) {
		return;
	}

	// Set next check time (5x per second)
	pCtrl->nextCheckTimeMs = pCtrl->curTimeMs + 200;

	esp_err_t	status;

	// Take a snapshot of current conditions
	MUTEX_GET(pCtrl);
	status = getStatusConditions(pCtrl);
	MUTEX_PUT(pCtrl);

	if (appControlButtonIsPressed()) {
		// Button press overrides status LED control
		if (!pCtrl->btnOverride) {
			pCtrl->btnOverride = true;
		}

		return;
	}

	// Make shorthand reference to the current conditions structure
	condition_t *	cond = &pCtrl->curCond;
	csLedColor_t	newColor = csLedColor_green;

	// Check for change in LED color
	if (ESP_OK == status) {
		if (cond->crit.activeCt > 0) {
			// One or more critical conditions are active
			newColor = csLedColor_red;
		} else if (cond->warn.activeCt > 0) {
			// One or more warning conditions are active
			newColor = csLedColor_yellow;
		}
	}

	// Set color on color change or when restoring from button override
	if (pCtrl->curColor != newColor || pCtrl->btnOverride) {
		pCtrl->curColor = newColor;

		appLedDrvSetColor(csLedNum_status, pCtrl->curColor);

		if (pCtrl->btnOverride) {
			// Recover from button override
			pCtrl->btnOverride = false;
			appLedDrvTurnOn(csLedNum_status);
		}
	}
}


/**
 * \brief Build a cJSON object of critical-level and warning-level active conditions
 *
 * The returned cJSON object will contain either, none, one, or both arrays of
 * conditions
 *
 * {
 *   "crit":[<array of strings>],
 *   "warn":[<array of strings>]
 * }
 */
esp_err_t appPumpStatusJson(cJSON ** jAlerts, cJSON ** jValues)
{
	control_t *	pCtrl = control;
	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}

	esp_err_t	status;

	MUTEX_GET(pCtrl);
	status = buildAlertsJson(&pCtrl->curCond, jAlerts, jValues);
	MUTEX_PUT(pCtrl);

	return status;
}


static void setCurrentLeakCb(
	const char *		method,
	cJSON *				jParams,
	csAwsJobStatus_t *	status,
	uint32_t			cbData
)
{
	control_t *	pCtrl = CS_ADR2PTR(cbData);

	if (!jParams) {
		gc_err("Missing \"params\"");
		status->jobStatus = JOB_EXECUTION_FAILED;
		strlcpy(status->details, "{\"message\":\"Missing params\"}", status->detailsSz);
		return;
	}

	cJSON *	jLo;
	cJSON *	jHi;

	jLo = cJSON_GetObjectItem(jParams, "lo");
	jHi = cJSON_GetObjectItem(jParams, "hi");

	if (!jLo || !jHi) {
		gc_err("Missing either \"hi\" or \"lo\"");
		status->jobStatus = JOB_EXECUTION_FAILED;
		strlcpy(status->details, "{\"message\":\"Need both hi and lo values\"}", status->detailsSz);
		return;
	}

	uint16_t	lo = (uint16_t)jLo->valueint;
	uint16_t	hi = (uint16_t)jHi->valueint;

	// Make sure values are sane
	// hi must be at least 100 uA greater than lo
	if ((hi < lo) || ((hi - lo) < 100)) {
		gc_err("Parameter values are not reasonable");
		status->jobStatus = JOB_EXECUTION_FAILED;
		strlcpy(status->details, "{\"message\":\"Parameter values are not reasonable\"}", status->detailsSz);
		return;
	}

	MUTEX_GET(pCtrl);
	paramMgrSetU16(nvsKey_currentLeakLo, lo);
	paramMgrSetU16(nvsKey_currentLeakHi, hi);
	MUTEX_PUT(pCtrl);

	status->jobStatus = JOB_EXECUTION_SUCCEEDED;
	strlcpy(status->details, "{}", status->detailsSz);
}


static void csAwsClientCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	control_t *				pCtrl  = CS_ADR2PTR(cbData);
	csAwsClientEvtCode_t	eCode  = (csAwsClientEvtCode_t)evtCode;

	switch (eCode)
	{
	case csAwsClientEvtCode_awsFirstConnect:
	case csAwsClientEvtCode_awsReconnect:
		// Register AWS job method handler
		if (csAwsJobsRegister("setCurrentLeak", setCurrentLeakCb, CS_PTR2ADR(pCtrl)) != ESP_OK) {
			gc_err("Failed to register AWS Job method");
		}
		break;

	default:
		// Ignore other events
		break;
	}
}


static void pumpDrvEvtCb(
	uint32_t	cbData,
	callCtx_t	ctx,
	uint32_t	evtCode,
	uint32_t	evtData
)
{
	control_t *			pCtrl = CS_ADR2PTR(cbData);
	wwPumpEvtCode_t		eCode  = (wwPumpEvtCode_t)evtCode;
	wwPumpEvtData_t *	eData  = CS_ADR2PTR(evtData);
	sysTest_t *			sysTest;
	pumpCycle_t *		cycle;

	MUTEX_GET(pCtrl);

	switch (eCode)
	{
	case wwPumpEvtCode_pwrSig:
		if (wwPwrSigReason_pumpOff == eData->pwrSig.pMeta->reason) {
			cycle = &pCtrl->curData.cycle;

			cycle->cycleLength = eData->pwrSig.pMeta->cycleLength;
			cycle->pFactor     = eData->pwrSig.pMeta->pFactor;
			cycle->updated     = true;
		}
		break;

	case wwPumpEvtCode_sysTest:
		sysTest = &pCtrl->curData.sysTest;

		// Current leak measurement is updated only on system test
		sysTest->wetTest     = eData->sysTest->wetTest;
		sysTest->currentLeak = eData->sysTest->currentLeak;
		sysTest->pFactor     = eData->sysTest->pFactor;
		sysTest->updated     = true;
		break;

	default:
		break;
	}

	MUTEX_PUT(pCtrl);
}


#define COND_ACTIVE(item) \
	(condState_active == (item).state || condState_inactivePending == (item).state)


static esp_err_t buildAlertsJson(condition_t * cond, cJSON ** jAlerts, cJSON ** jValues)
{
	const char *	sList[32];
	int				listCt;
	const char *	objName;

	if (!cond || !jAlerts || !jValues) {
		return ESP_ERR_INVALID_ARG;
	}

	*jAlerts = cJSON_CreateObject();
	*jValues = cJSON_CreateObject();

	cJSON *	jArray;
	cJSON *	jCrit = cJSON_AddObjectToObject(*jValues, "crit");

	// Decode active critical-level alarms
	listCt = 0;

	objName = "currentLeak";
	if (COND_ACTIVE(cond->crit.currentLeak)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jCrit, objName, cond->crit.currentLeak.value.u32);
	} else {
		cJSON_AddNullToObject(jCrit, objName);
	}

#if 0
	objName = "hiCurrentExceeded";
	if (COND_ACTIVE(cond->crit.hiCurrentExceeded)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jCrit, objName, cond->crit.hiCurrentExceeded.value.u16);
	} else {
		cJSON_AddNullToObject(jCrit, objName);
	}
#endif

	objName = "powerFactor";
	if (COND_ACTIVE(cond->crit.powerFactor)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jCrit, objName, ((float)cond->crit.powerFactor.value.u8)/100.0);
	} else {
		cJSON_AddNullToObject(jCrit, objName);
	}

	objName = "pumpCycleLen";
	if (COND_ACTIVE(cond->crit.pumpCycleLen)) {
		sList[listCt++] = objName;
		cJSON_AddBoolToObject(jCrit, objName, cond->crit.pumpCycleLen.value.b);
	} else {
		cJSON_AddNullToObject(jCrit, objName);
	}

	objName = "rotorLocked";
	if (COND_ACTIVE(cond->crit.rotorLocked)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jCrit, objName, ((float)cond->crit.rotorLocked.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jCrit, objName);
	}

	if (listCt > 0) {
		jArray = cJSON_CreateStringArray(sList, listCt);
		cJSON_AddItemToObject(*jAlerts, "crit", jArray);
	} else {
		// No active conditions - send an empty array
		cJSON_AddArrayToObject(*jAlerts, "crit");
	}

	// Decode active warning-level alarms
	listCt = 0;
	cJSON *	jWarn = cJSON_AddObjectToObject(*jValues, "warn");

	objName = "acLine";
	if (COND_ACTIVE(cond->warn.acLine)) {
		sList[listCt++] = objName;
		cJSON_AddBoolToObject(jWarn, objName, cond->warn.acLine.value.b);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

#if 0	// Disabled for now per WW's request
	objName = "dischargeLeak";
	if (COND_ACTIVE(cond->warn.wndDischargeLeak)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.wndDischargeLeak.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}
#endif

	objName = "inflow";
	if (COND_ACTIVE(cond->warn.wndInflow)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.wndInflow.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	objName = "obstruction";
	if (COND_ACTIVE(cond->warn.wndObstruction)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.wndObstruction.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

#if 0
	objName = "hiCurrent";
	if (COND_ACTIVE(cond->warn.hiCurrent)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.hiCurrent.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}
#endif

	objName = "hiVoltage";
	if (COND_ACTIVE(cond->warn.hiVoltage)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.hiVoltage.value.u16)/10.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	objName = "loVoltage";
	if (COND_ACTIVE(cond->warn.loVoltage)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.loVoltage.value.u16)/10.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	objName = "waterLevel";
	if (COND_ACTIVE(cond->warn.waterLevel)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, cond->warn.waterLevel.value.u8);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	objName = "thermalTrip";
	if (COND_ACTIVE(cond->warn.thermalTrip)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, cond->warn.thermalTrip.value.u16);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	/* future item ?
	objName = "loCurrent";
	if (COND_ACTIVE(cond->warn.loCurrent)) {	// ToDo Not being set yet
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, ((float)cond->warn.loCurrent.value.u16)/1000.0);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}
	*/

	objName = "runTime";
	if (COND_ACTIVE(cond->warn.runTime)) {
		sList[listCt++] = objName;
		cJSON_AddNumberToObject(jWarn, objName, cond->warn.runTime.value.u32);
	} else {
		cJSON_AddNullToObject(jWarn, objName);
	}

	if (listCt > 0) {
		jArray = cJSON_CreateStringArray(sList, listCt);
		cJSON_AddItemToObject(*jAlerts, "warn", jArray);
	} else {
		// No active conditions - send an empty array
		cJSON_AddArrayToObject(*jAlerts, "warn");
	}

	return ESP_OK;
}


#if 0
static void	calcAmpSlope(hiAmpTab_t * tab, int tabSz, float * slope)
{
	if (tabSz < 1) {
		return;
	} else if (tabSz < 2) {
		*slope = 1.0;
		return;
	}

	int	i;
	for (i = 0; i < tabSz-1; i++, slope++) {
		int16_t	deltaV = (int16_t)tab[i].dVolts - (int16_t)tab[i+1].dVolts;
		int16_t	deltaA = (int16_t)tab[i+1].mAmps - (int16_t)tab[i].mAmps;

		// Compute slope of mAmps/dVolts for this step
		*slope = (float)deltaA/(float)deltaV;
	}
}


static bool	isHighAmp(
	float *			slope,
	hiAmpTab_t *	tab,
	int				tabSz,
	uint16_t		mV,		// measured voltage (units of 0.1 V)
	uint16_t		mA		// measured amps    (units of 0.001 A)
)
{
	if (tabSz < 2) {
		return false;
	}

	int	i;
	for (i = 0; i < tabSz-1; i++) {
		//   Find range instant volts falls into
		if (mV <= tab[i].dVolts && mV >= tab[i+1].dVolts) {
			uint16_t	deltaV = tab[i].dVolts - mV;

			// Compute the mAmps limit for the given dVolts
			uint16_t	limit = (uint16_t)((float)tab[i].mAmps + ((float)deltaV * slope[i]));

			//gc_dbg("mA limit @ %u dV: %u", mV, limit);
			//gc_dbg("Measured mA: %u", mA);

			// See if limit is exceeded
			return (mA > limit) ? true : false;
		}
	}

	return false;
}
#endif


typedef struct {
	control_t *		pCtrl;
	uint64_t		curTimeMs;
	int *			activeCt;
	int				changeCt;
} itemCheckParams_t;

typedef enum {
	itemCheckMode_standard,
	itemCheckMode_sticky,
	itemCheckMode_force
} itemCheckMode_t;


static void _checkConditionItem(
	itemCheckParams_t *	check,
	itemCheckMode_t		mode,
	conditionItem_t *	item,
	bool				active,
	uint32_t			holdMs
)
{
	switch (item->state)
	{
	case condState_inactive:
		if (active) {
			if (itemCheckMode_force == mode) {
				// Move immediately to active state
				item->state         = condState_active;
				check->changeCt    += 1;
				*(check->activeCt) += 1;
			} else {
				// Begin transition to active state
				item->state     = condState_activePending;
				item->chgTimeMs = check->curTimeMs + holdMs;
			}
		}
		break;

	case condState_activePending:
		if (active) {
			if (itemCheckMode_force == mode || check->curTimeMs >= item->chgTimeMs) {
				item->state         = condState_active;
				check->changeCt    += 1;
				*(check->activeCt) += 1;
			}
		} else {
			// Ignore transient event
			item->state = condState_inactive;
		}
		break;

	case condState_active:
		if (!active) {
			if (itemCheckMode_force == mode) {
				// Move immediately to inactive state
				item->state         = condState_inactive;
				check->changeCt    += 1;
				*(check->activeCt) -= 1;
			} else if (itemCheckMode_standard == mode) {
				// Begin transition to inactive state
				item->state     = condState_inactivePending;
				item->chgTimeMs = check->curTimeMs + holdMs;
			}
		}
		break;

	case condState_inactivePending:
		if (active) {
			// Ignore transient event
			item->state = condState_active;
		} else if (itemCheckMode_force == mode || check->curTimeMs >= item->chgTimeMs) {
			// Move to inactive state
			item->state         = condState_inactive;
			check->changeCt    += 1;
			*(check->activeCt) -= 1;
		}
		break;

	default:
		gc_err("Invalid condState (%d)", item->state);
		break;
	}
}


static void checkConditionItem(
	itemCheckParams_t *	check,
	itemCheckMode_t		mode,
	conditionItem_t *	item,
	bool				active
)
{
	_checkConditionItem(check, mode, item, active, 1000);
}


static void checkConditionItem2(
	itemCheckParams_t *	check,
	itemCheckMode_t		mode,
	conditionItem_t *	item,
	bool				active,
	uint32_t			holdMs
)
{
	_checkConditionItem(check, mode, item, active, holdMs);
}


#define MIN_RUN_MS		(1000)

static esp_err_t getStatusConditions(control_t * pCtrl)
{
	static uint64_t	holdOff = 0;

	// Get shorthand for time
	uint64_t	curTimeMs = pCtrl->curTimeMs;
	esp_err_t	status;

	if (curTimeMs < holdOff) {
		// Don't query the EMTR so often during tests
		return ESP_OK;
	}

	// Get snapshots of current pump data

	wwPumpInstant_t *	instant = &pCtrl->curData.instant;

	status = wwPumpDrvGetInstValues(instant);

	if (ESP_ERR_INVALID_STATE == status) {
		// The EMTR is busy, probably system test
		// Hold off further accesses until it's ready
		holdOff = curTimeMs + 2000;
		return ESP_OK;
	} else if (ESP_OK != status) {
		gc_err("Failed to read pump instant values");
		return status;
	}

	wwPumpStatus_t *pump = &pCtrl->curData.status;
	if (wwPumpDrvGetStatus(pump) != ESP_OK) {
		gc_err("Failed to read pump status");
		return ESP_FAIL;
	}

	// Shorthand reference to the threshold values
	wwStatusLedThreshold_t *	thr = pCtrl->threshold;

	// Shorthand reference to the condition structure
	condition_t *	curCond = &pCtrl->curCond;

	if (wwPumpState_off == pump->pumpState.value) {
		// Pump is off
		if (pCtrl->pumpOn.on) {
			// Pump transitioned from on to off
			pCtrl->pollState = pollState_end;
			pCtrl->pumpOn.on = false;

			// Clear the not dropping condition at the end of the cycle
			pCtrl->waterLevel.notDropping = false;
		}
	} else {
		// Pump is on either via test or water level
		if (pCtrl->pumpOn.on) {
			// Pump continues to run, track the time it is active
			pCtrl->runTimeMs = curTimeMs - pCtrl->pumpOn.onTimeMs;

			// Checks to do for wet cycles
			if (pCtrl->pumpOn.waterLevel >= WW_WET_WELL_LEVEL) {

				// Check for water not dropping (or not fast enough) during this cycle
				// Once set, the condition remains active for the current cycle
				if (!pCtrl->waterLevel.notDropping) {
					// Check if water level has changed in the previous 10 seconds of running
					if (curTimeMs - pCtrl->waterLevel.recTimeMs >= 10000) {
						if (pump->waterLevel >= pCtrl->waterLevel.level) {
							pCtrl->waterLevel.notDropping = true;
						} else {
							// Capture the current level for the next test
							pCtrl->waterLevel.level = pump->waterLevel;
							pCtrl->waterLevel.recTimeMs = curTimeMs;
						}
					}
				}
			}
		} else {
			// Pump transitioned from off to on
			pCtrl->pollState = pollState_start;

			// Record the time and water level when the pump transitioned
			pCtrl->pumpOn.on            = true;
			pCtrl->pumpOn.onTimeMs      = curTimeMs;
			pCtrl->runTimeMs            = 0;

			// Capture the starting water level
			pCtrl->pumpOn.waterLevel    = pump->waterLevel;

			// Track the state of the water level as the pump runs
			pCtrl->waterLevel.level       = pump->waterLevel;
			pCtrl->waterLevel.recTimeMs   = curTimeMs;
			pCtrl->waterLevel.notDropping = false;
		}
	}

	// Set up to check individual conditions
	itemCheckParams_t	check = {
		.pCtrl     = pCtrl,
		.curTimeMs = curTimeMs,
		.changeCt  = 0
	};

	////////////////////////////////////////////////////////////////////////////
	// Check for critical conditions
	////////////////////////////////////////////////////////////////////////////
	check.activeCt = &curCond->crit.activeCt;

	// Conditions to be checked when the pump has run for a minimum time
	if (pCtrl->runTimeMs >= MIN_RUN_MS &&
		(pollState_run == pCtrl->pollState || pollState_end == pCtrl->pollState)
	) {
		itemCheckMode_t		checkMode;
		pumpData_t *		pumpData;

		if (pollState_run == pCtrl->pollState) {
			checkMode = itemCheckMode_sticky;
			pumpData  = &pCtrl->curData;
		} else {
			checkMode = itemCheckMode_standard;
			pumpData  = &pCtrl->preData;
		}

		// Shorthand to instant values
		wwPumpInstant_t *	inst = &pumpData->instant;

#if 0
		// Check for exceeded high current limit
		curCond->crit.hiCurrentExceeded.value.u16 = inst->mAmps;
		checkConditionItem(
			&check,
			checkMode,
			&curCond->crit.hiCurrentExceeded,
			isHighAmp(
				pCtrl->exAmpSlope,
				thr->exAmpTab,
				thr->exAmpTabSz,
				inst->dVolts,
				inst->mAmps
			)
		);
#endif

		// Checked for locked rotor
		curCond->crit.rotorLocked.value.u16 = inst->mAmps;
		checkConditionItem2(
			&check,
			checkMode,
			&curCond->crit.rotorLocked,
			(inst->mAmps > thr->lockedRotor.highMa),
			thr->lockedRotor.holdMs
		);
	}

	// Checks to do after a pump cycle completes
	pumpCycle_t *	cycle = &pCtrl->curData.cycle;
	if (cycle->updated) {
		cycle->updated = false;

		gc_dbg("Pump cycle update (water level: %u, cycle time: %u)", pCtrl->pumpOn.waterLevel, cycle->cycleLength);

		// Checks to do only on wet-well pump runs of at least 3 seconds
		if (pCtrl->pumpOn.waterLevel >= WW_WET_WELL_LEVEL && cycle->cycleLength >= 3) {
			// Check power factor
			bool curActive  = COND_ACTIVE(curCond->crit.powerFactor);
			uint8_t	testVal = cycle->pFactor;

			gc_dbg("  Check power factor (%u), alarm: %s", cycle->pFactor, curActive ? "true" : "false");

			if (testVal < thr->pwrFactor) {
				gc_dbg("  Power factor below minimum threshold");
				// Power factor is below the safe threshold
				if (curActive) {
					// Already active - reset the trigger count
					curCond->crit.powerFactor.stateCount = 0;
				} else {
					// Need 2 consecutive cycles to trigger a state change
					if (++curCond->crit.powerFactor.stateCount == 2) {
						gc_dbg("  Activate power factor alert");
						curCond->crit.powerFactor.stateCount = 0;
						// Activate the alert
						curCond->crit.powerFactor.value.u8 = testVal;
						checkConditionItem(
							&check,
							itemCheckMode_force,
							&curCond->crit.powerFactor,
							true
						);
					}
				}
			} else {
				// Power factor is above the safe threshold
				if (curActive) {
					// Need 2 consecutive cycles to trigger a state change
					if (++curCond->crit.powerFactor.stateCount == 2) {
						gc_dbg("  Deactivate power factor alert");
						curCond->crit.powerFactor.stateCount = 0;
						// Deactivate the alert
						checkConditionItem(
							&check,
							itemCheckMode_force,
							&curCond->crit.powerFactor,
							false
						);
					}
				} else {
					// Reset the trigger count
					curCond->crit.powerFactor.stateCount = 0;
				}
			}
		}
	}

	// Conditions to be checked after system test
	sysTest_t * sysTest = &pCtrl->curData.sysTest;
	if (sysTest->updated) {
		sysTest->updated = false;

		gc_dbg("System test update");

		// Check Current Leak using value from most recent system test
		uint32_t	value = sysTest->currentLeak;
		curCond->crit.currentLeak.value.u32 = value;

		if (appParams.pumpHealthOk) {
			// Check if alert condition is active
			if (value > (uint32_t)appParams.currentLeak.hi) {
				// Alert state changed from not active to active
				paramMgrSetBool(nvsKey_pumpHealthOk, false);
				csAwsUpdateShadowItem(&appShadow_pumpHealth, 0, "bad");

				checkConditionItem(
					&check,
					itemCheckMode_force,
					&curCond->crit.currentLeak,
					true
				);
			}
		} else {
			// Check if alert condition is still active
			if (value < (uint32_t)appParams.currentLeak.lo) {
				// Alert state changed from active to not active
				paramMgrSetBool(nvsKey_pumpHealthOk, true);
				csAwsUpdateShadowItem(&appShadow_pumpHealth, 0, "good");

				checkConditionItem(
					&check,
					itemCheckMode_force,
					&curCond->crit.currentLeak,
					false
				);
			}
		}
	}

	// Conditions to be checked always
	// TBD ...

	////////////////////////////////////////////////////////////////////////////
	// Check for warning conditions
	////////////////////////////////////////////////////////////////////////////
	check.activeCt = &curCond->warn.activeCt;

	// Conditions to be checked when the pump has run for a minimum time
	if (pCtrl->runTimeMs >= MIN_RUN_MS &&
		(pollState_run == pCtrl->pollState || pollState_end == pCtrl->pollState)
	) {
		itemCheckMode_t		checkMode;
		wwPumpInstant_t *	inst;

		if (pollState_run == pCtrl->pollState) {
			checkMode = itemCheckMode_sticky;
			inst      = &pCtrl->curData.instant;
		} else {
			checkMode = itemCheckMode_standard;
			inst      = &pCtrl->preData.instant;
		}

#if 0
		// High current
		curCond->warn.hiCurrent.value.u16 = inst->mAmps;
		checkConditionItem(
			&check,
			checkMode,
			&curCond->warn.hiCurrent,
			isHighAmp(
				pCtrl->hiAmpSlope,
				thr->hiAmpTab,
				thr->hiAmpTabSz,
				inst->dVolts,
				inst->mAmps
			)
		);
#endif

		if (pCtrl->waterLevel.notDropping) {
			// Water is not dropping, the amps may tell us why

			if (inst->mAmps > thr->waterNotDrop.highMa) {
				// High current indicates leak in the discharge pipe
#if 0	// This is disabled for now per WW's request
				curCond->warn.wndDischargeLeak.value.u16 = inst->mAmps;
				checkConditionItem(
					&check,
					checkMode,
					&curCond->warn.wndDischargeLeak,
					true
				);
#endif
			} else if (inst->mAmps < thr->thermalTrip.lowMa) {
				// Very low current indicates pump not running
				curCond->warn.thermalTrip.value.u16 = inst->mAmps;
				checkConditionItem2(
					&check,
					checkMode,
					&curCond->warn.thermalTrip,
					true,
					thr->thermalTrip.holdMs
				);
			} else if (inst->mAmps < thr->waterNotDrop.lowMa) {
				// Low current indicates obstruction in the discharge pipe
				curCond->warn.wndObstruction.value.u16 = inst->mAmps;
				checkConditionItem(
					&check,
					checkMode,
					&curCond->warn.wndObstruction,
					true
				);
			} else {
				// Normal current indicates high water inflow rate
				curCond->warn.wndInflow.value.u16 = inst->mAmps;
				checkConditionItem(
					&check,
					checkMode,
					&curCond->warn.wndInflow,
					true
				);
			}
		} else {
#if 0
			checkConditionItem(
				&check,
				itemCheckMode_force,
				&curCond->warn.wndDischargeLeak,
				false
			);
#endif

			checkConditionItem(
				&check,
				itemCheckMode_force,
				&curCond->warn.thermalTrip,
				false
			);

			checkConditionItem(
				&check,
				itemCheckMode_force,
				&curCond->warn.wndObstruction,
				false
			);

			checkConditionItem(
				&check,
				itemCheckMode_force,
				&curCond->warn.wndInflow,
				false
			);
		}
	} else {
		// Conditions to be checked while the pump is off

		// High household voltage
		curCond->warn.hiVoltage.value.u16 = instant->dVolts;
		checkConditionItem(
			&check,
			itemCheckMode_standard,
			&curCond->warn.hiVoltage,
			(instant->dVolts > thr->dVoltsHigh)
		);

		// Low household voltage
		curCond->warn.loVoltage.value.u16 = instant->dVolts;
		checkConditionItem(
			&check,
			itemCheckMode_standard,
			&curCond->warn.loVoltage,
			(instant->dVolts < thr->dVoltsLow)
		);
	}

	// Conditions to be checked always

	// Water level
	curCond->warn.waterLevel.value.u8 = pump->waterLevel;
	checkConditionItem(
		&check,
		itemCheckMode_standard,
		&curCond->warn.waterLevel,
		(pump->waterLevel > 4)
	);

	// AC line issue (missing ground, miswired, etc
	curCond->warn.acLine.value.b = pump->alarm.flags.item.acLine;
	checkConditionItem(
		&check,
		itemCheckMode_standard,
		&curCond->warn.acLine,
		pump->alarm.flags.item.acLine
	);

	// Check for run time exceeding time limit (code 36)
	curCond->warn.runTime.value.u32 = pCtrl->runTimeMs/1000;
	checkConditionItem(
		&check,
		itemCheckMode_standard,
		&curCond->warn.runTime,
		(pollState_run == pCtrl->pollState) && (curCond->warn.runTime.value.u32 >= thr->runTime)
	);

	////////////////////////////////////////////////////////////////////////////
	// Done with checks
	////////////////////////////////////////////////////////////////////////////

	// Check for active conditions that will turn on the siren
	// ToDo eventually add:
	//   loCurrentExceeded
	bool	siren =
		pCtrl->waterLevel.notDropping
		|| COND_ACTIVE(curCond->warn.waterLevel)
		|| COND_ACTIVE(curCond->crit.currentLeak)
#if 0
		|| COND_ACTIVE(curCond->crit.hiCurrentExceeded)
#endif
		|| COND_ACTIVE(curCond->crit.rotorLocked)
		|| COND_ACTIVE(curCond->crit.powerFactor)
	;
	appControlSirenSet(siren);

	// If there were any changes, update the shadow
	if (check.changeCt > 0) {
		cJSON *	jAlerts;
		cJSON *	jValues;

		if (buildAlertsJson(curCond, &jAlerts, &jValues) == ESP_OK) {
			// Build a list of shadow updates
			csAwsShadowUpdateList_t	lst[2];
			int						lstCt = 0;

			lst[lstCt].item       = &appShadow_alerts;
			lst[lstCt].inst       = 0;
			lst[lstCt].value.jObj = jAlerts;
			lstCt += 1;

			lst[lstCt].item       = &appShadow_alertValues;
			lst[lstCt].inst       = 0;
			lst[lstCt].value.jObj = jValues;
			lstCt += 1;

			csAwsUpdateShadowItemList(lst, lstCt);
		}
	}

	// Move polling state from transitional to steady value
	switch (pCtrl->pollState)
	{
	case pollState_start:
		pCtrl->pollState = pollState_run;
		break;

	case pollState_run:
		// Copy current state values
		pCtrl->preData = pCtrl->curData;
		break;

	case pollState_end:
		pCtrl->pollState = pollState_off;
		break;

	case pollState_off:
		break;

	default:
		break;
	}

	return ESP_OK;
}
