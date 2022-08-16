#include "sdkconfig.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <driver/uart.h>
//#include <esp_flash_encrypt.h>

#include "cs_common.h"
#include "pinout.h"
#include "led_drv.h"
#include "inp_drv.h"
#include "app_emtr_drv.h"
#include "emtr_pwr_sig.h"

static const char	TAG[] = {"app_main"};

/*
********************************************************************************
* Local functions
********************************************************************************
*/
static esp_err_t modulesInit(void);
static esp_err_t modulesStart(void);

/*
********************************************************************************
* Constant data
********************************************************************************
*/
#if 0
// Product identifying information
static const csCoreInit0_t coreInit0 = {
	.manufacturer  = "Wayne Water",
	.product       = "Halo",
	.model         = "WW-HALO",
	.mfgModel      = "MFG-HALO",
	.fwVersion     = appFwVer,
	.mqttTopicRoot = "iot8020",
	.features = {
		.iotPing    = 0,	// ToDo Enable when cloud support is added
		.secMfgData = 0
	},
	.otaUpdate     = {
#if CONFIG_SECURE_BOOT
		.type = "mcu",
#else
		.type = "xmcu",
#endif
		.reqCert = true
	}
};
#endif

#ifdef CONFIG_IOT8020_USE_BLUFI

// BLUFI provisioning UUID in reverse order:
// 0000FFFF-0000-1000-8000-00805F9B34FB
static const uint8_t	blufiProvUuid[16] = {
	0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00,
	0x00, 0x80,
	0x00, 0x10,
	0x00, 0x00,
	0xFF, 0xFF,		// Service id
	0x00, 0x00
};

#endif


/*
********************************************************************************
* Global data
********************************************************************************
*/
//appSelfTest_t	appSelfTest;
//appInfo_t		appInfo;


/*
********************************************************************************
* Private data
********************************************************************************
*/


/*
********************************************************************************
* Public functions
********************************************************************************
*/


/**
 * \brief Product software execution starts here
 *
 */
void app_main(void)
{
	// Do not change this message because factory process checks for it
	printf("\r\nApplication start\r\n");

	// Start the LED manager first
	// ToDo Read the slide switch to select brightness
	ESP_ERROR_CHECK(ledDrvInit(100));
	ESP_ERROR_CHECK(ledDrvStart());

	ledMode_t	ledMode[] = {ledMode_red, ledMode_grn, ledMode_blu};

	// Flash the LEDs to show boot
	int	i;
	for (i = 0; i < 3; i++) {
		ledMode_t	mode = ledMode[i];

		vTaskDelay(pdMS_TO_TICKS(200));
		ledDrvSetMode(ledId_system, mode);
		ledDrvSetMode(ledId_ble, mode);

		vTaskDelay(pdMS_TO_TICKS(200));
		ledDrvSetMode(ledId_system, ledMode_off);
		ledDrvSetMode(ledId_ble, ledMode_off);
	}

	ESP_ERROR_CHECK(inpDrvInit(NULL, NULL));
	ESP_ERROR_CHECK(inpDrvStart());

	// Do low-level core initialization to load manufacturing data and parameters first
//	ESP_LOGD(TAG, "csCoreInit0");
//	ESP_ERROR_CHECK(csCoreInit0((csCoreInit0_t *)&coreInit0));

	// Load application manufacturing data
//	ESP_LOGD(TAG, "appMfgDataLoad");
//	ESP_ERROR_CHECK(appMfgDataLoad());

	// Load application parameters
//	ESP_LOGD(TAG, "appParamsLoad");
//	ESP_ERROR_CHECK(appParamsLoad());

	// Start EMTR driver
	// Flash LEDs in case firmware update is required during start
	ESP_ERROR_CHECK(appEmtrDrvInit());

	//ledMgrFlashLed(ledId_system, 250, 250, 0, 0);
	ESP_ERROR_CHECK(appEmtrDrvStart());
	//ledMgrTurnLedOff(ledId_system);

#if 0
	// Initialize the CS core
	csCoreInit1_t	coreInit1 = {
		.awsConf = {
			.clientTaskPriority = TASK_PRIO_AWS_CLIENT,
			.connectionTimeoutMs = (5UL * 60UL * 1000UL)
		},
#ifdef CONFIG_IOT8020_USE_BLUFI
		.blufiConf = {
			.uuid       = blufiProvUuid,
			.appMethods = NULL,
			.features = {
				.accountId  = 1,
				.claimCode  = 1,
				.iotURL     = 0,
				.apiURL     = 0,
				.instDate   = 1
			}
		},
#endif
	};

	ESP_LOGD(TAG, "csCoreInit1");
	ESP_ERROR_CHECK(csCoreInit1(&coreInit1));
#endif

	// Print sign-on information
#if 0
	csCoreSignon();
	printf("---- Device-specific info ----\r\n");
	printf("(TBD)\r\n");
	printf("\r\n");
#endif

	// Display the parameters
//	paramMgrSettingsDump();

	// Initialize application modules
	ESP_ERROR_CHECK(modulesInit());

#if 0
	// Start application modules that may need to act on events from the core starting
	esp_err_t	status;

	if ((status = appControlStart()) != ESP_OK) {
		ESP_LOGE(TAG, "appControlStart error %d", status);
	}

	// Start core modules
	csCoreStartParams_t	startParams = {
		.softApProvConf = {
			.rsaPrvKey = appMfgData.rsaPrvKey,
			// Select provisioning features used by this device
			.features = {
				.accountId = 1,
				.claimCode = 1,
				.iotURL    = 0,
				.apiURL    = 0,
				.instDate  = 1
			},
			.testFuncCb = appSoftApTestCb
		}
	};

	ESP_LOGD(TAG, "Start core modules");
	ESP_ERROR_CHECK(csCoreStart(&startParams));
#endif

	// Start application modules
	ESP_ERROR_CHECK(modulesStart());
}


/*
********************************************************************************
* Private functions
********************************************************************************
*/


void pwrSigCallback(
	pwrSigMeta_t*	meta,
	uint8_t*		data,
	int				dataLen,
	void*			cbData
)
{
#if 0
	ESP_LOGI(TAG, "pwrSig meta data");
	ESP_LOGI(TAG, "  Reason          : %s", pwrSigReasonStr(meta->reason));
	ESP_LOGI(TAG, "  Samples         : %u", meta->numSamples);
	ESP_LOGI(TAG, "  Resolution      : %u", meta->resolution);
	ESP_LOGI(TAG, "  Relay cycles    : %u", meta->relayCycles);
	ESP_LOGI(TAG, "  Seconds powered : %u", meta->timePowered);
	ESP_LOGI(TAG, "  Seconds running : %u", meta->timeRunning);
	if (pwrSigReason_on == meta->reason) {
		ESP_LOGI(TAG, "  Inrush amps     : %0.3f", (double)meta->mAmpsInrush/1000.0);
	} else if (pwrSigReason_off == meta->reason) {
		ESP_LOGI(TAG, "  Cycle length    : %u", meta->cycleLength);
		ESP_LOGI(TAG, "  Average volts   : %0.2f", (double)meta->dVolts/10.0);
		ESP_LOGI(TAG, "  Average amps    : %0.2f", (double)meta->mAmps/1000.0);
		ESP_LOGI(TAG, "  Average pFactor : %u", meta->pFactor);
		ESP_LOGI(TAG, "  Temperature     : %u", meta->temperature);
	}
#endif

	// ToDo

}


/**
 * \brief Perform product-specific module initializations
 */
static esp_err_t modulesInit(void)
{
	ESP_LOGI(TAG, "Initialize modules");

#if 0
	pwrSigConf_t	pwrSigConf = {
		.port = UART_NUM_2,
		.rxGpio = UART2_RX_GPIO,
		.baudRate = 921600,
		.taskPriority = 4,
		.cbFunc = pwrSigCallback,
		.cbData = NULL
	};
	ESP_ERROR_CHECK(pwrSigInit(&pwrSigConf));
#endif

	return ESP_OK;
}

static void printAlarms(appEmtrAlarm_t alarms, char* buf, int bufSz)
{
	const char *	flagList[8];
	int				flagCt = 0;

#if 0
	if (alarms.resvd_7)
		flagList[flagCt++] = "rsvd-7";
	if (alarms.resvd_6)
		flagList[flagCt++] = "rsvd-6";
	if (alarms.resvd_5)
		flagList[flagCt++] = "rsvd-5";
	if (alarms.resvd_4)
		flagList[flagCt++] = "rsvd-4";
#endif
	if (alarms.item.temp)
		flagList[flagCt++] = "temp";
	if (alarms.item.gfci)
		flagList[flagCt++] = "gfci";
	if (alarms.item.relayOn)
		flagList[flagCt++] = "relay_on";
	if (alarms.item.relayOff)
		flagList[flagCt++] = "relay_off";

	*buf = '\0';
	int		i;

	for (i = 0; i < flagCt; i++) {
		if (i > 0) {
			strcat(buf, ", ");
		}
		strcat(buf, flagList[i]);
	}
}


void testTask(void* arg)
{
	esp_err_t	status;
	inpState_t	inpState;
	inpState_t	inpStatePrv = inpState_init;

	while (1)
	{
		vTaskDelay(pdMS_TO_TICKS(5000));

		appEmtrStatus_t		eStatus;

		printf("-----------------------\r\n");

		status = appEmtrDrvGetStatus(&eStatus);
		if (ESP_OK == status) {
			char alarmStr[60];
			printAlarms(eStatus.alarm.flags, alarmStr, sizeof(alarmStr));

			printf("EMTR status\r\n");
			printf("  Relay state    : %s\r\n", eStatus.relayStatus.str);
			printf("  Output state   : %s\r\n", eStatus.outputStatus.str);
			printf("  Alarms         : %02x [%s]\r\n", eStatus.alarm.flags.mask, alarmStr);
			printf("  Temperature (C): %d\r\n", eStatus.tempC);
			printf("\r\n");
		} else {
			ESP_LOGE(TAG, "Error %d reading EMTR status", status);
		}

		appEmtrTotals_t		eTotals;

		status = appEmtrDrvGetTotals(&eTotals);
		if (ESP_OK == status) {
			printf("EMTR totals\r\n");
			printf("  Epoch     : %u\r\n", eTotals.epoch);
			printf("  Cycles    : %u\r\n", eTotals.relayCycles);
			printf("  On        : %u seconds\r\n", eTotals.onDuration);
			printf("  Watt-Hours: %1.1f\r\n", (float)eTotals.dWattH/10.0);
			printf("\r\n");
		} else {
			ESP_LOGE(TAG, "Error %d reading EMTR totals", status);
		}

		appEmtrInstant_t	eInstant;

		status = appEmtrDrvGetInstant(&eInstant);
		if (ESP_OK == status) {
			printf("EMTR instant\r\n");
			printf("  Uptime      : %u\r\n", eInstant.uptime);
			printf("  Volts       : %1.1f\r\n", (float)eInstant.dVolts/10.0);
			printf("  Amps        : %1.3f\r\n", (float)eInstant.mAmps/1000.0);
			printf("  Watts       : %1.1f\r\n", (float)eInstant.dWatts/10.0);
			printf("  Power factor: %u\r\n", eInstant.pFactor);
			printf("  Relay on    : %u seconds\r\n", eInstant.relayOnSecs);
			printf("\r\n");
		} else {
			ESP_LOGE(TAG, "Error %d reading EMTR instant", status);
		}

		if (inpDrvStateRead(inpId_switch1, &inpState) == ESP_OK) {
			if (inpStatePrv != inpState) {
				inpStatePrv = inpState;

				if (inpState_active == inpState) {
					printf("Turn on relay\r\n");
					appEmtrDrvSetRelay(true);
				} else {
					printf("Turn off relay\r\n");
					appEmtrDrvSetRelay(false);
				}
				printf("\r\n");
			}
		}
	}
}

/**
 * \brief Start product-specific modules
 */
static esp_err_t modulesStart(void)
{
	ESP_LOGI(TAG, "Start modules");

#if 0
	ESP_ERROR_CHECK(pwrSigStart());
#endif

	// ToDo ?

	xTaskCreate(
		testTask,
		"test_emtr",
		4000,
		NULL,
		5,
		NULL
	);

	return ESP_OK;
}


