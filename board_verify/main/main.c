#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "sdkconfig.h"
#include <driver/uart.h>
#include <esp_log.h>
#include "cs_common.h"
#include "pinout.h"
#include "led_drv.h"
#include "inp_drv.h"
#include "emtr_drv.h"

static const char* TAG = "Main";

static void ledTask(void* arg)
{
	printf("LED task running\n");

	ledDrvSetMode(ledNum_system, ledMode_off);
	ledDrvSetMode(ledNum_ble, ledMode_off);

	while (true)
	{
		SLEEP_MS(500);
		ledDrvSetMode(ledNum_system, ledMode_red);

		SLEEP_MS(500);
		ledDrvSetMode(ledNum_system, ledMode_grn);
		ledDrvSetMode(ledNum_ble, ledMode_red);

		SLEEP_MS(500);
		ledDrvSetMode(ledNum_system, ledMode_blu);
		ledDrvSetMode(ledNum_ble, ledMode_grn);

		SLEEP_MS(500);
		ledDrvSetMode(ledNum_system, ledMode_off);
		ledDrvSetMode(ledNum_ble, ledMode_blu);

		SLEEP_MS(500);
		ledDrvSetMode(ledNum_ble, ledMode_off);
	}
}


static void uart1Task(void* arg)
{
	esp_err_t	status;

	status = uart_driver_install(UART_NUM_1, 1024, 0, 0, NULL, 0);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d installing UART 1", status);
		return;
	}

	uart_config_t	uCfg = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	status = uart_param_config(UART_NUM_1, &uCfg);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d configuring UART 1", status);
		return;
	}

	uart_set_pin(UART_NUM_1, UART1_TX_GPIO, UART1_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	while (true)
	{
		int	rxLen;
		uint8_t	buf[256];

		rxLen = uart_read_bytes(UART_NUM_1, buf, sizeof(buf), pdMS_TO_TICKS(100));
		if (rxLen > 0) {
			uart_write_bytes(UART_NUM_1, (const char *)buf, rxLen);
		}
	}
}


static void uart2Task(void* arg)
{
	esp_err_t	status;

	status = uart_driver_install(UART_NUM_2, 1024, 0, 0, NULL, 0);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d installing UART 2", status);
		return;
	}

	uart_config_t	uCfg = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity    = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};

	status = uart_param_config(UART_NUM_2, &uCfg);
	if (ESP_OK != status) {
		ESP_LOGE(TAG, "Error %d configuring UART 2", status);
		return;
	}

	uart_set_pin(UART_NUM_2, UART_PIN_NO_CHANGE, UART2_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	while (true)
	{
		int	rxLen;
		uint8_t	buf[256];

		rxLen = uart_read_bytes(UART_NUM_2, buf, sizeof(buf), pdMS_TO_TICKS(100));
		if (rxLen > 0) {
			printf("UART2 received %d bytes\n", rxLen);
		}
	}
}

static void inpCallback(inpId_t inpId, bool active, void* cbData)
{
	// Ignore this
	(void)cbData;

	const char*	inpName;
	const char*	state;
	char	buf[10];

	switch(inpId)
	{
	case inpId_button1:
		inpName = "Button 1";
		state = active ? "Pressed" : "Released";
		break;
	case inpId_button2:
		inpName = "Button 2";
		state = active ? "Pressed" : "Released";
		break;
	case inpId_switch1:
		inpName = "Switch 1";
		state = active ? "Closed" : "Open";
		break;
	default:
		snprintf(buf, sizeof(buf), "Input %d", inpId);
		inpName = (const char*)buf;
		state = active ? "Active" : "Inactive";
		break;
	}

	printf("\n%s: %s\n", inpName, state);
}


void app_main(void)
{
    printf("\n");
    printf("Powder Watts 30-Amp board verification\n");
    printf("\n");

    printf("Initialize drivers\n");
    ledDrvInit();
    inpDrvInit(inpCallback, NULL);

#if 1
    emtrDrvConf_t	emtrConf = {
    	.uartPort   = UART_NUM_1,
		.baudRate   = 115200,
		.rxGpio     = UART1_RX_GPIO,
		.txGpio     = UART1_TX_GPIO,
		.resetGpio  = EMTR_RST_GPIO,
    };
    emtrDrvInit(&emtrConf);
#endif

    printf("Start drivers\n");
    ledDrvStart();
    inpDrvStart();
    emtrDrvStart();

#if 1
	static const ledMode_t ledMode[4] = {
		ledMode_red, ledMode_grn, ledMode_blu, ledMode_off
	};

	int	modeIdx;

	for (modeIdx = 0; modeIdx < 4; modeIdx++) {
		ledMode_t	mode = ledMode[modeIdx];

		ledDrvSetMode(ledNum_system, mode);
		ledDrvSetMode(ledNum_ble, mode);
		SLEEP_MS(500);
	}
#endif

    BaseType_t		xStatus;
    TaskHandle_t	taskHandle;

#if 1
    printf("Start LED task\n");
    xStatus = xTaskCreate(
    	ledTask,
		"ledTask",
		2000,
		NULL,
		7,
		&taskHandle
    );
    if (pdPASS == xStatus) {
    	printf("LED task started\n");
    }
#endif

#if 1
    printf("Start UART1 task\n");
    xStatus = xTaskCreate(
    	uart1Task,
		"uart1Task",
		2000,
		NULL,
		7,
		NULL
    );
    if (pdPASS == xStatus) {
    	printf("UART 1 task started\n");
    }
#endif

#if 1
    printf("Start UART2 task\n");
    xStatus = xTaskCreate(
    	uart2Task,
		"uart2Task",
		2000,
		NULL,
		7,
		NULL
    );
    if (pdPASS == xStatus) {
    	printf("UART 2 task started\n");
    }
#endif
}
