/*
 * led_drv.c
 *
 */

#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "pinout.h"
#include "led_drv.h"

#include "cs_common.h"
#include "sdkconfig.h"

//static const char *TAG = "LED_DRV";

typedef struct {
	struct {
		gpio_num_t	red;
		gpio_num_t	grn;
		gpio_num_t	blu;
	} gpio;
} const chanInfo_t;


typedef struct {
	chanInfo_t	*info;
	ledMode_t	mode;
} chanCtrl_t;

typedef struct {
	bool			isStarted;
	chanCtrl_t		*chanCtrl;
} drvCtrl_t;

static chanInfo_t chanInfo[] = {
	{
		.gpio = {
			.red = LED1_R_GPIO,
			.grn = LED1_G_GPIO,
			.blu = LED1_B_GPIO,
		}
	},
	{
		.gpio = {
			.red = LED2_R_GPIO,
			.grn = LED2_G_GPIO,
			.blu = LED2_B_GPIO,
		}
	}
};


static drvCtrl_t	*drvCtrl;


esp_err_t ledDrvInit(void)
{
	if (drvCtrl) {
		return ESP_OK;
	}

	drvCtrl_t	*pCtrl = calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	// Allocate a control structure for each channel
	pCtrl->chanCtrl = calloc(LED_DRV_NUM_CHANS, sizeof(chanCtrl_t));
	if (!pCtrl->chanCtrl) {
		return ESP_ERR_NO_MEM;
	}

	// Configure GPIO outputs
	gpio_config_t	pinCfg = {
		.pin_bit_mask = 0,
		.mode         = GPIO_MODE_OUTPUT,
		.pull_up_en   = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE
	};

	chanCtrl_t	*chan;
	int	i;
	for (i = 0, chan = pCtrl->chanCtrl; i < LED_DRV_NUM_CHANS; i++, chan++) {
		// Attach the info structure to the channel
		chan->info = &chanInfo[i];

		// Outputs initially off
		gpio_set_level(chan->info->gpio.red, 1);
		gpio_set_level(chan->info->gpio.grn, 1);
		gpio_set_level(chan->info->gpio.blu, 1);

		pinCfg.pin_bit_mask =
			(1ULL << chan->info->gpio.red) |
			(1ULL << chan->info->gpio.grn) |
			(1ULL << chan->info->gpio.blu)
		;
		gpio_config(&pinCfg);
	}

	drvCtrl = pCtrl;
	return ESP_OK;
}


esp_err_t ledDrvStart(void)
{
	// Nothing special to do
	return ESP_OK;
}


esp_err_t ledDrvSetMode(ledId_t led, ledMode_t mode)
{
	if (led < 0 || led >= LED_DRV_NUM_CHANS) {
		return ESP_ERR_INVALID_ARG;
	}

	chanInfo_t	*info = &chanInfo[led];

	switch (led)
	{
	case ledId_system:
		info = &chanInfo[0];
		break;

	case ledId_ble:
		info = &chanInfo[1];
		break;

	default:
		return ESP_ERR_INVALID_ARG;
	}

	// Apply changes per mode
	switch (mode)
	{
	case ledMode_off:
		gpio_set_level(info->gpio.red, 1);
		gpio_set_level(info->gpio.grn, 1);
		gpio_set_level(info->gpio.blu, 1);
		break;

	case ledMode_red:
		gpio_set_level(info->gpio.red, 0);
		gpio_set_level(info->gpio.grn, 1);
		gpio_set_level(info->gpio.blu, 1);
		break;

	case ledMode_grn:
		gpio_set_level(info->gpio.red, 1);
		gpio_set_level(info->gpio.grn, 0);
		gpio_set_level(info->gpio.blu, 1);
		break;

	case ledMode_blu:
		gpio_set_level(info->gpio.red, 1);
		gpio_set_level(info->gpio.grn, 1);
		gpio_set_level(info->gpio.blu, 0);
		break;

	default:
		return ESP_ERR_INVALID_ARG;
	}

	return ESP_OK;
}
