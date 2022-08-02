/*
 * gpio_drv.c
 *
 *  Created on: Jul 19, 2022
 *      Author: wesd
 */

#include <esp_err.h>
#include <driver/gpio.h>

#include "cs_common.h"
#include "pinout.h"
#include "inp_drv.h"

typedef struct {
	inpId_t		id;
	const char*	name;
	struct {
		gpio_num_t	num;
		uint64_t	mask;
	} gpio;
} const inpInfo_t;

typedef struct {
	inpInfo_t*	info;
	inpState_t	state;
} inpCtrl_t;

typedef struct {
	inpCbFunc_t	cbFunc;
	void*		cbData;
	bool		isRunning;
	inpCtrl_t*	inp;
} taskCtrl_t;

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
#define NUM_INPUTS 	(sizeof(inpInfo) / sizeof(inpInfo_t))

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
	pCtrl->inp = calloc(NUM_INPUTS, sizeof(inpCtrl_t));
	if (!pCtrl->inp) {
		return ESP_ERR_NO_MEM;
	}

	// Set up the discrete GPIO inputs
    inpCtrl_t*	inp;
    int 		i;
    for (i = 0, inp = pCtrl->inp; i < NUM_INPUTS; i++, inp++) {
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
		NULL
    );
    if (pdPASS == xStatus) {
    	printf("Input task started");
    }

    pCtrl->isRunning = true;
	return ESP_OK;
}


esp_err_t inpDrvStateRead(inpId_t id, inpState_t* state)
{
	taskCtrl_t*	pCtrl = taskCtrl;
	if (!pCtrl || !pCtrl->isRunning) {
		return ESP_ERR_INVALID_STATE;
	}

	int	i;
	inpCtrl_t*	inp;
	for (i = 0, inp = pCtrl->inp; i < NUM_INPUTS; i++, inp++) {
		if (inp->info->id == id) {
			*state = inp->state;
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
    	vTaskDelay(pdMS_TO_TICKS(50));

    	// Check inputs for change of state
    	int	i;
    	inpCtrl_t*	inp;
    	for (i = 0, inp = pCtrl->inp; i < NUM_INPUTS; i++, inp++) {
    		inpState_t	curState;
    		curState = gpio_get_level(inp->info->gpio.num) == 0 ? inpState_active : inpState_inactive;

    		if (curState != inp->state) {
    			// Note change of state
    			inp->state = curState;

    			if (pCtrl->cbFunc) {
    				pCtrl->cbFunc(
    					inp->info->id,
						(curState == inpState_active),
						pCtrl->cbData
					);
    			}
    		}
    	}
    }
}
