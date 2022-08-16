/*
 * outlet_drv.c
 *
 *  Created on: Jul 13, 2018
 *      Author: wesd
 */
#include "cs_common.h"
#include "cs_heap.h"
#include "cs_platform.h"
#include "cs_heap.h"
#include "event_callback.h"
#include "param_mgr.h"
#include "emtr_drv.h"
#include "cap1298_drv.h"
#include "app_led_drv.h"
#include "app_params.h"
#include "app_led_mgr.h"

// Comment out the MOD_NAME line to disable debug prints from this file
#define MOD_NAME	"led_mgr"
#include "mod_debug.h"


////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

// How frequently to check LED flash and Fade states
#define TASK_CYCLE_TIME_MS		(10)
// The number of milliseconds over which to fade
#define LED_FADE_TIME_MS		(1000)


////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

typedef struct {
	float		currentLevel;
	float		targetLevel;
	bool		adjusting;
	float		step;
	uint32_t	fullBrightMs;
} fadeCtrl_t;


typedef enum {
	ledState_undef = 0,
	ledState_on,
	ledState_off,
	ledState_repeatDelay
} ledState_t;


/**
 * \brief control structure for an individual LED
 */
typedef struct {
	ledState_t		state;
	uint32_t		onTimeMs;
	uint32_t		offTimeMs;
	uint32_t		numCycles;
	uint32_t		repeatDelay;
	uint32_t		cycleCount;
	uint32_t		timer;
	csLedColor_t	color;
} ledCtrl_t;


/**
 * \brief Control structure for the LED manager
 */
typedef struct {
	cbHandle_t			cbHandle;
	bool				isStarted;
	TaskHandle_t		taskHandle;
	SemaphoreHandle_t	mutex;
	uint8_t				brightness;
	fadeCtrl_t			fade;
	ledCtrl_t			ledCtrl[NUM_LEDS];
} ledMgrCtrl_t;


////////////////////////////////////////////////////////////////////////////////
// local functions
////////////////////////////////////////////////////////////////////////////////
static int checkCall(ledMgrCtrl_t * pCtrl, csLedNum_t led);
static void ledMgrTask(void * taskParam);


////////////////////////////////////////////////////////////////////////////////
// local data
////////////////////////////////////////////////////////////////////////////////

// Allocate the control structure
static ledMgrCtrl_t *	ledCtrl;


/**
 * \brief Initialize the LED manager
 */
esp_err_t ledMgrInit(uint8_t brightness)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL != pCtrl)
		return ESP_OK;

	if ((pCtrl = cs_heap_calloc(1, sizeof(*pCtrl))) == NULL)
		return ESP_ERR_NO_MEM;

	pCtrl->brightness = brightness;

	if ((pCtrl->mutex = xSemaphoreCreateMutex()) == NULL) {
		gc_err("Mutex create failed");
		return ESP_FAIL;
	}

	esp_err_t	status;

	// Set up registrations for LED events
	status = eventRegisterCreate(&pCtrl->cbHandle);
	if (ESP_OK != status) {
		return status;
	}

	// Initialize the LED driver
	if (appLedDrvInit(brightness) != ESP_OK) {
		gc_err("appLedDrvInit failed");
		return ESP_FAIL;
	}

	int		i;

	for (i = 0; i < NUM_LEDS; i++) {
		pCtrl->ledCtrl[i].color = csLedColor_blue;
	}

	ledCtrl = pCtrl;
	return ESP_OK;
}


/**
 * \brief Start the LED manager
 */
esp_err_t ledMgrStart(void)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;
	if (pCtrl->isStarted)
		return ESP_OK;

	pCtrl->fade.currentLevel = (float)pCtrl->brightness;
	pCtrl->fade.targetLevel  = pCtrl->fade.currentLevel;

	// Start the LED driver
	if (appLedDrvStart() != ESP_OK) {
		gc_err("appLedDrvStart failed");
		return ESP_FAIL;
	}

	esp_err_t	status;

	// Start the control task
	status = xTaskCreate(
		ledMgrTask,
		"ledMgrTask",
		2000,
		(void *)pCtrl,
		TASK_PRIO_LED_MGR,
		&pCtrl->taskHandle
	);
	if (pdPASS != status) {
		gc_err("Task create failed");
		return ESP_FAIL;
	}

	pCtrl->isStarted = true;
	return ESP_OK;
}


esp_err_t ledMgrCallbackRegister(eventCbFunc_t cbFunc, uint32_t cbData)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	return eventRegisterCallback(pCtrl->cbHandle, cbFunc, cbData);
}


esp_err_t ledMgrTurnLedOn(csLedNum_t ledNum)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t	status;

	// Check parameters and lock the mutex
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK)
		return status;

	int	ledIdx = ledNum - 1;

	// Reference the control structure for this LED
	ledCtrl_t *	pLed = &pCtrl->ledCtrl[ledIdx];

	// Record the state
	pLed->state       = ledState_on;
	pLed->timer       = 0;
	pLed->cycleCount  = 0;
	pLed->repeatDelay = 0;

	status = appLedDrvTurnOn(ledNum);
	if (ESP_OK != status) {
		gc_err("appLedDrvTurnOn failed");
		goto exitMutex;
	}

exitMutex:
	xSemaphoreGive(pCtrl->mutex);
	return status;
}


esp_err_t ledMgrTurnLedOff(csLedNum_t ledNum)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t	status;

	// Check parameters and lock the mutex
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK)
		return status;

	int	ledIdx = ledNum - 1;

	// Reference the control structure for this LED
	ledCtrl_t *	pLed = &pCtrl->ledCtrl[ledIdx];

	// Record the state
	pLed->state       = ledState_off;
	pLed->timer       = 0;
	pLed->cycleCount  = 0;
	pLed->repeatDelay = 0;

	status = appLedDrvTurnOff(ledNum);
	if (ESP_OK != status) {
		gc_err("appLedDrvTurnOff failed");
		goto exitMutex;
	}

exitMutex:
	xSemaphoreGive(pCtrl->mutex);
	return status;
}


esp_err_t ledMgrSetLedColor(csLedNum_t ledNum, csLedColor_t color)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t	status;

	// Check parameters and lock the mutex
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK)
		return status;

	pCtrl->ledCtrl[ledNum - 1].color = color;
	status = appLedDrvSetColor(ledNum, color);

	xSemaphoreGive(pCtrl->mutex);
	return status;
}


csLedColor_t ledMgrGetLedColor(csLedNum_t ledNum)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return csLedColor_null;

	if (ledNum < 1 || ledNum >= NUM_LEDS)
		return csLedColor_null;

	return pCtrl->ledCtrl[ledNum - 1].color;
}


esp_err_t ledMgrFlashLed(
	csLedNum_t	ledNum,
	uint32_t	onTimeMs,
	uint32_t	offTimeMs,
	uint32_t	numCycles,
	uint32_t	repeatDelay
)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	esp_err_t	status;

	// Check parameters and lock the mutex
	if ((status = checkCall(pCtrl, ledNum)) != ESP_OK)
		return status;

	int	ledIdx = ledNum - 1;

	// Reference the control structure for this LED
	ledCtrl_t *	pLed = &pCtrl->ledCtrl[ledIdx];

	pLed->state       = ledState_on;
	pLed->onTimeMs    = onTimeMs;
	pLed->offTimeMs   = offTimeMs;
	pLed->numCycles   = numCycles;
	pLed->timer       = onTimeMs;
	pLed->cycleCount  = 0;
	pLed->repeatDelay = repeatDelay;

	status = appLedDrvTurnOn(ledNum);
	if (ESP_OK != status) {
		gc_err("appLedDrvTurnOn failed");
		goto exitMutex;
	}

exitMutex:
	xSemaphoreGive(pCtrl->mutex);
	return status;
}


/**
 * \brief Fade LED up to 100 percent
 *
 * \param [in] fadeUpTime Number of milliseconds to hold at 100 percent
 *
 */
void ledMgrFadeUp(uint32_t holdTime)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);
	pCtrl->fade.targetLevel  = 100.0;
	pCtrl->fade.fullBrightMs = holdTime;
	xSemaphoreGive(pCtrl->mutex);
}


/**
 * \brief Fade LED down to the level of the brightness parameter
 */
void ledMgrFadeDown(void)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return;

	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);
	pCtrl->fade.targetLevel = (float)pCtrl->brightness;
	xSemaphoreGive(pCtrl->mutex);
}


/**
 * \brief Set the new brightness level, store to parameters
 */
esp_err_t ledMgrSetBrightness(int level, callCtx_t ctx)
{
	ledMgrCtrl_t *	pCtrl = ledCtrl;
	if (NULL == pCtrl)
		return ESP_FAIL;

	if (!pCtrl->isStarted)
		return ESP_FAIL;

	if (level < 0 || level > 100)
		return ESP_ERR_INVALID_ARG;

	pCtrl->brightness = level;

	if (callCtx_null != ctx) {
		// Store the parameter
		paramMgrSetU8("brightness", (uint8_t)level);
	}

	// Adjust the LED
	xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);
	appLedDrvSetBrightness(level);
	//pCtrl->fade.targetLevel = (float)level;
	xSemaphoreGive(pCtrl->mutex);

	if (callCtx_null != ctx) {
		// Notify interested parties of the change to this parameter
		eventNotify(
			pCtrl->cbHandle,
			ctx,
			(uint32_t)ledMgrEvt_setBrightness,
			(uint32_t)level
		);
	}

	return ESP_OK;
}


static esp_err_t checkCall(ledMgrCtrl_t * pCtrl, csLedNum_t ledNum)
{
	esp_err_t		status;

	if (!pCtrl->isStarted)
		return ESP_FAIL;

	if (ledNum < 1 || ledNum > NUM_LEDS)
		return ESP_ERR_INVALID_ARG;

	if (xSemaphoreTake(pCtrl->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
		status = ESP_OK;
	} else {
		status = ESP_ERR_TIMEOUT;
	}

	return status;
}


/**
 * \brief Check if LED is fading up or down
 */
static void checkFade(ledMgrCtrl_t * pCtrl)
{
	fadeCtrl_t * pFade = &pCtrl->fade;

	// Check for LEDs temporarily at 100 percent
	if (pFade->fullBrightMs > 0) {
		if (pFade->fullBrightMs > TASK_CYCLE_TIME_MS) {
			pFade->fullBrightMs -= TASK_CYCLE_TIME_MS;
		} else {
			// Restore to the user-selected brightness level
			pFade->fullBrightMs = 0;
			pFade->targetLevel  = (float)pCtrl->brightness;
		}

		return;
	}

	if (pFade->adjusting) {
		if (pFade->currentLevel == pFade->targetLevel) {
			// Done adjusting
			pFade->adjusting = false;
			//gc_dbg("Fade complete");
		} else {
			float	fadeDiff = pFade->currentLevel - pFade->targetLevel;

			fadeDiff = CS_FABS(fadeDiff);

			//gc_dbg("Fade current: %f", pFade->currentLevel);
			//gc_dbg("Fade target : %f", pFade->targetLevel);
			//gc_dbg("Fade diff   : %f", fadeDiff);

			// Make the next adjustment
			if (fadeDiff < CS_FABS(pFade->step)) {
				// Final step - fractional increment
				pFade->currentLevel = pFade->targetLevel;
			} else {
				// Next step
				pFade->currentLevel += pFade->step;
			}

			appLedDrvSetBrightness((int)pFade->currentLevel);
		}
	} else if (pFade->currentLevel != pFade->targetLevel) {
		// Begin fade
		float	steps = (float)LED_FADE_TIME_MS / (float)TASK_CYCLE_TIME_MS;

		pFade->adjusting = true;
		pFade->step      = (pFade->targetLevel - pFade->currentLevel) / steps;
		//gc_dbg("fade step value: %f", pFade->step);
	}
}


static void checkLedFlash(ledMgrCtrl_t * pCtrl, int ledIdx)
{
	ledCtrl_t *		pLed = &pCtrl->ledCtrl[ledIdx];
	csLedNum_t		ledNum = 1 + ledIdx;

	if (pLed->timer == 0)
		return;

	if (pLed->timer > TASK_CYCLE_TIME_MS) {
		pLed->timer -= TASK_CYCLE_TIME_MS;
		return;
	}

	// Timer expired
	switch (pLed->state)
	{
	case ledState_on:
		// Turn off the LED
		appLedDrvTurnOff(ledNum);

		pLed->state = ledState_off;
		pLed->timer = pLed->offTimeMs;

		if (pLed->numCycles > 0) {
			pLed->cycleCount += 1;
			if (pLed->cycleCount == pLed->numCycles) {
				if (pLed->repeatDelay > 0) {
					pLed->state = ledState_repeatDelay;
					pLed->timer = pLed->repeatDelay;
				} else {
					pLed->timer = 0;
				}
			}
		}
		break;

	case ledState_off:
		// Turn on LED
		appLedDrvTurnOn(ledNum);

		pLed->state = ledState_on;
		pLed->timer = pLed->onTimeMs;
		break;

	case ledState_repeatDelay:
		if (pLed->timer > TASK_CYCLE_TIME_MS) {
			pLed->timer -= TASK_CYCLE_TIME_MS;
			return;
		}

		appLedDrvTurnOn(ledNum);
		pLed->state      = ledState_on;
		pLed->timer      = pLed->onTimeMs;
		pLed->cycleCount = 0;
		break;

	default:
		// Do nothing
		break;
	}
}


/**
 * \brief LED manager task
 */
static void ledMgrTask(void * taskParam)
{
	ledMgrCtrl_t *	pCtrl = (ledMgrCtrl_t *)taskParam;
	int				ledIdx;

	while (1)
	{
		// Update the control structure every 20 milliseconds
		// Release the mutex while sleeping
		vTaskDelay(pdMS_TO_TICKS(TASK_CYCLE_TIME_MS));

		xSemaphoreTake(pCtrl->mutex, portMAX_DELAY);

		// Check for LED fade adjustment
		checkFade(pCtrl);

		// Check for LED flashing
		for (ledIdx = 0; ledIdx < NUM_LEDS; ledIdx++) {
			checkLedFlash(pCtrl, ledIdx);
		}

		xSemaphoreGive(pCtrl->mutex);
	}
}
