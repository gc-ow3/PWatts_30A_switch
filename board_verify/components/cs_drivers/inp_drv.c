/*
 * gpio_drv.c
 *
 *  Created on: Jul 19, 2022
 *      Author: wesd
 */
#include "sdkconfig.h"
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>

#include "cs_common.h"
#include "pinout.h"
#include "inp_drv.h"

static const char*	TAG = "inp_drv";

/*
 *  The number of milliseconds the input must be stable to be
 *  recognized as a valid change of state
 */
#define INP_STATE_HOLD_TIME_MS	(70)

typedef struct {
	inpId_t		id;
	const char*	name;
	struct {
		gpio_num_t	num;
		uint64_t	mask;
	} gpio;
} const inpInfo_t;

typedef enum {
	inpState_init = 0,
	inpState_pendActive,
	inpState_active,
	inpState_pendInactive,
	inpState_inactive
} inpState_t;


typedef struct {
	inpInfo_t*	info;
	inpState_t	state;
	int64_t		timeoutMs;
} inpCtrl_t;

static void inpTask(void* arg);

static inpInfo_t inpInfo[] = {
	{
		.id   = inpId_button1,
		.name = "Button 1",
		.gpio = {
			.num  = BUTTON1_GPIO,
			.mask = 1ULL << BUTTON1_GPIO
		}
	},
	{
		.id   = inpId_button2,
		.name = "Button 2",
		.gpio = {
			.num  = BUTTON2_GPIO,
			.mask = 1ULL << BUTTON2_GPIO
		}
	},
	{
		.id   = inpId_switch1,
		.name = "Switch 1",
		.gpio = {
			.num  = SWITCH1_GPIO,
			.mask = 1ULL << SWITCH1_GPIO
		}
	}
};
#define numInputs 	(sizeof(inpInfo) / sizeof(inpInfo_t))


typedef struct {
	inpCbFunc_t		cbFunc;
	void*			cbData;
	bool			isRunning;
	inpCtrl_t		inp[numInputs];
	TaskHandle_t	taskHandle;
} taskCtrl_t;


static taskCtrl_t*	taskCtrl;

esp_err_t inpDrvInit(inpCbFunc_t cbFunc, void* cbData)
{
	if (taskCtrl) {
		return ESP_OK;
	}

	taskCtrl_t*	pCtrl = calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	pCtrl->cbFunc = cbFunc;
	pCtrl->cbData = cbData;

	// Set up the discrete GPIO inputs
    inpCtrl_t*	inp;
    int 		i;
    for (i = 0, inp = pCtrl->inp; i < numInputs; i++, inp++) {
    	inp->state = inpState_init;

    	// Attach the info structure to the control channel
    	inp->info = &inpInfo[i];

    	gpio_config_t	gpioCfg = {
    		.pin_bit_mask = inp->info->gpio.mask,
    		.mode         = GPIO_MODE_INPUT,
    		.pull_down_en = GPIO_PULLDOWN_DISABLE,
    		.pull_up_en   = GPIO_PULLUP_DISABLE,
    		.intr_type    = GPIO_INTR_DISABLE
    	};
    	gpio_config(&gpioCfg);
    }

    taskCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t inpDrvStart(void)
{
	taskCtrl_t*	pCtrl = taskCtrl;
	if (!pCtrl) {
		return ESP_ERR_INVALID_STATE;
	}
	if (pCtrl->isRunning) {
		return ESP_OK;
	}

    BaseType_t	xStatus;

    xStatus = xTaskCreate(
    	inpTask,
		"inpTask",
		2000,
		(void*)pCtrl,
		7,
		&pCtrl->taskHandle
    );
    if (pdPASS == xStatus) {
    	printf("Input task started");
    }

    pCtrl->isRunning = true;
	return ESP_OK;
}


esp_err_t inpDrvRead(inpId_t id, bool* active)
{
	taskCtrl_t*	pCtrl = taskCtrl;
	if (!pCtrl || !pCtrl->isRunning) {
		return ESP_ERR_INVALID_STATE;
	}

	int	i;
	inpCtrl_t*	inp;
	for (i = 0, inp = pCtrl->inp; i < numInputs; i++, inp++) {
		if (inp->info->id == id) {
			*active = ((inp->state == inpState_active) || (inp->state == inpState_pendInactive));
			return ESP_OK;
		}
	}

	return ESP_ERR_INVALID_ARG;
}


static void inpTask(void* arg)
{
	taskCtrl_t*	pCtrl = (taskCtrl_t*)arg;

    while (true)
    {
    	SLEEP_MS(10);

    	int64_t	curTimeMs = esp_timer_get_time() / 1000;

    	// Check inputs for change of state
    	int	i;
    	inpCtrl_t*	inp;
    	for (i = 0, inp = pCtrl->inp; i < numInputs; i++, inp++) {
    		// Read current input level
    		int	level = gpio_get_level(inp->info->gpio.num);

    		if (level) {
    			// Input is high (inactive)
    			switch (inp->state)
    			{
    			case inpState_init:
    				inp->state = inpState_pendInactive;
    				inp->timeoutMs = curTimeMs + INP_STATE_HOLD_TIME_MS;
    				break;

    			case inpState_pendActive:
    				inp->state = inpState_inactive;
    				break;

    			case inpState_active:
    				inp->state = inpState_pendInactive;
    				inp->timeoutMs = curTimeMs + INP_STATE_HOLD_TIME_MS;
    				break;

    			case inpState_pendInactive:
    				if (inp->timeoutMs >= curTimeMs) {
    					inp->state = inpState_inactive;
        				if (pCtrl->cbFunc) {
        					pCtrl->cbFunc(inp->info->id, false, pCtrl->cbData);
        				}
    				}
    				break;

    			case inpState_inactive:
    				// Remain in this state
    				break;

    			default:
    				ESP_LOGE(TAG, "Invalid input state (%d)", inp->state);
    				inp->state = inpState_init;
    				break;
    			}
    		} else {
    			// Input is low (active)
    			switch (inp->state)
    			{
    			case inpState_init:
    				inp->state = inpState_pendActive;
    				inp->timeoutMs = curTimeMs + INP_STATE_HOLD_TIME_MS;
    				break;

    			case inpState_pendActive:
    				if (inp->timeoutMs >= curTimeMs) {
    					inp->state = inpState_active;
        				if (pCtrl->cbFunc) {
        					pCtrl->cbFunc(inp->info->id, true, pCtrl->cbData);
        				}
    				}
    				break;

    			case inpState_active:
    				// Remain in this state
    				break;

    			case inpState_pendInactive:
    				inp->state = inpState_active;
    				break;

    			case inpState_inactive:
    				inp->state = inpState_pendActive;
    				inp->timeoutMs = curTimeMs + INP_STATE_HOLD_TIME_MS;
    				break;

    			default:
    				ESP_LOGE(TAG, "Invalid input state (%d)", inp->state);
    				inp->state = inpState_init;
    				break;
    			}
    		}
    	}
    }
}
