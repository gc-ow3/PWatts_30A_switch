/*
 * emtr_drv.c
 *
 *  Created on: Jul 20, 2022
 *      Author: wesd
 */


#include "emtr_drv.h"

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

// EMTR message framing characters
#define MSG_CHAR_SOP					((uint8_t)0x1B)
#define MSG_CHAR_EOP					((uint8_t)0x0A)


typedef struct {
	emtrDrvConf_t	conf;
} taskCtrl_t;


static taskCtrl_t*	taskCtrl;


esp_err_t emtrDrvInit(emtrDrvConf_t* conf)
{
	if (taskCtrl) {
		return ESP_OK;
	}

	taskCtrl_t*	pCtrl = calloc(1, sizeof(*pCtrl));
	if (!pCtrl) {
		return ESP_ERR_NO_MEM;
	}

	pCtrl->conf = *conf;

	// Set up the EMTR reset GPIO
	gpio_config_t	gpioCfg = {
		.pin_bit_mask = 1ULL << pCtrl->conf.resetGpio,
		.mode         = GPIO_MODE_OUTPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en   = GPIO_PULLUP_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE
	};
	gpio_config(&gpioCfg);
	// Initially put the EMTR into reset
	gpio_set_level(pCtrl->conf.resetGpio, 0);

#if 0
	esp_err_t	status;

	// Install the UART driver
	status = uart_driver_install(pCtrl->conf.uartPort, 128 * 2, 0, 0, NULL, 0);
	if (ESP_OK != status) {
		return status;
	}

	// Configure the UART
	uart_config_t	uCfg = {
		.baud_rate = pCtrl->conf.baudRate,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	if ((status = uart_param_config(pCtrl->conf.uartPort, &uCfg)) != ESP_OK) {
		return status;
	}

	// Assign the GPIO pins
	status = uart_set_pin(
		pCtrl->conf.uartPort,
		pCtrl->conf.txGpio,
		pCtrl->conf.rxGpio,
		UART_PIN_NO_CHANGE,	// RTS not used,
		UART_PIN_NO_CHANGE	// CTS not used
	);
	if (ESP_OK != status) {
		return status;
	}

	// ToDo ?

#endif

	taskCtrl = pCtrl;
	return ESP_OK;
}

esp_err_t emtrDrvStart(void)
{
	// ToDo

	return ESP_OK;
}


