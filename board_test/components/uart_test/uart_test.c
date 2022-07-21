#include "sdkconfig.h"

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#include "uart_test.h"
#include "pinout.h"

static const char TAG[] = {"UART_TEST"};

static void loopTask(void *arg);
static void spyTask(void *arg);

typedef enum {
	taskMode_stop = 0,
	taskMode_loop,
	taskMode_spy
} taskMode_t;

typedef struct {
	const char *	name;
	uart_port_t		port;
	UBaseType_t		taskPriority;
	gpio_num_t		gpioTx;
	gpio_num_t		gpioRx;
} const uartInfo_t;


typedef struct {
	uartInfo_t	*pInfo;
	taskMode_t	mode;
	const char	*modeStr;
} taskCtrl_t;

static uartInfo_t	uartInfo[] = {
	{
		.name = "UART1",
		.port = UART_NUM_1,
		.taskPriority = 4,
		.gpioTx = UART1_TX_GPIO,
		.gpioRx = UART1_RX_GPIO
	},
	{
		.name = "UART2",
		.port = UART_NUM_2,
		.taskPriority = 4,
		.gpioTx = UART_PIN_NO_CHANGE,
		.gpioRx = UART2_RX_GPIO
	}
};

static taskCtrl_t	taskCtrl[2];

static esp_err_t _uartModeStart(uart_port_t port, uint32_t baud, taskMode_t mode)
{
	taskCtrl_t	*pCtrl;
	uartInfo_t	*pInfo;

	if (UART_NUM_1 == port) {
		pCtrl = &taskCtrl[0];
		pInfo = &uartInfo[0];
	} else if (UART_NUM_2 == port) {
		pCtrl = &taskCtrl[1];
		pInfo = &uartInfo[1];
	} else {
		return ESP_ERR_INVALID_ARG;
	}
	pCtrl->pInfo = pInfo;

	if (pCtrl->mode != taskMode_stop) {
		return ESP_ERR_INVALID_STATE;
	}

	void	(*taskFunc)(void *arg);

	switch (mode)
	{
	case taskMode_loop:
		pCtrl->modeStr = "Loop";
		taskFunc = loopTask;
		break;

	case taskMode_spy:
		pCtrl->modeStr = "Spy";
		taskFunc = spyTask;
		break;

	default:
		return ESP_ERR_INVALID_ARG;
	}

	esp_err_t	status;

	uart_config_t	uCfg = {
		.baud_rate = baud,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	status = uart_param_config(pInfo->port, &uCfg);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d configuring UART for %s", status, pInfo->name);
		return status;
	}

	uart_set_pin(pInfo->port, pInfo->gpioTx, pInfo->gpioRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	status = uart_driver_install(pInfo->port, 1024, 0, 0, NULL, 0);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d installing UART for %s", status, pInfo->name);
		return status;
	}

	pCtrl->mode = mode;
	status = xTaskCreate(
		taskFunc,
		pInfo->name,
		3000,
		(void *)pCtrl,
		pInfo->taskPriority,
		NULL
	);
	if (pdPASS != status) {
		ESP_LOGE(TAG, "Failed to start %s", pInfo->name);
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t uartLoopStart(uart_port_t port, uint32_t baud)
{
	return _uartModeStart(port, baud, taskMode_loop);
}

esp_err_t uartSpyStart(uart_port_t port, uint32_t baud)
{
	return _uartModeStart(port, baud, taskMode_spy);
}


static esp_err_t _uartModeStop(uart_port_t port, taskMode_t mode)
{
	taskCtrl_t *pCtrl;

	if (UART_NUM_1 == port) {
		pCtrl = &taskCtrl[0];
	} else if (UART_NUM_2 == port) {
		pCtrl = &taskCtrl[1];
	} else {
		return ESP_ERR_INVALID_ARG;
	}

	if (pCtrl->mode != mode) {
		return ESP_ERR_INVALID_STATE;
	}

	pCtrl->mode = taskMode_stop;
	vTaskDelay(pdMS_TO_TICKS(200));
	return ESP_OK;
}


esp_err_t uartLoopStop(uart_port_t port)
{
	return _uartModeStop(port, taskMode_loop);
}


esp_err_t uartSpyStop(uart_port_t port)
{
	return _uartModeStop(port, taskMode_spy);
}


static void loopTask(void *arg)
{
	taskCtrl_t	*pCtrl = arg;
	uartInfo_t	*pInfo = pCtrl->pInfo;

	uint8_t	buf[64];
	int	rxLen;

	//ESP_LOGI(TAG, "Loop task '%s' started", pInfo->name);

	while (taskMode_stop != pCtrl->mode)
	{
		rxLen = uart_read_bytes(pInfo->port, buf, sizeof(buf), pdMS_TO_TICKS(100));
		if (rxLen > 0) {
			//ESP_LOGI(TAG, "%s received %d bytes", pInfo->name, rxLen);
			uart_write_bytes(pInfo->port, (const char *)buf, rxLen);
		}
	}
	uart_driver_delete(pInfo->port);

	//ESP_LOGI(TAG, "Loop task '%s' stopped", pInfo->name);
	vTaskDelete(NULL);
}


static void spyTask(void *arg)
{
	taskCtrl_t	*pCtrl = arg;
	uartInfo_t	*pInfo = pCtrl->pInfo;

	uint8_t	buf[64];
	int	rxLen;

	//ESP_LOGI(TAG, "Spy task '%s' started", pInfo->name);

	while (taskMode_stop != pCtrl->mode)
	{
		rxLen = uart_read_bytes(pInfo->port, buf, sizeof(buf), pdMS_TO_TICKS(100));
		if (rxLen > 0) {
			//ESP_LOGI(TAG, "%s received %d bytes", pInfo->name, rxLen);

			int i;
			for (i = 0; i < rxLen; i++) {
				printf("%c", buf[i]);
			}
		}
	}
	uart_driver_delete(pInfo->port);

	//ESP_LOGI(TAG, "Spy task '%s' stopped", pInfo->name);
	vTaskDelete(NULL);
}
